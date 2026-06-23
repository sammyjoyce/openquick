package api

import (
	"bufio"
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"net/http"
	"strings"
	"time"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
	"openquick.dev/quickd/internal/uploads"
)

const (
	providerChatResponseLimit  = 10 << 20
	providerImageResponseLimit = 32 << 20
)

type aiMessage struct {
	Role    string `json:"role"`
	Content string `json:"content"`
}

type aiUsage struct {
	PromptTokens     int `json:"prompt_tokens"`
	CompletionTokens int `json:"completion_tokens"`
}

type aiChatRequest struct {
	Messages []aiMessage `json:"messages"`
	Model    string      `json:"model"`
	Stream   bool        `json:"stream"`
}

type aiChatResponse struct {
	ID      string    `json:"id"`
	Model   string    `json:"model"`
	Message aiMessage `json:"message"`
	Usage   aiUsage   `json:"usage"`
}

type aiImageRequest struct {
	Prompt string `json:"prompt"`
	Model  string `json:"model"`
	Size   string `json:"size"`
}

type errUnknownModel struct{ model string }

func (e errUnknownModel) Error() string {
	if e.model == "" {
		return "unknown model"
	}
	return "unknown model: " + e.model
}

func (s *Server) handleAI(w http.ResponseWriter, r *http.Request, site string, id *identity.Identity) {
	if !s.Config.AIConfigured() {
		http.Error(w, "ai disabled", http.StatusServiceUnavailable)
		return
	}
	switch r.URL.Path {
	case "/_quick/ai/chat":
		s.handleAIChat(w, r, site, id)
	case "/_quick/ai/images":
		s.handleAIImages(w, r, site, id)
	default:
		http.NotFound(w, r)
	}
}

