package static

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestDevProxyTokenValidationGatingAndSiteMatch(t *testing.T) {
	h, _ := testStaticHandler(t)
	ctx := context.Background()
	token, _, err := h.Store.MintDevToken(ctx, "demo", "tester", time.Hour)
	if err != nil {
		t.Fatal(err)
	}

	req := httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/_quick/identity", nil)
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("X-Quick-Dev-Token", token)
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK {
		t.Fatalf("disabled token status=%d body=%q", rr.Code, rr.Body.String())
	}
	var disabled map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &disabled); err != nil {
		t.Fatal(err)
	}
	if disabled["provider"] == "dev-proxy" {
		t.Fatalf("dev token was accepted while disabled: %+v", disabled)
	}

	h.Config.DevProxy.Enabled = true
	req = httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/_quick/identity", nil)
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("X-Quick-Dev-Token", token)
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), `"provider":"dev-proxy"`) || !strings.Contains(rr.Body.String(), `"subject":"dev-token:demo"`) {
		t.Fatalf("enabled token status=%d body=%q", rr.Code, rr.Body.String())
	}

	if _, err := h.Store.EnsureSite(ctx, "other", "other"); err != nil {
		t.Fatal(err)
	}
	wrongSite, _, err := h.Store.MintDevToken(ctx, "other", "tester", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	req = httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/_quick/identity", nil)
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("X-Quick-Dev-Token", wrongSite)
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusForbidden {
		t.Fatalf("wrong-site status=%d body=%q", rr.Code, rr.Body.String())
	}
}

func TestDevRemoteAPIProxyAndLocalSDK(t *testing.T) {
	h, root := testStaticHandler(t)
	var proxied bool
	remote := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/_quick/db/posts" {
			t.Fatalf("remote path=%s", r.URL.Path)
		}
		proxied = true
		if got := r.Header.Get("X-Quick-Dev-Token"); got != "secret-token" {
			t.Fatalf("X-Quick-Dev-Token=%q", got)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"proxied":true}`))
	}))
	defer remote.Close()
	h.DevDir = root
	h.DevSite = "demo"
	h.RemoteAPI = remote.URL
	h.RemoteAPIToken = "secret-token"

	req := httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/_quick/db/posts", nil)
	req.RemoteAddr = "127.0.0.1:1"
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK || !proxied || !strings.Contains(rr.Body.String(), "proxied") {
		t.Fatalf("proxy status=%d proxied=%v body=%q", rr.Code, proxied, rr.Body.String())
	}

	proxied = false
	req = httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/_quick/sdk.js", nil)
	req.RemoteAddr = "127.0.0.1:1"
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK || proxied || !strings.Contains(rr.Body.String(), "quick") {
		t.Fatalf("sdk status=%d proxied=%v body prefix=%q", rr.Code, proxied, rr.Body.String()[:min(20, rr.Body.Len())])
	}
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
