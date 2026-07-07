package api_test

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/url"
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
	revision := created["revision"].(string)
	etag := rr.Header().Get("ETag")
	if revision == "" || etag == "" {
		t.Fatalf("missing revision/etag created=%v etag=%q", created, etag)
	}
	req := httptest.NewRequest(http.MethodPatch, "http://a.localhost:9366/_quick/db/posts/"+id, strings.NewReader(`{"status":"published"}`))
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("If-Match", etag)
	rr = httptest.NewRecorder()
	app.h.ServeHTTP(rr, req)
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), "published") {
		t.Fatalf("patch status=%d body=%s", rr.Code, rr.Body.String())
	}
	var patched map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &patched); err != nil {
		t.Fatal(err)
	}
	currentRevision := patched["revision"].(string)
	req = httptest.NewRequest(http.MethodPatch, "http://a.localhost:9366/_quick/db/posts/"+id, strings.NewReader(`{"status":"stale"}`))
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("If-Match", revision)
	rr = httptest.NewRecorder()
	app.h.ServeHTTP(rr, req)
	if rr.Code != http.StatusConflict || !strings.Contains(rr.Body.String(), "revision_mismatch") {
		t.Fatalf("stale patch status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodPut, "a.localhost:9366", "/_quick/db/posts/"+id+"?revision="+url.QueryEscape(currentRevision), []byte(`{"title":"replaced","status":"published"}`))
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), "replaced") {
		t.Fatalf("conditional put status=%d body=%s", rr.Code, rr.Body.String())
	}
	var replaced map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &replaced); err != nil {
		t.Fatal(err)
	}
	currentRevision = replaced["revision"].(string)
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/posts", nil)
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), id) {
		t.Fatalf("list a status=%d body=%s", rr.Code, rr.Body.String())
	}
	for _, title := range []string{"second", "third"} {
		rr = doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/db/posts", []byte(`{"title":"`+title+`"}`))
		if rr.Code != http.StatusCreated {
			t.Fatalf("create %s status=%d body=%s", title, rr.Code, rr.Body.String())
		}
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/posts?limit=1", nil)
	if rr.Code != http.StatusOK {
		t.Fatalf("paged list status=%d body=%s", rr.Code, rr.Body.String())
	}
	var page struct {
		Documents  []map[string]any `json:"documents"`
		NextCursor string           `json:"next_cursor"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &page); err != nil {
		t.Fatal(err)
	}
	if len(page.Documents) != 1 || page.NextCursor == "" {
		t.Fatalf("page=%+v", page)
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/posts?limit=2&cursor="+url.QueryEscape(page.NextCursor), nil)
	if rr.Code != http.StatusOK || !strings.Contains(rr.Body.String(), "second") || !strings.Contains(rr.Body.String(), "third") {
		t.Fatalf("second page status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/posts?cursor=bogus", nil)
	if rr.Code != http.StatusBadRequest {
		t.Fatalf("bad cursor status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodGet, "b.localhost:9366", "/_quick/db/posts", nil)
	if rr.Code != http.StatusOK || strings.Contains(rr.Body.String(), id) {
		t.Fatalf("cross-site leak status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodDelete, "a.localhost:9366", "/_quick/db/posts/missing?revision="+url.QueryEscape(currentRevision), nil)
	if rr.Code != http.StatusNotFound {
		t.Fatalf("conditional missing delete status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodDelete, "a.localhost:9366", "/_quick/db/posts/"+id+"?revision="+url.QueryEscape(currentRevision), nil)
	if rr.Code != http.StatusNoContent {
		t.Fatalf("delete status=%d body=%s", rr.Code, rr.Body.String())
	}
}

func TestDocumentFilterAndSortQueries(t *testing.T) {
	app := newApp(t, 1024)
	for _, body := range []string{
		`{"title":"first","status":"closed"}`,
		`{"title":"second","status":"open"}`,
		`{"title":"third","status":"open"}`,
	} {
		rr := doReq(app.h, http.MethodPost, "a.localhost:9366", "/_quick/db/tasks", []byte(body))
		if rr.Code != http.StatusCreated {
			t.Fatalf("create status=%d body=%s", rr.Code, rr.Body.String())
		}
	}
	rr := doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/tasks?filter="+url.QueryEscape(`{"status":"open"}`)+"&sort=-created_at", nil)
	if rr.Code != http.StatusOK {
		t.Fatalf("filter status=%d body=%s", rr.Code, rr.Body.String())
	}
	var res struct {
		Documents []map[string]any `json:"documents"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &res); err != nil {
		t.Fatal(err)
	}
	if len(res.Documents) != 2 {
		t.Fatalf("documents=%+v", res.Documents)
	}
	for _, doc := range res.Documents {
		data := doc["data"].(map[string]any)
		if data["status"] != "open" {
			t.Fatalf("unexpected filtered doc: %+v", doc)
		}
	}
	if res.Documents[0]["created_at"].(string) < res.Documents[1]["created_at"].(string) {
		t.Fatalf("not sorted descending: %+v", res.Documents)
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/tasks?limit=1&filter="+url.QueryEscape(`{"status":"open"}`)+"&sort=id", nil)
	if rr.Code != http.StatusOK {
		t.Fatalf("filtered page status=%d body=%s", rr.Code, rr.Body.String())
	}
	var firstPage struct {
		Documents  []map[string]any `json:"documents"`
		NextCursor string           `json:"next_cursor"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &firstPage); err != nil {
		t.Fatal(err)
	}
	if len(firstPage.Documents) != 1 || firstPage.NextCursor == "" {
		t.Fatalf("first filtered page=%+v", firstPage)
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/tasks?limit=1&filter="+url.QueryEscape(`{"status":"open"}`)+"&sort=id&cursor="+url.QueryEscape(firstPage.NextCursor), nil)
	if rr.Code != http.StatusOK {
		t.Fatalf("filtered second page status=%d body=%s", rr.Code, rr.Body.String())
	}
	var secondPage struct {
		Documents  []map[string]any `json:"documents"`
		NextCursor string           `json:"next_cursor"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &secondPage); err != nil {
		t.Fatal(err)
	}
	if len(secondPage.Documents) != 1 || secondPage.NextCursor != "" || secondPage.Documents[0]["id"] == firstPage.Documents[0]["id"] {
		t.Fatalf("second filtered page=%+v first=%+v", secondPage, firstPage)
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/db/tasks?filter="+url.QueryEscape(`{"status":{"$ne":"open"}}`), nil)
	if rr.Code != http.StatusBadRequest || !strings.Contains(rr.Body.String(), "unsupported filter operator") {
		t.Fatalf("unsupported filter status=%d body=%s", rr.Code, rr.Body.String())
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

func TestUploadHeadChecksStoredObject(t *testing.T) {
	app := newApp(t, 1024)
	req := httptest.NewRequest(http.MethodPost, "http://a.localhost:9366/_quick/uploads?name=x.txt", strings.NewReader("hello"))
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("Content-Type", "text/plain")
	rr := httptest.NewRecorder()
	app.h.ServeHTTP(rr, req)
	if rr.Code != http.StatusCreated {
		t.Fatalf("upload status=%d body=%s", rr.Code, rr.Body.String())
	}
	var uploaded map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &uploaded); err != nil {
		t.Fatal(err)
	}
	id, _ := uploaded["id"].(string)
	if id == "" {
		t.Fatalf("missing upload id: %#v", uploaded)
	}
	rr = doReq(app.h, http.MethodHead, "a.localhost:9366", "/_quick/uploads/"+id, nil)
	if rr.Code != http.StatusOK {
		t.Fatalf("head status=%d body=%s", rr.Code, rr.Body.String())
	}
	if rr.Body.Len() != 0 {
		t.Fatalf("HEAD returned body %q", rr.Body.String())
	}
	if got := rr.Header().Get("Content-Type"); !strings.Contains(got, "text/plain") {
		t.Fatalf("content-type=%q", got)
	}
	req = httptest.NewRequest(http.MethodPost, "http://a.localhost:9366/_quick/uploads?name=y.txt", strings.NewReader("bye"))
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("Content-Type", "text/plain")
	rr = httptest.NewRecorder()
	app.h.ServeHTTP(rr, req)
	if rr.Code != http.StatusCreated {
		t.Fatalf("second upload status=%d body=%s", rr.Code, rr.Body.String())
	}
	rr = doReq(app.h, http.MethodGet, "a.localhost:9366", "/_quick/uploads?limit=10", nil)
	if rr.Code != http.StatusOK {
		t.Fatalf("list uploads status=%d body=%s", rr.Code, rr.Body.String())
	}
	var list struct {
		Uploads []map[string]any `json:"uploads"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &list); err != nil {
		t.Fatal(err)
	}
	if len(list.Uploads) != 2 {
		t.Fatalf("uploads=%+v", list.Uploads)
	}
	if list.Uploads[0]["name"] == "" || list.Uploads[0]["size"] == nil || list.Uploads[0]["created_by"] == nil || list.Uploads[0]["content_type"] == nil {
		t.Fatalf("upload metadata missing: %+v", list.Uploads[0])
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
