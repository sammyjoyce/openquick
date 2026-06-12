package api_test

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"openquick.dev/quickd/internal/api"
	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
	"openquick.dev/quickd/internal/sites"
	"openquick.dev/quickd/internal/static"
	"openquick.dev/quickd/internal/store"
)

func newConfiguredApp(t *testing.T, mutate func(*config.Config), adapter identity.Adapter) appFixture {
	t.Helper()
	root := t.TempDir()
	cfg := config.Default(root)
	cfg.Viewer.AllowAnonymous = true
	if mutate != nil {
		mutate(&cfg)
	}
	cfg.ApplyDefaults()
	st, err := store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { st.Close() })
	for _, name := range []string{"a", "b"} {
		siteDir := filepath.Join(root, "sites", name)
		rel := filepath.Join(siteDir, "releases", "20260611T000000Z-abcdef")
		if err := os.MkdirAll(rel, 0o770); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(filepath.Join(rel, "index.html"), []byte(name), 0o660); err != nil {
			t.Fatal(err)
		}
		if err := sites.WriteSiteConfig(siteDir, sites.SiteConfig{Name: name}); err != nil {
			t.Fatal(err)
		}
		if err := os.Symlink(filepath.Join("releases", "20260611T000000Z-abcdef"), filepath.Join(siteDir, "current")); err != nil {
			t.Fatal(err)
		}
		if _, err := st.EnsureSite(context.Background(), name, name); err != nil {
			t.Fatal(err)
		}
	}
	if adapter == nil {
		adapter = identity.DevAdapter{Email: "sam@example.com", AllowPublicUnsafe: true}
	}
	apiHandler := api.New(cfg, st)
	h := static.New(cfg, st, adapter, apiHandler)
	return appFixture{h: h, api: apiHandler, cfg: cfg, st: st}
}

func fakeOpenAI(t *testing.T) *httptest.Server {
	t.Helper()
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if got := r.Header.Get("Authorization"); got != "Bearer test-key" {
			t.Errorf("Authorization=%q", got)
		}
		switch r.URL.Path {
		case "/chat/completions":
			var body struct {
				Model  string `json:"model"`
				Stream bool   `json:"stream"`
			}
			_ = json.NewDecoder(r.Body).Decode(&body)
			if body.Stream {
				w.Header().Set("Content-Type", "text/event-stream")
				_, _ = w.Write([]byte("data: {\"choices\":[{\"delta\":{\"content\":\"hel\"}}]}\n\n"))
				_, _ = w.Write([]byte("data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}],\"usage\":{\"prompt_tokens\":3,\"completion_tokens\":2}}\n\n"))
				_, _ = w.Write([]byte("data: [DONE]\n\n"))
				return
			}
			writeJSON(w, map[string]any{"id": "chat-1", "model": body.Model, "choices": []any{map[string]any{"message": map[string]any{"role": "assistant", "content": "pong"}}}, "usage": map[string]any{"prompt_tokens": 4, "completion_tokens": 5}})
		case "/images/generations":
			writeJSON(w, map[string]any{"data": []any{map[string]any{"b64_json": base64.StdEncoding.EncodeToString([]byte("fake image bytes"))}}})
		default:
			http.NotFound(w, r)
		}
	}))
}

func fakeAnthropic(t *testing.T) *httptest.Server {
	t.Helper()
	return httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if got := r.Header.Get("x-api-key"); got != "test-key" {
			t.Errorf("x-api-key=%q", got)
		}
		if r.URL.Path != "/v1/messages" {
			http.NotFound(w, r)
			return
		}
		var body struct {
			Model  string `json:"model"`
			Stream bool   `json:"stream"`
		}
		_ = json.NewDecoder(r.Body).Decode(&body)
		if body.Stream {
			w.Header().Set("Content-Type", "text/event-stream")
			_, _ = w.Write([]byte("event: message_start\ndata: {\"type\":\"message_start\",\"usage\":{\"input_tokens\":7}}\n\n"))
			_, _ = w.Write([]byte("event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"hi\"}}\n\n"))
			_, _ = w.Write([]byte("event: message_delta\ndata: {\"type\":\"message_delta\",\"usage\":{\"output_tokens\":1}}\n\n"))
			_, _ = w.Write([]byte("event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n"))
			return
		}
		writeJSON(w, map[string]any{"id": "msg-1", "model": body.Model, "role": "assistant", "content": []any{map[string]any{"type": "text", "text": "anthropic pong"}}, "usage": map[string]any{"input_tokens": 2, "output_tokens": 3}})
	}))
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}