func (s *Server) handleAIChat(w http.ResponseWriter, r *http.Request, site string, id *identity.Identity) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w)
		return
	}
	if !s.dataAllowed(r, id) {
		http.Error(w, "authentication required", http.StatusForbidden)
		return
	}
	var req aiChatRequest
	if err := readAIJSON(w, r, s.Config.AI.Limits.MaxRequestBytes, &req); err != nil {
		http.Error(w, err.Error(), statusForBodyError(err))
		return
	}
	if len(req.Messages) == 0 {
		http.Error(w, "messages are required", http.StatusBadRequest)
		return
	}
	for _, msg := range req.Messages {
		if strings.TrimSpace(msg.Role) == "" || msg.Content == "" {
			http.Error(w, "messages must include role and content", http.StatusBadRequest)
			return
		}
	}
	provider, model, err := s.resolveAIProvider(req.Model)
	if err != nil {
		if errors.As(err, &errUnknownModel{}) {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	if !provider.Available {
		http.Error(w, "provider unavailable", http.StatusServiceUnavailable)
		return
	}
	if ok := s.allowAIRequest(w, r, site, id); !ok {
		return
	}
	if req.Stream {
		s.streamAIChat(w, r, site, id, provider, model, req.Messages)
		return
	}
	resp, err := s.providerChat(r.Context(), provider, model, req.Messages)
	if err != nil {
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	if s.Store != nil {
		if err := s.Store.RecordAIAudit(r.Context(), site, identity.SubjectKey(id), provider.Name, resp.Model, resp.Usage.PromptTokens, resp.Usage.CompletionTokens); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
	}
	writeJSON(w, http.StatusOK, resp)
}

func (s *Server) handleAIImages(w http.ResponseWriter, r *http.Request, site string, id *identity.Identity) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w)
		return
	}
	if !s.dataAllowed(r, id) {
		http.Error(w, "authentication required", http.StatusForbidden)
		return
	}
	var req aiImageRequest
	if err := readAIJSON(w, r, s.Config.AI.Limits.MaxRequestBytes, &req); err != nil {
		http.Error(w, err.Error(), statusForBodyError(err))
		return
	}
	if strings.TrimSpace(req.Prompt) == "" {
		http.Error(w, "prompt is required", http.StatusBadRequest)
		return
	}
	provider, model, err := s.resolveAIProvider(req.Model)
	if err != nil {
		if errors.As(err, &errUnknownModel{}) {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	if provider.Type != "openai" {
		http.Error(w, "provider does not support images", http.StatusNotImplemented)
		return
	}
	if !provider.Available {
		http.Error(w, "provider unavailable", http.StatusServiceUnavailable)
		return
	}
	if ok := s.allowAIRequest(w, r, site, id); !ok {
		return
	}
	imageBytes, contentType, err := s.openAIImage(r.Context(), provider, model, req.Prompt, req.Size)
	if err != nil {
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	if s.Uploads == nil {
		http.Error(w, "uploads unavailable", http.StatusServiceUnavailable)
		return
	}
	up, err := s.Uploads.SaveRaw(r.Context(), site, identity.SubjectKey(id), "ai-image", contentType, bytes.NewReader(imageBytes))
	if err != nil {
		http.Error(w, err.Error(), uploads.ErrorStatus(err))
		return
	}
	if s.Store != nil {
		if err := s.Store.RecordAIAudit(r.Context(), site, identity.SubjectKey(id), provider.Name, model, 0, 0); err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{"id": up.ID, "model": model, "url": uploads.URL(up.ID)})
}

func readAIJSON(w http.ResponseWriter, r *http.Request, limit int64, out any) error {
	if limit <= 0 {
		limit = 1 << 20
	}
	r.Body = http.MaxBytesReader(w, r.Body, limit+1)
	data, err := io.ReadAll(r.Body)
	if err != nil {
		return err
	}
	if int64(len(data)) > limit {
		return errBodyTooLarge
	}
	data = bytes.TrimSpace(data)
	if len(data) == 0 {
		return errors.New("empty JSON body")
	}
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	if err := dec.Decode(out); err != nil {
		return err
	}
	return nil
}

var errBodyTooLarge = errors.New("request body too large")

func statusForBodyError(err error) int {
	if errors.Is(err, errBodyTooLarge) || strings.Contains(err.Error(), "request body too large") {
		return http.StatusRequestEntityTooLarge
	}
	return http.StatusBadRequest
}

func (s *Server) allowAIRequest(w http.ResponseWriter, r *http.Request, site string, id *identity.Identity) bool {
	if s.RateLimiter != nil && !s.RateLimiter.Allow("ai:rpm", identity.SubjectKey(id), s.Config.AI.Limits.RequestsPerMinutePerIdentity, time.Minute) {
		writeAIRateLimit(w, "ai:rpm", "rate limited", s.Config.AI.Limits.RequestsPerMinutePerIdentity, time.Now().UTC().Add(time.Minute))
		return false
	}
	if s.Store != nil {
		now := time.Now().UTC()
		dayStart := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, time.UTC).Unix()
		ok, err := s.Store.AllowRateLimit(r.Context(), "ai:daily", site, dayStart, s.Config.AI.Limits.RequestsPerDayPerSite)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return false
		}
		if !ok {
			writeAIRateLimit(w, "ai:daily", "daily budget exceeded", s.Config.AI.Limits.RequestsPerDayPerSite, time.Unix(dayStart, 0).UTC().Add(24*time.Hour))
			return false
		}
	}
	return true
}

func writeAIRateLimit(w http.ResponseWriter, scope, message string, limit int, reset time.Time) {
	now := time.Now().UTC()
	retryAfter := int(reset.Sub(now).Seconds())
	if retryAfter < 1 {
		retryAfter = 1
	}
	w.Header().Set("Retry-After", fmt.Sprintf("%d", retryAfter))
	writeJSON(w, http.StatusTooManyRequests, map[string]any{
		"error":       message,
		"code":        "rate_limited",
		"scope":       scope,
		"limit":       limit,
		"reset":       reset.Format(time.RFC3339),
		"retry_after": retryAfter,
	})
}

