package identity

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"sync/atomic"
	"testing"
	"time"

	"github.com/go-jose/go-jose/v4"
	"github.com/go-jose/go-jose/v4/jwt"
)

func req(remote string) *http.Request {
	r := httptest.NewRequest(http.MethodGet, "http://demo.localhost/_quick/identity", nil)
	r.RemoteAddr = remote
	return r
}

func TestStatusForError(t *testing.T) {
	t.Parallel()
	tests := []struct {
		err  error
		code int
	}{
		{ErrMissingCredential, http.StatusUnauthorized},
		{ErrInvalidCredential, http.StatusForbidden},
		{ErrProviderUnavailable, http.StatusServiceUnavailable},
		{ErrAnonymousNotAllowed, http.StatusUnauthorized},
		{ErrMisconfiguredAdapter, http.StatusInternalServerError},
	}
	for _, tt := range tests {
		if got := StatusForError(tt.err); got != tt.code {
			t.Fatalf("StatusForError(%v)=%d want %d", tt.err, got, tt.code)
		}
	}
}

func TestDevAndNoneLoopbackEnforcement(t *testing.T) {
	dev := DevAdapter{Email: "sam@example.com"}
	id, err := dev.Authenticate(context.Background(), req("127.0.0.1:1"))
	if err != nil || !id.Authenticated || id.Provider != "dev" || id.Subject != "dev:sam@example.com" {
		t.Fatalf("dev id=%+v err=%v", id, err)
	}
	if _, err := dev.Authenticate(context.Background(), req("203.0.113.1:1")); !errors.Is(err, ErrAnonymousNotAllowed) {
		t.Fatalf("non-loopback dev err=%v", err)
	}
	anon, err := (NoneAdapter{}).Authenticate(context.Background(), req("127.0.0.1:1"))
	if err != nil || anon.Authenticated || anon.Provider != "anonymous" {
		t.Fatalf("anon=%+v err=%v", anon, err)
	}
}

func TestTailscaleServeAdapter(t *testing.T) {
	r := req("127.0.0.1:1")
	r.Header.Set("Tailscale-User-Login", "sam@example.com")
	r.Header.Set("Tailscale-User-Name", "Sam")
	r.Header.Set("Tailscale-User-Profile-Pic", "https://pic")
	id, err := (TailscaleServeAdapter{}).Authenticate(context.Background(), r)
	if err != nil || id.Provider != "tailscale" || id.Login != "sam@example.com" || id.Name != "Sam" || id.AvatarURL == "" {
		t.Fatalf("id=%+v err=%v", id, err)
	}
	r.RemoteAddr = "203.0.113.1:1"
	if _, err := (TailscaleServeAdapter{}).Authenticate(context.Background(), r); !errors.Is(err, ErrInvalidCredential) {
		t.Fatalf("non-loopback err=%v", err)
	}
}

type fakeWhoIs struct{ got string }

func (f *fakeWhoIs) WhoIs(ctx context.Context, addr string) (*WhoIs, error) {
	f.got = addr
	w := &WhoIs{}
	w.UserProfile.LoginName = "sam@example.com"
	w.UserProfile.DisplayName = "Sam"
	w.UserProfile.ProfilePicURL = "https://pic"
	w.Node.Name = "device.tailnet.ts.net."
	return w, nil
}

func TestTailscaleLocalAPIAdapter(t *testing.T) {
	client := &fakeWhoIs{}
	adapter := TailscaleLocalAPIAdapter{
		TrustedProxies: []netip.Prefix{netip.MustParsePrefix("127.0.0.1/32")},
		SourceIPHeader: "X-Forwarded-For",
		Client:         client,
	}
	r := req("127.0.0.1:1234")
	r.Header.Set("X-Forwarded-For", "100.64.0.10")
	id, err := adapter.Authenticate(context.Background(), r)
	if err != nil || id.Device != "device.tailnet.ts.net" || id.Login != "sam@example.com" {
		t.Fatalf("id=%+v err=%v", id, err)
	}
	if client.got == "" {
		t.Fatalf("WhoIs was not called")
	}
	r.RemoteAddr = "203.0.113.1:1234"
	if _, err := adapter.Authenticate(context.Background(), r); !errors.Is(err, ErrInvalidCredential) {
		t.Fatalf("untrusted proxy err=%v", err)
	}
}

