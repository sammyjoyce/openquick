package static

import (
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
	"openquick.dev/quickd/internal/store"
)

func testStaticHandler(t *testing.T) (*Handler, string) {
	t.Helper()
	root := t.TempDir()
	cfg := config.Default(root)
	st, err := store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { st.Close() })
	siteDir := filepath.Join(root, "sites", "demo")
	rel := filepath.Join(siteDir, "releases", "20260611T000000Z-abcdef")
	if err := os.MkdirAll(filepath.Join(rel, "assets"), 0o770); err != nil {
		t.Fatal(err)
	}
	files := map[string]string{
		"index.html":                  "<h1>demo</h1>",
		"assets/app.12345678.js":      "console.log('ok')",
		".env":                        "SECRET=1",
		"assets/nested.abcdef123.css": "body{}",
	}
	for name, body := range files {
		p := filepath.Join(rel, filepath.FromSlash(name))
		if err := os.MkdirAll(filepath.Dir(p), 0o770); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(body), 0o660); err != nil {
			t.Fatal(err)
		}
	}
	outside := filepath.Join(root, "outside.txt")
	if err := os.WriteFile(outside, []byte("secret"), 0o600); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(outside, filepath.Join(rel, "leak")); err != nil {
		t.Fatal(err)
	}
	if err := sites.WriteSiteConfig(siteDir, sites.SiteConfig{Name: "demo", Routing: sites.RoutingConfig{SPAFallback: "/index.html"}}); err != nil {
		t.Fatal(err)
	}
	current := filepath.Join(siteDir, "current")
	if err := os.Symlink(filepath.Join("releases", "20260611T000000Z-abcdef"), current); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Lstat(current); err != nil {
		t.Fatalf("current symlink missing: %v", err)
	}
	_, _ = st.EnsureSite(httptest.NewRequest(http.MethodGet, "/", nil).Context(), "demo", "demo")
	apiHandler := api.New(cfg, st)
	h := New(cfg, st, identity.NoneAdapter{}, apiHandler)
	return h, root
}

func perform(h http.Handler, host, target string) *httptest.ResponseRecorder {
	req := httptest.NewRequest(http.MethodGet, "http://"+host+target, nil)
	req.RemoteAddr = "127.0.0.1:12345"
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	return rr
}

func TestStaticHardeningAndRouting(t *testing.T) {
	h, _ := testStaticHandler(t)
	debugReq := httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/", nil)
	if site, stripped, ok := h.route(debugReq); !ok || site != "demo" || stripped != "/" {
		t.Fatalf("route site=%q stripped=%q ok=%v", site, stripped, ok)
	}
	if !h.siteExists("demo") {
		t.Fatalf("siteExists(demo)=false root=%s", h.Config.RemoteRoot)
	}
	if root, err := h.releaseRoot("demo"); err != nil || root == "" {
		t.Fatalf("releaseRoot=%q err=%v", root, err)
	}
	tests := []struct {
		name   string
		host   string
		path   string
		status int
		body   string
	}{
		{"host routing", "demo.localhost:9366", "/", http.StatusOK, "demo"},
		{"path fallback", "localhost:9366", "/~/demo/", http.StatusOK, "demo"},
		{"traversal", "demo.localhost:9366", "/%2e%2e/secret", http.StatusBadRequest, ""},
		{"dotfile", "demo.localhost:9366", "/.env", http.StatusNotFound, ""},
		{"symlink escape", "demo.localhost:9366", "/leak", http.StatusForbidden, ""},
		{"spa fallback", "demo.localhost:9366", "/missing/route", http.StatusOK, "demo"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			rr := perform(h, tt.host, tt.path)
			if rr.Code != tt.status {
				t.Fatalf("status=%d body=%q want %d", rr.Code, rr.Body.String(), tt.status)
			}
			if tt.body != "" && !strings.Contains(rr.Body.String(), tt.body) {
				t.Fatalf("body=%q missing %q", rr.Body.String(), tt.body)
			}
		})
	}
}

func TestStaticHeaders(t *testing.T) {
	h, _ := testStaticHandler(t)
	rr := perform(h, "demo.localhost:9366", "/")
	if rr.Header().Get("X-Content-Type-Options") != "nosniff" {
		t.Fatalf("missing nosniff")
	}
	if rr.Header().Get("Cache-Control") != "no-cache" {
		t.Fatalf("html cache=%q", rr.Header().Get("Cache-Control"))
	}
	rr = perform(h, "demo.localhost:9366", "/assets/app.12345678.js")
	if !strings.Contains(rr.Header().Get("Cache-Control"), "immutable") {
		t.Fatalf("hashed asset cache=%q", rr.Header().Get("Cache-Control"))
	}
}