func (s *Server) resolveAIProvider(requested string) (config.AIProviderConfig, string, error) {
	providers := s.Config.AI.Providers
	if len(providers) == 0 || !s.Config.AI.Enabled {
		return config.AIProviderConfig{}, "", errors.New("ai disabled")
	}
	requested = strings.TrimSpace(requested)
	if requested == "" {
		p := s.defaultAIProvider()
		model := p.DefaultModel
		if model == "" && len(p.Models) > 0 {
			model = p.Models[0]
		}
		if model == "" || !modelAllowed(model, p.Models) {
			return config.AIProviderConfig{}, "", errUnknownModel{}
		}
		return p, model, nil
	}
	for _, p := range providers {
		if stripped, ok := stripProviderPrefix(requested, p.Name); ok {
			if modelAllowed(stripped, p.Models) {
				return p, stripped, nil
			}
			if modelAllowed(requested, p.Models) {
				return p, requested, nil
			}
			return config.AIProviderConfig{}, "", errUnknownModel{model: requested}
		}
	}
	for _, p := range providers {
		if modelAllowed(requested, p.Models) {
			return p, requested, nil
		}
	}
	p := s.defaultAIProvider()
	if modelAllowed(requested, p.Models) {
		return p, requested, nil
	}
	return config.AIProviderConfig{}, "", errUnknownModel{model: requested}
}

func (s *Server) defaultAIProvider() config.AIProviderConfig {
	want := strings.TrimSpace(s.Config.AI.DefaultProvider)
	for _, p := range s.Config.AI.Providers {
		if p.Name == want {
			return p
		}
	}
	return s.Config.AI.Providers[0]
}

func stripProviderPrefix(model, providerName string) (string, bool) {
	providerName = strings.TrimSpace(providerName)
	if providerName == "" {
		return "", false
	}
	for _, sep := range []string{":", "/"} {
		prefix := providerName + sep
		if strings.HasPrefix(model, prefix) {
			return strings.TrimPrefix(model, prefix), true
		}
	}
	return "", false
}

func modelAllowed(model string, allowlist []string) bool {
	for _, allowed := range allowlist {
		if model == allowed {
			return true
		}
	}
	return false
}

func (s *Server) providerChat(ctx context.Context, provider config.AIProviderConfig, model string, messages []aiMessage) (aiChatResponse, error) {
	switch provider.Type {
	case "openai":
		return s.openAIChat(ctx, provider, model, messages)
	case "anthropic":
		return s.anthropicChat(ctx, provider, model, messages)
	default:
		return aiChatResponse{}, fmt.Errorf("unsupported provider type %q", provider.Type)
	}
}

func (s *Server) openAIChat(ctx context.Context, provider config.AIProviderConfig, model string, messages []aiMessage) (aiChatResponse, error) {
	payload := map[string]any{"model": model, "messages": messages, "stream": false}
	data, err := providerJSON(ctx, http.MethodPost, provider.BaseURL+"/chat/completions", providerHeaders(provider), payload, 30*time.Second, providerChatResponseLimit)
	if err != nil {
		return aiChatResponse{}, err
	}
	var raw struct {
		ID      string `json:"id"`
		Model   string `json:"model"`
		Choices []struct {
			Message aiMessage `json:"message"`
			Text    string    `json:"text"`
		} `json:"choices"`
		Usage struct {
			PromptTokens     int `json:"prompt_tokens"`
			CompletionTokens int `json:"completion_tokens"`
		} `json:"usage"`
	}
	if err := json.Unmarshal(data, &raw); err != nil {
		return aiChatResponse{}, err
	}
	msg := aiMessage{Role: "assistant"}
	if len(raw.Choices) > 0 {
		msg = raw.Choices[0].Message
		if msg.Content == "" {
			msg.Content = raw.Choices[0].Text
		}
		if msg.Role == "" {
			msg.Role = "assistant"
		}
	}
	if raw.ID == "" {
		raw.ID, _ = randomID()
	}
	if raw.Model == "" {
		raw.Model = model
	}
	return aiChatResponse{ID: raw.ID, Model: raw.Model, Message: msg, Usage: aiUsage{PromptTokens: raw.Usage.PromptTokens, CompletionTokens: raw.Usage.CompletionTokens}}, nil
}

