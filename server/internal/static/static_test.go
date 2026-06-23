package static

import (
	"archive/zip"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
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
	return performMethod(h, http.MethodGet, host, target)
}

func performMethod(h http.Handler, method, host, target string) *httptest.ResponseRecorder {
	req := httptest.NewRequest(method, "http://"+host+target, nil)
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
		{"symlink escape", "demo.localhost:9366", "/leak", http.StatusForbidden, "Return to the site home"},
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

func TestPrecompressedAssetServing(t *testing.T) {
	h, root := testStaticHandler(t)
	h.Adapter = identity.DevAdapter{Email: "sam@example.com", AllowPublicUnsafe: true}
	rel := filepath.Join(root, "sites", "demo", "releases", "20260611T000000Z-abcdef")
	brPath := filepath.Join(rel, "assets", "app.12345678.js.br")
	if err := os.WriteFile(brPath, []byte("brotli-bytes"), 0o660); err != nil {
		t.Fatal(err)
	}
	req := httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/assets/app.12345678.js", nil)
	req.Header.Set("Accept-Encoding", "br, gzip")
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK || rr.Body.String() != "brotli-bytes" {
		t.Fatalf("status=%d body=%q", rr.Code, rr.Body.String())
	}
	if rr.Header().Get("Content-Encoding") != "br" {
		t.Fatalf("content-encoding=%q", rr.Header().Get("Content-Encoding"))
	}
	if !strings.Contains(rr.Header().Get("Vary"), "Accept-Encoding") {
		t.Fatalf("vary=%q", rr.Header().Get("Vary"))
	}
	if ct := rr.Header().Get("Content-Type"); !strings.Contains(ct, "javascript") && !strings.Contains(ct, "text/plain") {
		t.Fatalf("content-type=%q", ct)
	}

	req = httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/assets/app.12345678.js", nil)
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK || strings.Contains(rr.Header().Get("Content-Encoding"), "br") || rr.Body.String() == "brotli-bytes" {
		t.Fatalf("fallback status=%d encoding=%q body=%q", rr.Code, rr.Header().Get("Content-Encoding"), rr.Body.String())
	}

	req = httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/assets/app.12345678.js", nil)
	req.Header.Set("Accept-Encoding", "br;q=0, gzip")
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK || rr.Header().Get("Content-Encoding") == "br" || rr.Body.String() == "brotli-bytes" {
		t.Fatalf("q=0 fallback status=%d encoding=%q body=%q", rr.Code, rr.Header().Get("Content-Encoding"), rr.Body.String())
	}

	req = httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/assets/app.12345678.js", nil)
	req.Header.Set("Accept-Encoding", "br;q=0, *;q=1")
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK || rr.Header().Get("Content-Encoding") == "br" || rr.Body.String() == "brotli-bytes" {
		t.Fatalf("specific q=0 fallback status=%d encoding=%q body=%q", rr.Code, rr.Header().Get("Content-Encoding"), rr.Body.String())
	}
}

func TestPermissionDeniedFriendlyHTMLAndAPIJSON(t *testing.T) {
	h, _ := testStaticHandler(t)
	req := httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/", nil)
	req.RemoteAddr = "203.0.113.9:12345"
	req.Header.Set("Accept", "text/html")
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusUnauthorized || !strings.Contains(rr.Header().Get("Content-Type"), "text/html") || !strings.Contains(rr.Body.String(), "Access required") || strings.Contains(rr.Body.String(), "anonymous not allowed") {
		t.Fatalf("html denial status=%d ct=%q body=%s", rr.Code, rr.Header().Get("Content-Type"), rr.Body.String())
	}

	req = httptest.NewRequest(http.MethodGet, "http://demo.localhost:9366/_quick/identity", nil)
	req.RemoteAddr = "203.0.113.9:12345"
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusUnauthorized || !strings.Contains(rr.Header().Get("Content-Type"), "application/json") || !strings.Contains(rr.Body.String(), "anonymous not allowed") {
		t.Fatalf("api denial status=%d ct=%q body=%s", rr.Code, rr.Header().Get("Content-Type"), rr.Body.String())
	}
}

func TestCustom404PageAndDirectoryListingOrder(t *testing.T) {
	h, root := testStaticHandler(t)
	siteDir := filepath.Join(root, "sites", "demo")
	rel := filepath.Join(siteDir, "releases", "20260611T000000Z-abcdef")
	if err := sites.WriteSiteConfig(siteDir, sites.SiteConfig{Name: "demo"}); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(rel, "404.html"), []byte("<h1>Custom missing page</h1>"), 0o660); err != nil {
		t.Fatal(err)
	}
	rr := perform(h, "demo.localhost:9366", "/missing")
	if rr.Code != http.StatusNotFound || !strings.Contains(rr.Body.String(), "Custom missing page") {
		t.Fatalf("custom 404 status=%d body=%q", rr.Code, rr.Body.String())
	}
	rr = performMethod(h, http.MethodHead, "demo.localhost:9366", "/missing")
	if rr.Code != http.StatusNotFound || rr.Body.Len() != 0 {
		t.Fatalf("custom 404 HEAD status=%d body=%q", rr.Code, rr.Body.String())
	}
	if err := os.WriteFile(filepath.Join(rel, "403.html"), []byte("<h1>Custom unavailable page</h1>"), 0o660); err != nil {
		t.Fatal(err)
	}
	rr = perform(h, "demo.localhost:9366", "/leak")
	if rr.Code != http.StatusForbidden || !strings.Contains(rr.Body.String(), "Custom unavailable page") || strings.Contains(rr.Body.String(), "outside.txt") {
		t.Fatalf("custom 403 status=%d body=%q", rr.Code, rr.Body.String())
	}

	docs := filepath.Join(rel, "docs")
	for _, dir := range []string{"z-dir", "a-dir"} {
		if err := os.MkdirAll(filepath.Join(docs, dir), 0o770); err != nil {
			t.Fatal(err)
		}
	}
	for name, body := range map[string]string{"z.txt": "z", "A.txt": "a", "readme.txt": "readme"} {
		if err := os.WriteFile(filepath.Join(docs, name), []byte(body), 0o660); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(filepath.Join(rel, "quick.json"), []byte(`{"routing":{"directory_listing":true}}`), 0o660); err != nil {
		t.Fatal(err)
	}
	rr = perform(h, "demo.localhost:9366", "/docs/")
	body := rr.Body.String()
	if rr.Code != http.StatusOK {
		t.Fatalf("listing status=%d body=%q", rr.Code, body)
	}
	order := []string{"a-dir/", "z-dir/", "A.txt", "readme.txt", "z.txt"}
	last := -1
	for _, want := range order {
		idx := strings.Index(body, want)
		if idx < 0 {
			t.Fatalf("listing missing %q: %s", want, body)
		}
		if idx <= last {
			t.Fatalf("listing order %v not reflected in %s", order, body)
		}
		last = idx
	}
}

func performWithOrigin(h http.Handler, host, target, origin string) *httptest.ResponseRecorder {
	req := httptest.NewRequest(http.MethodGet, "http://"+host+target, nil)
	req.RemoteAddr = "127.0.0.1:12345"
	if origin != "" {
		req.Header.Set("Origin", origin)
	}
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	return rr
}

func TestStaticCrossSiteLibraryCORS(t *testing.T) {
	h, _ := testStaticHandler(t)
	h.Config.PublicBaseDomain = "quick.example.com"

	rr := performWithOrigin(h, "demo.quick.example.com", "/assets/app.12345678.js", "https://lib.quick.example.com")
	if rr.Code != http.StatusOK {
		t.Fatalf("sibling status=%d body=%q", rr.Code, rr.Body.String())
	}
	if got := rr.Header().Get("Access-Control-Allow-Origin"); got != "https://lib.quick.example.com" {
		t.Fatalf("ACAO=%q", got)
	}
	if got := rr.Header().Get("Access-Control-Allow-Credentials"); got != "true" {
		t.Fatalf("ACAC=%q", got)
	}
	if !strings.Contains(rr.Header().Get("Vary"), "Origin") {
		t.Fatalf("Vary=%q", rr.Header().Get("Vary"))
	}

	rr = performWithOrigin(h, "demo.quick.example.com", "/assets/app.12345678.js", "https://evil.example")
	if rr.Code != http.StatusOK {
		t.Fatalf("non-sibling status=%d body=%q", rr.Code, rr.Body.String())
	}
	if got := rr.Header().Get("Access-Control-Allow-Origin"); got != "" {
		t.Fatalf("non-sibling ACAO=%q", got)
	}

	rr = performWithOrigin(h, "demo.quick.example.com", "/_quick/capabilities", "https://lib.quick.example.com")
	if rr.Code != http.StatusOK {
		t.Fatalf("api status=%d body=%q", rr.Code, rr.Body.String())
	}
	if got := rr.Header().Get("Access-Control-Allow-Origin"); got != "" {
		t.Fatalf("api ACAO changed to %q", got)
	}
}

func TestRootSDKServedWithoutSiteRoute(t *testing.T) {
	h, _ := testStaticHandler(t)
	rr := perform(h, "localhost:9366", "/_quick/sdk.js")
	if rr.Code != http.StatusOK {
		t.Fatalf("sdk status=%d body=%q", rr.Code, rr.Body.String())
	}
	if got := rr.Header().Get("Content-Type"); !strings.Contains(got, "text/javascript") {
		t.Fatalf("sdk content-type=%q", got)
	}
	if !strings.Contains(rr.Body.String(), "OpenQuick request failed") {
		t.Fatalf("sdk body missing expected SDK content: %q", rr.Body.String())
	}
}

func TestSiteDirectoryRendersRows(t *testing.T) {
	h, _ := testStaticHandler(t)
	h.Config.PublicBaseDomain = "quick.example.com"
	if err := h.Store.RecordDeploy(context.Background(), "demo", "20260612T010203Z-feedface", "sam@example.com", 123, 4); err != nil {
		t.Fatal(err)
	}

	rr := perform(h, "quick.example.com", "/")
	if rr.Code != http.StatusOK {
		t.Fatalf("directory status=%d body=%q", rr.Code, rr.Body.String())
	}
	body := rr.Body.String()
	for _, want := range []string{"OpenQuick sites", "demo", "https://demo.quick.example.com", "20260612T010203Z-feedface", "sam@example.com"} {
		if !strings.Contains(body, want) {
			t.Fatalf("directory body missing %q: %s", want, body)
		}
	}
}

func TestSiteDirectoryEscapesMetadata(t *testing.T) {
	h, _ := testStaticHandler(t)
	h.Config.PublicBaseDomain = "quick.example.com"
	if err := h.Store.RecordDeploy(context.Background(), "evil", "<b>rel</b>", "<script>alert(1)</script>", 1, 1); err != nil {
		t.Fatal(err)
	}

	rr := perform(h, "quick.example.com", "/~/")
	if rr.Code != http.StatusOK {
		t.Fatalf("directory status=%d body=%q", rr.Code, rr.Body.String())
	}
	body := rr.Body.String()
	if strings.Contains(body, "<script>alert(1)</script>") || strings.Contains(body, "<b>rel</b>") {
		t.Fatalf("directory did not escape hostile metadata: %s", body)
	}
	if !strings.Contains(body, "&lt;script&gt;alert(1)&lt;/script&gt;") || !strings.Contains(body, "&lt;b&gt;rel&lt;/b&gt;") {
		t.Fatalf("directory missing escaped metadata: %s", body)
	}
}

func TestSiteDirectoryDisabled(t *testing.T) {
	h, _ := testStaticHandler(t)
	h.Config.Directory.Enabled = false
	rr := perform(h, "localhost:9366", "/")
	if rr.Code != http.StatusNotFound {
		t.Fatalf("disabled status=%d body=%q", rr.Code, rr.Body.String())
	}
}

func TestSiteDirectoryAnonymousDeniedWhenIdentityRequired(t *testing.T) {
	h, _ := testStaticHandler(t)
	h.Config.Viewer.AllowAnonymous = false
	h.Config.Viewer.RequireIdentity = true
	rr := perform(h, "localhost:9366", "/")
	if rr.Code != http.StatusUnauthorized {
		t.Fatalf("identity-required status=%d body=%q", rr.Code, rr.Body.String())
	}
}

func TestAliasCustomDomainAskPublicAndListing(t *testing.T) {
	h, root := testStaticHandler(t)
	ctx := context.Background()
	if _, err := h.Store.EnsureSite(ctx, "demo", "alias"); err != nil {
		t.Fatal(err)
	}
	rr := perform(h, "alias.localhost:9366", "/")
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), "demo") {
		t.Fatalf("alias status=%d body=%q", rr.Code, rr.Body.String())
	}
	if _, err := h.Store.AddDomain(ctx, "app.example.org", "demo"); err != nil {
		t.Fatal(err)
	}
	rr = perform(h, "app.example.org", "/")
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), "demo") {
		t.Fatalf("domain status=%d body=%q", rr.Code, rr.Body.String())
	}
	req := httptest.NewRequest(http.MethodGet, "http://quick.example.com/_quick/domains/ask?domain=app.example.org", nil)
	req.RemoteAddr = "127.0.0.1:1"
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK {
		t.Fatalf("ask known=%d body=%q", rr.Code, rr.Body.String())
	}
	req = httptest.NewRequest(http.MethodGet, "http://quick.example.com/_quick/domains/ask?domain=missing.example.org", nil)
	req.RemoteAddr = "203.0.113.10:1"
	rr = httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	if rr.Code != http.StatusNotFound {
		t.Fatalf("ask untrusted=%d", rr.Code)
	}

	h.Config.PublicStatic.Enabled = true
	h.Config.Viewer.AllowAnonymous = false
	h.Config.Viewer.RequireIdentity = true
	if err := h.Store.SetSitePublic(ctx, "demo", true); err != nil {
		t.Fatal(err)
	}
	rr = perform(h, "alias.localhost:9366", "/")
	if rr.Code != http.StatusOK {
		t.Fatalf("public static=%d body=%q", rr.Code, rr.Body.String())
	}
	rr = perform(h, "alias.localhost:9366", "/_quick/identity")
	if rr.Code != http.StatusUnauthorized {
		t.Fatalf("public api anonymous=%d body=%q", rr.Code, rr.Body.String())
	}

	rel := filepath.Join(root, "sites", "demo", "releases", "20260611T000000Z-abcdef")
	if err := os.MkdirAll(filepath.Join(rel, "docs"), 0o770); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(rel, "docs", "readme.txt"), []byte("hello"), 0o660); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(rel, "docs", ".secret"), []byte("hide"), 0o660); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(rel, "quick.json"), []byte(`{"routing":{"directory_listing":true}}`), 0o660); err != nil {
		t.Fatal(err)
	}
	h.Config.Viewer.AllowAnonymous = true
	h.Config.Viewer.RequireIdentity = false
	rr = perform(h, "alias.localhost:9366", "/docs/")
	body := rr.Body.String()
	if rr.Code != http.StatusOK || !strings.Contains(body, "readme.txt") || strings.Contains(body, ".secret") {
		t.Fatalf("listing status=%d body=%q", rr.Code, body)
	}
}

