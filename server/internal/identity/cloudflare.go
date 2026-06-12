package identity

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/go-jose/go-jose/v4"
	"github.com/go-jose/go-jose/v4/jwt"
)

type CloudflareAccessAdapter struct {
	TeamDomain           string
	Audience             string
	JWKSURL              string
	EmailDomainAllowlist []string
	HTTPClient           *http.Client
	Now                  func() time.Time

	mu        sync.Mutex
	jwks      *jose.JSONWebKeySet
	jwksUntil time.Time
}

func (a *CloudflareAccessAdapter) Name() string { return "cloudflare-access" }

func (a *CloudflareAccessAdapter) Authenticate(ctx context.Context, r *http.Request) (*Identity, error) {
	if strings.TrimSpace(a.TeamDomain) == "" || strings.TrimSpace(a.Audience) == "" || strings.TrimSpace(a.JWKSURL) == "" {
		return nil, fmt.Errorf("%w: cloudflare team_domain, audience, and jwks_url are required", ErrMisconfiguredAdapter)
	}
	raw := strings.TrimSpace(r.Header.Get("Cf-Access-Jwt-Assertion"))
	if raw == "" {
		return nil, ErrMissingCredential
	}
	parsed, err := jwt.ParseSigned(raw, []jose.SignatureAlgorithm{jose.RS256, jose.RS384, jose.RS512, jose.ES256, jose.ES384, jose.ES512})
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrInvalidCredential, err)
	}
	kid, alg := tokenKeyAndAlg(parsed)
	if kid == "" || alg == "" {
		return nil, fmt.Errorf("%w: missing kid or alg", ErrInvalidCredential)
	}
	key, err := a.lookupKey(ctx, kid, false)
	if err != nil {
		return nil, err
	}
	if key == nil {
		key, err = a.lookupKey(ctx, kid, true)
		if err != nil {
			return nil, err
		}
	}
	if key == nil {
		return nil, fmt.Errorf("%w: unknown kid", ErrInvalidCredential)
	}
	if key.Algorithm != "" && key.Algorithm != alg {
		return nil, fmt.Errorf("%w: jwk alg mismatch", ErrInvalidCredential)
	}
	var claims jwt.Claims
	var extra accessClaims
	var rawClaims map[string]any
	if err := parsed.Claims(key.Key, &claims, &extra, &rawClaims); err != nil {
		return nil, fmt.Errorf("%w: %v", ErrInvalidCredential, err)
	}
	if err := a.validateClaims(claims); err != nil {
		return nil, err
	}
	if err := a.validateEmail(extra.Email); err != nil {
		return nil, err
	}
	providerSubject := claims.Subject
	if providerSubject == "" {
		providerSubject = extra.Email
	}
	if providerSubject == "" {
		return nil, fmt.Errorf("%w: missing sub", ErrInvalidCredential)
	}
	login := extra.Email
	if login == "" {
		login = providerSubject
	}
	return &Identity{
		Authenticated: true,
		Provider:      "cloudflare",
		Subject:       "cloudflare:" + providerSubject,
		Email:         extra.Email,
		Login:         login,
		Name:          extra.Name,
		Groups:        extra.Groups,
		Capabilities:  accessClaimCapabilities(rawClaims),
		Raw: map[string]string{
			"kid": kid,
			"iss": claims.Issuer,
		},
	}, nil
}

type accessClaims struct {
	Email  string   `json:"email"`
	Name   string   `json:"name"`
	Groups []string `json:"groups"`
}

var cloudflareKnownClaims = map[string]bool{
	"iss":    true,
	"sub":    true,
	"aud":    true,
	"exp":    true,
	"nbf":    true,
	"iat":    true,
	"jti":    true,
	"email":  true,
	"name":   true,
	"groups": true,
}