func (s *Server) anthropicChat(ctx context.Context, provider config.AIProviderConfig, model string, messages []aiMessage) (aiChatResponse, error) {
	payload := anthropicPayload(model, messages, false)
	data, err := providerJSON(ctx, http.MethodPost, anthropicMessagesURL(provider.BaseURL), providerHeaders(provider), payload, 30*time.Second, providerChatResponseLimit)
	if err != nil {
		return aiChatResponse{}, err
	}
	var raw struct {
		ID      string          `json:"id"`
		Model   string          `json:"model"`
		Role    string          `json:"role"`
		Content json.RawMessage `json:"content"`
		Usage   struct {
			InputTokens  int `json:"input_tokens"`
			OutputTokens int `json:"output_tokens"`
		} `json:"usage"`
	}
	if err := json.Unmarshal(data, &raw); err != nil {
		return aiChatResponse{}, err
	}
	role := raw.Role
	if role == "" {
		role = "assistant"
	}
	if raw.ID == "" {
		raw.ID, _ = randomID()
	}
	if raw.Model == "" {
		raw.Model = model
	}
	return aiChatResponse{ID: raw.ID, Model: raw.Model, Message: aiMessage{Role: role, Content: anthropicContentText(raw.Content)}, Usage: aiUsage{PromptTokens: raw.Usage.InputTokens, CompletionTokens: raw.Usage.OutputTokens}}, nil
}

func anthropicContentText(raw json.RawMessage) string {
	var s string
	if json.Unmarshal(raw, &s) == nil {
		return s
	}
	var parts []struct {
		Type string `json:"type"`
		Text string `json:"text"`
	}
	if json.Unmarshal(raw, &parts) == nil {
		var b strings.Builder
		for _, p := range parts {
			b.WriteString(p.Text)
		}
		return b.String()
	}
	return ""
}

func anthropicPayload(model string, messages []aiMessage, stream bool) map[string]any {
	out := make([]map[string]string, 0, len(messages))
	var system []string
	for _, msg := range messages {
		role := strings.TrimSpace(msg.Role)
		if role == "system" {
			system = append(system, msg.Content)
			continue
		}
		if role != "assistant" {
			role = "user"
		}
		out = append(out, map[string]string{"role": role, "content": msg.Content})
	}
	if len(out) == 0 {
		out = append(out, map[string]string{"role": "user", "content": ""})
	}
	payload := map[string]any{"model": model, "messages": out, "max_tokens": 1024, "stream": stream}
	if len(system) > 0 {
		payload["system"] = strings.Join(system, "\n")
	}
	return payload
}

func providerHeaders(provider config.AIProviderConfig) http.Header {
	h := http.Header{}
	h.Set("Content-Type", "application/json")
	switch provider.Type {
	case "openai":
		h.Set("Authorization", "Bearer "+provider.APIKey)
	case "anthropic":
		h.Set("x-api-key", provider.APIKey)
		h.Set("anthropic-version", "2023-06-01")
	}
	return h
}

func providerJSON(ctx context.Context, method, url string, headers http.Header, payload any, timeout time.Duration, limit int64) ([]byte, error) {
	body, err := json.Marshal(payload)
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, method, url, bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	for k, values := range headers {
		for _, v := range values {
			req.Header.Add(k, v)
		}
	}
	client := &http.Client{Timeout: timeout}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	data, err := readAllCapped(resp.Body, limit)
	if err != nil {
		return nil, err
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("provider status %d: %s", resp.StatusCode, strings.TrimSpace(string(data)))
	}
	return data, nil
}

func readAllCapped(r io.Reader, limit int64) ([]byte, error) {
	lr := &io.LimitedReader{R: r, N: limit + 1}
	data, err := io.ReadAll(lr)
	if err != nil {
		return nil, err
	}
	if int64(len(data)) > limit {
		return nil, errors.New("provider response too large")
	}
	return data, nil
}

func anthropicMessagesURL(base string) string {
	if strings.HasSuffix(base, "/v1") {
		return base + "/messages"
	}
	return base + "/v1/messages"
}