func zipBody(t *testing.T, files map[string]string) []byte {
	t.Helper()
	var buf bytes.Buffer
	zw := zip.NewWriter(&buf)
	for name, body := range files {
		w, err := zw.Create(name)
		if err != nil {
			t.Fatal(err)
		}
		if _, err := w.Write([]byte(body)); err != nil {
			t.Fatal(err)
		}
	}
	if err := zw.Close(); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func portalTokenHash(token string) string {
	sum := sha256.Sum256([]byte(token))
	return hex.EncodeToString(sum[:])
}

func postDeploy(h http.Handler, target, token, origin string, body []byte) *httptest.ResponseRecorder {
	req := httptest.NewRequest(http.MethodPost, "http://localhost:9366"+target, bytes.NewReader(body))
	req.RemoteAddr = "127.0.0.1:12345"
	req.Header.Set("Content-Type", "application/zip")
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	if origin != "" {
		req.Header.Set("Origin", origin)
	}
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	return rr
}

func TestHTTPDeployPortalTokenOriginConfirmAndActivation(t *testing.T) {
	h, root := testStaticHandler(t)
	body := zipBody(t, map[string]string{"index.html": "portal"})

	rr := postDeploy(h, "/_quick/deploy/demo", "", "", body)
	if rr.Code != http.StatusNotFound {
		t.Fatalf("disabled status=%d body=%q", rr.Code, rr.Body.String())
	}
	h.Config.HTTPDeploy.Enabled = true
	h.Config.HTTPDeploy.Tokens = []string{portalTokenHash("secret")}
	if err := h.Store.RecordDeploy(context.Background(), "demo", "20260612T010203Z-feedface", "alice", 1, 1); err != nil {
		t.Fatal(err)
	}
	rr = postDeploy(h, "/_quick/deploy/demo", "", "", body)
	if rr.Code != http.StatusForbidden {
		t.Fatalf("unauthorized status=%d body=%q", rr.Code, rr.Body.String())
	}
	rr = postDeploy(h, "/_quick/deploy/demo", "secret", "https://evil.example", body)
	if rr.Code != http.StatusForbidden {
		t.Fatalf("origin status=%d body=%q", rr.Code, rr.Body.String())
	}
	rr = postDeploy(h, "/_quick/deploy/demo", "secret", "", body)
	if rr.Code != http.StatusConflict || !strings.Contains(rr.Body.String(), "confirm_overwrite") {
		t.Fatalf("confirm status=%d body=%q", rr.Code, rr.Body.String())
	}
	rr = postDeploy(h, "/_quick/deploy/demo?confirm=demo", "secret", "", body)
	if rr.Code != http.StatusOK {
		t.Fatalf("activate status=%d body=%q", rr.Code, rr.Body.String())
	}
	current := filepath.Join(root, "sites", "demo", "current", "index.html")
	b, err := os.ReadFile(current)
	if err != nil || string(b) != "portal" {
		t.Fatalf("current=%q err=%v", b, err)
	}
}