func TestAIChatImageLimitsAndAudit(t *testing.T) {
	t.Setenv("OPENAI_KEY", "test-key")
	provider := fakeOpenAI(t)
	defer provider.Close()
	app := newConfiguredApp(t, func(cfg *config.Config) {
		cfg.AI.Enabled = true
		cfg.AI.Providers = []config.AIProviderConfig{{Name: "openai", Type: "openai", BaseURL: provider.URL, APIKeyEnv: "OPENAI_KEY", Models: []string{"gpt-test", "img-test"}, DefaultModel: "gpt-test"}}
		cfg.AI.Limits.RequestsPerMinutePerIdentity = 10
		cfg.AI.Limits.RequestsPerDayPerSite = 10
	}, nil)

	rr := doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/capabilities", nil)
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `"ai":true`) {
		t.Fatalf("capabilities status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}],"model":"gpt-test"}`))
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `"content":"pong"`) || !strings.Contains(rr.Body.String(), `"prompt_tokens":4`) {
		t.Fatalf("chat status=%d body=%s", rr.Code, rr.Body.String())
	}
	count, err := app.st.AIAuditCount(context.Background(), "a")
	if err != nil || count != 1 {
		t.Fatalf("audit count=%d err=%v", count, err)
	}
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/images", []byte(`{"prompt":"draw","model":"img-test"}`))
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `"url":"/_quick/uploads/`) {
		t.Fatalf("image status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}],"model":"bad"}`))
	if rr.Code != http.StatusBadRequest {
		t.Fatalf("unknown model status=%d body=%s", rr.Code, rr.Body.String())
	}
}

func TestAIStreamingAnthropicAndAnonymousRateLimits(t *testing.T) {
	t.Setenv("OPENAI_KEY", "test-key")
	t.Setenv("ANTHROPIC_KEY", "test-key")
	openai := fakeOpenAI(t)
	defer openai.Close()
	anthropic := fakeAnthropic(t)
	defer anthropic.Close()
	app := newConfiguredApp(t, func(cfg *config.Config) {
		cfg.AI.Enabled = true
		cfg.AI.Providers = []config.AIProviderConfig{
			{Name: "openai", Type: "openai", BaseURL: openai.URL, APIKeyEnv: "OPENAI_KEY", Models: []string{"gpt-test"}, DefaultModel: "gpt-test"},
			{Name: "anthropic", Type: "anthropic", BaseURL: anthropic.URL, APIKeyEnv: "ANTHROPIC_KEY", Models: []string{"claude-test"}, DefaultModel: "claude-test"},
		}
		cfg.AI.DefaultProvider = "openai"
		cfg.AI.Limits.RequestsPerMinutePerIdentity = 1
		cfg.AI.Limits.RequestsPerDayPerSite = 2
	}, nil)

	rr := doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}],"model":"gpt-test","stream":true}`))
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `data: {"delta":"hel"}`) || !strings.Contains(rr.Body.String(), "data: [DONE]") {
		t.Fatalf("openai stream status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}],"model":"claude-test","stream":true}`))
	if rr.Code != http.StatusTooManyRequests {
		t.Fatalf("rpm status=%d body=%s", rr.Code, rr.Body.String())
	}

	app = newConfiguredApp(t, func(cfg *config.Config) {
		cfg.AI.Enabled = true
		cfg.AI.Providers = []config.AIProviderConfig{{Name: "anthropic", Type: "anthropic", BaseURL: anthropic.URL, APIKeyEnv: "ANTHROPIC_KEY", Models: []string{"claude-test"}, DefaultModel: "claude-test"}}
		cfg.AI.Limits.RequestsPerMinutePerIdentity = 10
		cfg.AI.Limits.RequestsPerDayPerSite = 10
	}, nil)
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}],"stream":true}`))
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `data: {"delta":"hi"}`) || !strings.Contains(rr.Body.String(), "data: [DONE]") {
		t.Fatalf("anthropic stream status=%d body=%s", rr.Code, rr.Body.String())
	}

	budget := newConfiguredApp(t, func(cfg *config.Config) {
		cfg.AI.Enabled = true
		cfg.AI.Providers = []config.AIProviderConfig{{Name: "openai", Type: "openai", BaseURL: openai.URL, APIKeyEnv: "OPENAI_KEY", Models: []string{"gpt-test"}, DefaultModel: "gpt-test"}}
		cfg.AI.Limits.RequestsPerMinutePerIdentity = 10
		cfg.AI.Limits.RequestsPerDayPerSite = 1
	}, nil)
	rr = doReq(budget.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}]}`))
	if rr.Code != http.StatusOK {
		t.Fatalf("budget first status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(budget.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}]}`))
	if rr.Code != http.StatusTooManyRequests {
		t.Fatalf("daily budget status=%d body=%s", rr.Code, rr.Body.String())
	}

	anon := newConfiguredApp(t, func(cfg *config.Config) {
		cfg.AI.Enabled = true
		cfg.AI.Providers = []config.AIProviderConfig{{Name: "openai", Type: "openai", BaseURL: openai.URL, APIKeyEnv: "OPENAI_KEY", Models: []string{"gpt-test"}, DefaultModel: "gpt-test"}}
	}, identity.NoneAdapter{AllowPublicUnsafe: true})
	rr = doReq(anon.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}]}`))
	if rr.Code != http.StatusForbidden {
		t.Fatalf("anonymous status=%d body=%s", rr.Code, rr.Body.String())
	}
}

func TestAIDisabledByDefault(t *testing.T) {
	app := newConfiguredApp(t, nil, nil)
	rr := doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/capabilities", nil)
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `"ai":false`) {
		t.Fatalf("capabilities status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/ai/chat", []byte(`{"messages":[{"role":"user","content":"ping"}]}`))
	if rr.Code != http.StatusServiceUnavailable {
		t.Fatalf("disabled status=%d body=%s", rr.Code, rr.Body.String())
	}
}

func TestWarehouseQueriesParamsTruncationAndQueryOnly(t *testing.T) {
	app := newConfiguredApp(t, func(cfg *config.Config) {
		cfg.Warehouse.Enabled = true
		cfg.Warehouse.Queries = []config.WarehouseQueryConfig{
			{Name: "echo", SQL: "SELECT ? AS name, ? AS n", Params: []config.WarehouseParamConfig{{Name: "name", Type: "string"}, {Name: "n", Type: "int"}}},
			{Name: "collections", SQL: "SELECT COUNT(*) AS c FROM documents WHERE collection=?", Params: []config.WarehouseParamConfig{{Name: "collection", Type: "string"}}},
			{Name: "limited", SQL: "WITH nums(n) AS (VALUES (1),(2),(3)) SELECT n FROM nums", MaxRows: 2},
			{Name: "sneaky", SQL: "WITH x AS (INSERT INTO documents(site_id, collection, id, data_json, created_at, updated_at) VALUES(1, 'x', 'x', '{}', 'n', 'n') RETURNING id) SELECT id FROM x"},
		}
	}, nil)
	if _, err := app.st.CreateDocument(context.Background(), "a", "posts", "one", `{"title":"one"}`, "test"); err != nil {
		t.Fatal(err)
	}
	rr := doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/warehouse/echo", []byte(`{"name":"sam","n":7}`))
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `"columns":["name","n"]`) || !strings.Contains(rr.Body.String(), `"rows":[["sam",7]]`) {
		t.Fatalf("echo status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/warehouse/collections?collection=posts%27%20OR%201%3D1%20--", nil)
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `"rows":[[0]]`) {
		t.Fatalf("injection status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/warehouse/missing", []byte(`{}`))
	if rr.Code != http.StatusNotFound {
		t.Fatalf("unknown status=%d", rr.Code)
	}
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/warehouse/limited", []byte(`{}`))
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `"row_count":2`) || !strings.Contains(rr.Body.String(), `"truncated":true`) {
		t.Fatalf("limited status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/warehouse/sneaky", []byte(`{}`))
	if rr.Code == http.StatusOK {
		t.Fatalf("sneaky write unexpectedly succeeded: %s", rr.Body.String())
	}
}

func TestWarehouseWriteSQLRejectedAtDecode(t *testing.T) {
	_, err := config.Decode(strings.NewReader(`{"remote_root":"/tmp/q","warehouse":{"enabled":true,"queries":[{"name":"bad","sql":"DELETE FROM documents"}]},"iap":{"type":"none"}}`))
	if err == nil || !strings.Contains(err.Error(), "SELECT or WITH") {
		t.Fatalf("err=%v", err)
	}
	_, err = config.Decode(strings.NewReader(`{"remote_root":"/tmp/q","warehouse":{"enabled":true,"queries":[{"name":"bad","sql":"SELECT 1; DROP TABLE documents"}]},"iap":{"type":"none"}}`))
	if err == nil || !strings.Contains(err.Error(), "semicolons") {
		t.Fatalf("semicolon err=%v", err)
	}
}