func (s *Server) openAIImage(ctx context.Context, provider config.AIProviderConfig, model, prompt, size string) ([]byte, string, error) {
	payload := map[string]any{"model": model, "prompt": prompt, "n": 1, "response_format": "b64_json"}
	if strings.TrimSpace(size) != "" {
		payload["size"] = size
	}
	data, err := providerJSON(ctx, http.MethodPost, provider.BaseURL+"/images/generations", providerHeaders(provider), payload, 120*time.Second, providerImageResponseLimit)
	if err != nil {
		return nil, "", err
	}
	var raw struct {
		Data []struct {
			B64JSON string `json:"b64_json"`
			URL     string `json:"url"`
		} `json:"data"`
	}
	if err := json.Unmarshal(data, &raw); err != nil {
		return nil, "", err
	}
	if len(raw.Data) == 0 {
		return nil, "", errors.New("provider returned no image")
	}
	if raw.Data[0].B64JSON != "" {
		decoded, err := base64.StdEncoding.DecodeString(raw.Data[0].B64JSON)
		if err != nil {
			return nil, "", err
		}
		return decoded, http.DetectContentType(decoded), nil
	}
	if raw.Data[0].URL == "" {
		return nil, "", errors.New("provider returned no image bytes")
	}
	return fetchImageURL(ctx, raw.Data[0].URL, s.Config.MaxUploadBytes)
}

func fetchImageURL(ctx context.Context, imageURL string, maxBytes int64) ([]byte, string, error) {
	ctx, cancel := context.WithTimeout(ctx, 120*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, imageURL, nil)
	if err != nil {
		return nil, "", err
	}
	client := &http.Client{Timeout: 120 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, "", fmt.Errorf("image fetch status %d", resp.StatusCode)
	}
	if maxBytes <= 0 {
		maxBytes = 100 << 20
	}
	data, err := readAllCapped(resp.Body, maxBytes)
	if err != nil {
		return nil, "", err
	}
	contentType := resp.Header.Get("Content-Type")
	media, _, _ := mime.ParseMediaType(contentType)
	if media == "" {
		contentType = http.DetectContentType(data)
	}
	return data, contentType, nil
}

func (s *Server) streamAIChat(w http.ResponseWriter, r *http.Request, site string, id *identity.Identity, provider config.AIProviderConfig, model string, messages []aiMessage) {
	resp, err := s.providerChatStream(r.Context(), provider, model, messages)
	if err != nil {
		http.Error(w, err.Error(), http.StatusServiceUnavailable)
		return
	}
	defer resp.Body.Close()
	w.Header().Set("Content-Type", "text/event-stream; charset=utf-8")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.WriteHeader(http.StatusOK)
	flusher, _ := w.(http.Flusher)
	usage := aiUsage{}
	scanProviderSSE(resp.Body, provider.Type, &usage, func(delta string) bool {
		if delta == "" {
			return true
		}
		payload, _ := json.Marshal(map[string]string{"delta": delta})
		_, _ = fmt.Fprintf(w, "data: %s\n\n", payload)
		if flusher != nil {
			flusher.Flush()
		}
		return true
	})
	_, _ = io.WriteString(w, "data: [DONE]\n\n")
	if flusher != nil {
		flusher.Flush()
	}
	if s.Store != nil {
		_ = s.Store.RecordAIAudit(r.Context(), site, identity.SubjectKey(id), provider.Name, model, usage.PromptTokens, usage.CompletionTokens)
	}
}