func TestStripQuickHeaders(t *testing.T) {
	next := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("X-Quick-Identity") != "" {
			t.Fatalf("X-Quick header was not stripped")
		}
		w.WriteHeader(http.StatusNoContent)
	})
	r := req("127.0.0.1:1")
	r.Header.Set("X-Quick-Identity", "spoof")
	rr := httptest.NewRecorder()
	StripQuickHeaders(next).ServeHTTP(rr, r)
	if rr.Code != http.StatusNoContent {
		t.Fatalf("code=%d", rr.Code)
	}
}

func TestCloudflareAccessAdapterJWTValidation(t *testing.T) {
	now := time.Date(2026, 6, 12, 0, 0, 0, 0, time.UTC)
	key1 := mustRSA(t)
	key2 := mustRSA(t)
	var kid atomic.Value
	kid.Store("kid1")
	jwks := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		active := kid.Load().(string)
		pub := &key1.PublicKey
		if active == "kid2" {
			pub = &key2.PublicKey
		}
		_ = json.NewEncoder(w).Encode(jose.JSONWebKeySet{Keys: []jose.JSONWebKey{{Key: pub, KeyID: active, Algorithm: string(jose.RS256), Use: "sig"}}})
	}))
	defer jwks.Close()
	adapter := &CloudflareAccessAdapter{
		TeamDomain:           "https://team.cloudflareaccess.com",
		Audience:             "aud1",
		JWKSURL:              jwks.URL,
		EmailDomainAllowlist: []string{"example.com"},
		Now:                  func() time.Time { return now },
	}

	tok1 := signAccessToken(t, key1, "kid1", now, "aud1", now.Add(time.Hour))
	r := req("127.0.0.1:1")
	r.Header.Set("Cf-Access-Jwt-Assertion", tok1)
	id, err := adapter.Authenticate(context.Background(), r)
	if err != nil || id.Provider != "cloudflare" || id.Email != "sam@example.com" || id.Subject != "cloudflare:user-1" {
		t.Fatalf("cloudflare id=%+v err=%v", id, err)
	}

	kid.Store("kid2")
	tok2 := signAccessToken(t, key2, "kid2", now, "aud1", now.Add(time.Hour))
	r.Header.Set("Cf-Access-Jwt-Assertion", tok2)
	if _, err := adapter.Authenticate(context.Background(), r); err != nil {
		t.Fatalf("unknown kid refresh failed: %v", err)
	}

	expired := signAccessToken(t, key2, "kid2", now, "aud1", now.Add(-time.Minute))
	r.Header.Set("Cf-Access-Jwt-Assertion", expired)
	if _, err := adapter.Authenticate(context.Background(), r); !errors.Is(err, ErrInvalidCredential) {
		t.Fatalf("expired err=%v", err)
	}
	badAud := signAccessToken(t, key2, "kid2", now, "wrong", now.Add(time.Hour))
	r.Header.Set("Cf-Access-Jwt-Assertion", badAud)
	if _, err := adapter.Authenticate(context.Background(), r); !errors.Is(err, ErrInvalidCredential) {
		t.Fatalf("bad audience err=%v", err)
	}
}

func mustRSA(t *testing.T) *rsa.PrivateKey {
	t.Helper()
	k, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	return k
}

func signAccessToken(t *testing.T, key *rsa.PrivateKey, kid string, now time.Time, aud string, exp time.Time) string {
	t.Helper()
	opts := (&jose.SignerOptions{}).WithType("JWT").WithHeader("kid", kid)
	signer, err := jose.NewSigner(jose.SigningKey{Algorithm: jose.RS256, Key: key}, opts)
	if err != nil {
		t.Fatal(err)
	}
	tok, err := jwt.Signed(signer).
		Claims(jwt.Claims{Issuer: "https://team.cloudflareaccess.com", Subject: "user-1", Audience: jwt.Audience{aud}, Expiry: jwt.NewNumericDate(exp), NotBefore: jwt.NewNumericDate(now.Add(-time.Minute))}).
		Claims(map[string]any{"email": "sam@example.com", "name": "Sam", "groups": []string{"engineering"}}).
		Serialize()
	if err != nil {
		t.Fatal(err)
	}
	return tok
}