func accessClaimCapabilities(raw map[string]any) map[string]any {
	if len(raw) == 0 {
		return nil
	}
	keys := make([]string, 0, len(raw))
	for k, v := range raw {
		if k == "" || cloudflareKnownClaims[k] {
			continue
		}
		if _, ok := v.(string); ok {
			keys = append(keys, k)
		}
	}
	if len(keys) == 0 {
		return nil
	}
	sort.Strings(keys)
	if len(keys) > 16 {
		keys = keys[:16]
	}
	caps := make(map[string]any, len(keys))
	for _, k := range keys {
		caps[k] = raw[k]
	}
	return caps
}

func tokenKeyAndAlg(tok *jwt.JSONWebToken) (kid, alg string) {
	for _, h := range tok.Headers {
		if h.KeyID != "" {
			kid = h.KeyID
		}
		if h.Algorithm != "" {
			alg = string(h.Algorithm)
		}
	}
	return kid, alg
}

func (a *CloudflareAccessAdapter) lookupKey(ctx context.Context, kid string, force bool) (*jose.JSONWebKey, error) {
	if err := a.ensureJWKS(ctx, force); err != nil {
		return nil, err
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.jwks == nil {
		return nil, nil
	}
	for _, k := range a.jwks.Keys {
		if k.KeyID == kid {
			kk := k
			return &kk, nil
		}
	}
	return nil, nil
}

func (a *CloudflareAccessAdapter) ensureJWKS(ctx context.Context, force bool) error {
	now := a.now()
	a.mu.Lock()
	fresh := a.jwks != nil && now.Before(a.jwksUntil)
	a.mu.Unlock()
	if fresh && !force {
		return nil
	}
	client := a.HTTPClient
	if client == nil {
		client = &http.Client{Timeout: 10 * time.Second}
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, a.JWKSURL, nil)
	if err != nil {
		return err
	}
	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("%w: %v", ErrProviderUnavailable, err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("%w: jwks status %d", ErrProviderUnavailable, resp.StatusCode)
	}
	var set jose.JSONWebKeySet
	if err := json.NewDecoder(resp.Body).Decode(&set); err != nil {
		return fmt.Errorf("%w: %v", ErrProviderUnavailable, err)
	}
	a.mu.Lock()
	a.jwks = &set
	a.jwksUntil = now.Add(10 * time.Minute)
	a.mu.Unlock()
	return nil
}

func (a *CloudflareAccessAdapter) validateClaims(c jwt.Claims) error {
	now := a.now()
	issuer := strings.TrimRight(a.TeamDomain, "/")
	if c.Issuer != issuer && c.Issuer != issuer+"/" {
		return fmt.Errorf("%w: invalid issuer", ErrInvalidCredential)
	}
	if !audienceContains(c.Audience, a.Audience) {
		return fmt.Errorf("%w: invalid audience", ErrInvalidCredential)
	}
	if c.Expiry == nil || !now.Before(c.Expiry.Time()) {
		return fmt.Errorf("%w: token expired", ErrInvalidCredential)
	}
	if c.NotBefore != nil && now.Before(c.NotBefore.Time()) {
		return fmt.Errorf("%w: token not yet valid", ErrInvalidCredential)
	}
	return nil
}

func (a *CloudflareAccessAdapter) validateEmail(email string) error {
	if len(a.EmailDomainAllowlist) == 0 {
		return nil
	}
	at := strings.LastIndexByte(email, '@')
	if at < 0 {
		return fmt.Errorf("%w: email domain not allowed", ErrInvalidCredential)
	}
	domain := strings.ToLower(email[at+1:])
	for _, allowed := range a.EmailDomainAllowlist {
		if strings.ToLower(strings.TrimSpace(allowed)) == domain {
			return nil
		}
	}
	return fmt.Errorf("%w: email domain not allowed", ErrInvalidCredential)
}

func (a *CloudflareAccessAdapter) now() time.Time {
	if a.Now != nil {
		return a.Now()
	}
	return time.Now()
}

func audienceContains(aud jwt.Audience, want string) bool {
	for _, a := range aud {
		if a == want {
			return true
		}
	}
	return false
}