func (s *Server) providerChatStream(ctx context.Context, provider config.AIProviderConfig, model string, messages []aiMessage) (*http.Response, error) {
	var payload any
	var endpoint string
	switch provider.Type {
	case "openai":
		payload = map[string]any{"model": model, "messages": messages, "stream": true, "stream_options": map[string]bool{"include_usage": true}}
		endpoint = provider.BaseURL + "/chat/completions"
	case "anthropic":
		payload = anthropicPayload(model, messages, true)
		endpoint = anthropicMessagesURL(provider.BaseURL)
	default:
		return nil, fmt.Errorf("unsupported provider type %q", provider.Type)
	}
	body, err := json.Marshal(payload)
	if err != nil {
		return nil, err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, endpoint, bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	for k, values := range providerHeaders(provider) {
		for _, v := range values {
			req.Header.Add(k, v)
		}
	}
	req.Header.Set("Accept", "text/event-stream")
	client := &http.Client{Timeout: 30 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		data, _ := readAllCapped(resp.Body, providerChatResponseLimit)
		resp.Body.Close()
		return nil, fmt.Errorf("provider status %d: %s", resp.StatusCode, strings.TrimSpace(string(data)))
	}
	return resp, nil
}

func scanProviderSSE(body io.Reader, providerType string, usage *aiUsage, onDelta func(string) bool) {
	limited := &io.LimitedReader{R: body, N: providerChatResponseLimit + 1}
	scanner := bufio.NewScanner(limited)
	scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
	var lines []string
	process := func() bool {
		if len(lines) == 0 {
			return false
		}
		delta, done := providerSSEEvent(providerType, lines, usage)
		lines = nil
		if delta != "" && !onDelta(delta) {
			return true
		}
		return done
	}
	for scanner.Scan() {
		line := strings.TrimRight(scanner.Text(), "\r")
		if line == "" {
			if process() {
				return
			}
			continue
		}
		lines = append(lines, line)
	}
	_ = process()
}

func providerSSEEvent(providerType string, lines []string, usage *aiUsage) (string, bool) {
	var eventName string
	var dataParts []string
	for _, line := range lines {
		if strings.HasPrefix(line, ":") {
			continue
		}
		if strings.HasPrefix(line, "event:") {
			eventName = strings.TrimSpace(strings.TrimPrefix(line, "event:"))
			continue
		}
		if strings.HasPrefix(line, "data:") {
			v := strings.TrimPrefix(line, "data:")
			if strings.HasPrefix(v, " ") {
				v = v[1:]
			}
			dataParts = append(dataParts, v)
		}
	}
	data := strings.Join(dataParts, "\n")
	trimmed := strings.TrimSpace(data)
	if trimmed == "" {
		return "", eventName == "message_stop"
	}
	if trimmed == "[DONE]" || strings.EqualFold(trimmed, "done") {
		return "", true
	}
	var payload any
	dec := json.NewDecoder(strings.NewReader(data))
	dec.UseNumber()
	if err := dec.Decode(&payload); err != nil {
		return data, false
	}
	if m, ok := payload.(map[string]any); ok {
		mergeUsage(usage, m)
		if typ, _ := m["type"].(string); typ == "message_stop" {
			return "", true
		}
	}
	return extractProviderDelta(providerType, payload), eventName == "message_stop"
}

func mergeUsage(usage *aiUsage, payload map[string]any) {
	if usage == nil {
		return
	}
	if u, ok := payload["usage"].(map[string]any); ok {
		if v := intField(u, "prompt_tokens", "input_tokens"); v > 0 {
			usage.PromptTokens = v
		}
		if v := intField(u, "completion_tokens", "output_tokens"); v > 0 {
			usage.CompletionTokens = v
		}
	}
}

func intField(m map[string]any, names ...string) int {
	for _, name := range names {
		switch v := m[name].(type) {
		case json.Number:
			i, _ := v.Int64()
			return int(i)
		case float64:
			return int(v)
		case int:
			return v
		}
	}
	return 0
}

func extractProviderDelta(providerType string, payload any) string {
	if s, ok := payload.(string); ok {
		return s
	}
	m, ok := payload.(map[string]any)
	if !ok {
		return ""
	}
	if providerType == "openai" {
		if choices, ok := m["choices"].([]any); ok && len(choices) > 0 {
			if choice, ok := choices[0].(map[string]any); ok {
				if delta := textFromDelta(choice["delta"]); delta != "" {
					return delta
				}
				if text, _ := choice["text"].(string); text != "" {
					return text
				}
				if msg := textFromDelta(choice["message"]); msg != "" {
					return msg
				}
			}
		}
	}
	if providerType == "anthropic" {
		if delta, ok := m["delta"].(map[string]any); ok {
			if text, _ := delta["text"].(string); text != "" {
				return text
			}
		}
		if block, ok := m["content_block"].(map[string]any); ok {
			if text, _ := block["text"].(string); text != "" {
				return text
			}
		}
		if completion, _ := m["completion"].(string); completion != "" {
			return completion
		}
	}
	if delta := textFromDelta(m["delta"]); delta != "" {
		return delta
	}
	if text, _ := m["text"].(string); text != "" {
		return text
	}
	return ""
}

func textFromDelta(v any) string {
	switch x := v.(type) {
	case string:
		return x
	case map[string]any:
		if content, _ := x["content"].(string); content != "" {
			return content
		}
		if text, _ := x["text"].(string); text != "" {
			return text
		}
	}
	return ""
}
