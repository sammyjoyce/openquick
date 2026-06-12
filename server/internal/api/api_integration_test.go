package api_test

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"nhooyr.io/websocket"

	"openquick.dev/quickd/internal/api"
	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
	"openquick.dev/quickd/internal/sites"
	"openquick.dev/quickd/internal/static"
	"openquick.dev/quickd/internal/store"
)

type appFixture struct {
	h   http.Handler
	api *api.Server
	cfg config.Config
	st  *store.Store
}

func newApp(t *testing.T, maxUpload int64) appFixture {
	t.Helper()
	root := t.TempDir()
	cfg := config.Default(root)
	cfg.MaxUploadBytes = maxUpload
	cfg.Viewer.AllowAnonymous = true
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
	apiHandler := api.New(cfg, st)
	h := static.New(cfg, st, identity.DevAdapter{Email: "sam@example.com", AllowPublicUnsafe: true}, apiHandler)
	return appFixture{h: h, api: apiHandler, cfg: cfg, st: st}
}

func doReq(h http.Handler, method, host, target string, body []byte) *httptest.ResponseRecorder {
	req := httptest.NewRequest(method, "http://"+host+target, bytes.NewReader(body))
	req.RemoteAddr = "127.0.0.1:1234"
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	rr := httptest.NewRecorder()
	h.ServeHTTP(rr, req)
	return rr
}

func TestDocumentCRUDAndSiteIsolation(t *testing.T) {
	app := newApp(t, 1024)
	rr := doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/db/posts", []byte(`{"title":"hello"}`))
	if rr.Code != http.StatusCreated {
		t.Fatalf("create status=%d body=%s", rr.Code, rr.Body.String())
	}
	var created map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &created); err != nil {
		t.Fatal(err)
	}
	id := created["id"].(string)
	rr = doReq(app.h, http.MethodPatch, "a.localhost:9366", "/_quick/db/posts/"+id, []byte(`{"status":"published"}`))
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), "published") {
		t.Fatalf("patch status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/posts", nil)
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), id) {
		t.Fatalf("list a status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodGet, "b.localhost:9366", "/_quick/db/posts", nil)
	if rr.Code != http.StatusOK || strings.Contains(rr.Body.String(), id) {
		t.Fatalf("cross-site leak status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodDelete, "a.localhost:9366", "/_quick/db/posts/"+id, nil)
	if rr.Code != http.StatusNoContent {
		t.Fatalf("delete status=%d body=%s", rr.Code, rr.Body.String())
	}
}

func TestAPIOriginAndUploadLimit(t *testing.T) {
	app := newApp(t, 4)
	req := httptest.NewRequest(http.MethodPost, "http://a.localhost:9366/_quick/db/posts", strings.NewReader(`{"x":1}`))
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Origin", "http://evil.example")
	rr := httptest.NewRecorder()
	app.h.ServeHTTP(rr, req)
	if rr.Code != http.StatusForbidden {
		t.Fatalf("cross origin status=%d", rr.Code)
	}
	req = httptest.NewRequest(http.MethodPost, "http://a.localhost:9366/_quick/uploads?name=x.txt", strings.NewReader("12345"))
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("Content-Type", "text/plain")
	rr = httptest.NewRecorder()
	app.h.ServeHTTP(rr, req)
	if rr.Code != http.StatusRequestEntityTooLarge {
		t.Fatalf("upload status=%d body=%s", rr.Code, rr.Body.String())
	}
}

func TestRealtimeSubscribeAndDBFanout(t *testing.T) {
	app := newApp(t, 1024)
	ts := httptest.NewServer(app.h)
	defer ts.Close()
	wsURL := "ws" + strings.TrimPrefix(ts.URL, "http") + "/~/a/_quick/realtime"
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	conn, _, err := websocket.Dial(ctx, wsURL, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "done")
	if err := conn.Write(ctx, websocket.MessageText, []byte(`{"type":"subscribe","channel":"db:posts"}`)); err != nil {
		t.Fatal(err)
	}
	_, ack, err := conn.Read(ctx)
	if err != nil || !strings.Contains(string(ack), "subscribed") {
		t.Fatalf("ack=%s err=%v", ack, err)
	}
	resp, err := http.Post(ts.URL+"/~/a/_quick/db/posts", "application/json", strings.NewReader(`{"title":"fanout"}`))
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusCreated {
		t.Fatalf("post status=%d", resp.StatusCode)
	}
	_, msg, err := conn.Read(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(msg), `"event":"create"`) || !strings.Contains(string(msg), `"channel":"db:posts"`) {
		t.Fatalf("unexpected event: %s", msg)
	}
}
