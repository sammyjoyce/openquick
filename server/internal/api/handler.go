package api

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"net/http"
	"net/url"
	"path"
	"regexp"
	"strings"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
	"openquick.dev/quickd/internal/ratelimit"
	"openquick.dev/quickd/internal/realtime"
	"openquick.dev/quickd/internal/store"
	"openquick.dev/quickd/internal/uploads"
)

const jsonLimit = 1 << 20

type siteContextKey struct{}

type SiteContext struct {
	Name string
	Dev  bool
}

func WithSite(ctx context.Context, site SiteContext) context.Context {
	return context.WithValue(ctx, siteContextKey{}, site)
}

func SiteFromContext(ctx context.Context) (SiteContext, bool) {
	site, ok := ctx.Value(siteContextKey{}).(SiteContext)
	return site, ok
}

type Server struct {
	Config      config.Config
	Store       *store.Store
	Uploads     *uploads.Manager
	Realtime    *realtime.Hub
	RateLimiter *ratelimit.Limiter
}

func New(cfg config.Config, st *store.Store) *Server {
	cfg.ApplyDefaults()
	return &Server{Config: cfg, Store: st, Uploads: uploads.New(cfg.RemoteRoot, cfg.MaxUploadBytes, st), Realtime: realtime.New(), RateLimiter: ratelimit.New()}
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path == "/_quick/health" {
		writeJSON(w, http.StatusOK, map[string]any{"format_version": "1.0", "ok": true})
		return
	}
	if !s.sameOriginOK(w, r) {
		return
	}
	site, ok := SiteFromContext(r.Context())
	if !ok || site.Name == "" {
		http.NotFound(w, r)
		return
	}
	id, _ := identity.FromContext(r.Context())
	if id == nil {
		http.Error(w, "identity missing", http.StatusUnauthorized)
		return
	}
	if strings.HasPrefix(r.URL.Path, "/_quick/db/") {
		s.handleDB(w, r, site.Name, id)
		return
	}
	if strings.HasPrefix(r.URL.Path, "/_quick/uploads") {
		s.handleUploads(w, r, site.Name, id)
		return
	}
	if strings.HasPrefix(r.URL.Path, "/_quick/ai/") {
		s.handleAI(w, r, site.Name, id)
		return
	}
	if strings.HasPrefix(r.URL.Path, "/_quick/warehouse/") {
		s.handleWarehouse(w, r, site.Name, id)
		return
	}
	switch r.URL.Path {
	case "/_quick/identity":
		if r.Method != http.MethodGet {
			methodNotAllowed(w)
			return
		}
		writeJSON(w, http.StatusOK, id)
	case "/_quick/sdk.js":
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			methodNotAllowed(w)
			return
		}
		w.Header().Set("Content-Type", "text/javascript; charset=utf-8")
		w.Header().Set("Cache-Control", "no-cache")
		_, _ = w.Write(embeddedSDK)
	case "/_quick/capabilities":
		if r.Method != http.MethodGet {
			methodNotAllowed(w)
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"identity": true, "db": true, "realtime": true, "uploads": true, "ai": s.Config.AIConfigured(), "warehouse": s.Config.WarehouseConfigured()})
	case "/_quick/realtime":
		if r.Method != http.MethodGet {
			methodNotAllowed(w)
			return
		}
		if !s.dataAllowed(r, id) {
			http.Error(w, "authentication required", http.StatusUnauthorized)
			return
		}
		if err := s.Realtime.Serve(w, r, site.Name, identity.SubjectKey(id)); err != nil {
			return
		}
	default:
		http.NotFound(w, r)
	}
}

func (s *Server) sameOriginOK(w http.ResponseWriter, r *http.Request) bool {
	origin := r.Header.Get("Origin")
	if origin == "" {
		return true
	}
	u, err := url.Parse(origin)
	if err != nil {
		http.Error(w, "invalid origin", http.StatusForbidden)
		return false
	}
	if !sameHost(u.Host, r.Host) {
		if isStateChanging(r.Method) {
			http.Error(w, "cross-origin state change denied", http.StatusForbidden)
			return false
		}
		return true
	}
	w.Header().Set("Access-Control-Allow-Origin", origin)
	w.Header().Set("Vary", "Origin")
	if r.Method == http.MethodOptions {
		w.Header().Set("Access-Control-Allow-Methods", "GET,POST,PUT,PATCH,DELETE,OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
		w.WriteHeader(http.StatusNoContent)
		return false
	}
	return true
}

func sameHost(a, b string) bool { return strings.EqualFold(stripPort(a), stripPort(b)) }

func stripPort(h string) string {
	if strings.Count(h, ":") == 1 {
		if i := strings.LastIndexByte(h, ':'); i >= 0 {
			return strings.ToLower(h[:i])
		}
	}
	return strings.ToLower(strings.Trim(h, "[]"))
}

func isStateChanging(method string) bool {
	switch method {
	case http.MethodPost, http.MethodPut, http.MethodPatch, http.MethodDelete:
		return true
	default:
		return false
	}
}

func (s *Server) dataAllowed(r *http.Request, id *identity.Identity) bool {
	if id != nil && id.Authenticated {
		return true
	}
	site, _ := SiteFromContext(r.Context())
	return site.Dev && identity.IsLoopbackRemote(r.RemoteAddr)
}

var collectionRE = regexp.MustCompile(`^[A-Za-z0-9_-]{1,64}$`)
var idRE = regexp.MustCompile(`^[A-Za-z0-9_.:-]{1,128}$`)

func (s *Server) handleDB(w http.ResponseWriter, r *http.Request, site string, id *identity.Identity) {
	if s.Store == nil {
		http.Error(w, "store unavailable", http.StatusServiceUnavailable)
		return
	}
	if !s.dataAllowed(r, id) {
		http.Error(w, "authentication required", http.StatusUnauthorized)
		return
	}
	collection, docID, ok := splitDBPath(r.URL.Path)
	if !ok || !collectionRE.MatchString(collection) || (docID != "" && !idRE.MatchString(docID)) {
		http.NotFound(w, r)
		return
	}
	actor := identity.SubjectKey(id)
	switch r.Method {
	case http.MethodGet:
		if docID == "" {
			docs, err := s.Store.ListDocuments(r.Context(), site, collection)
			if err != nil {
				s.writeStoreError(w, err)
				return
			}
			out := make([]map[string]any, 0, len(docs))
			for _, d := range docs {
				out = append(out, documentJSON(d))
			}
			writeJSON(w, http.StatusOK, map[string]any{"documents": out})
			return
		}
		doc, err := s.Store.GetDocument(r.Context(), site, collection, docID)
		if err != nil {
			s.writeStoreError(w, err)
			return
		}
		writeJSON(w, http.StatusOK, documentJSON(doc))
	case http.MethodPost:
		if docID != "" {
			http.NotFound(w, r)
			return
		}
		body, err := readJSONBody(w, r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		newID, err := randomID()
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		doc, err := s.Store.CreateDocument(r.Context(), site, collection, newID, string(body), actor)
		if err != nil {
			s.writeStoreError(w, err)
			return
		}
		out := documentJSON(doc)
		s.Realtime.Publish(site, "db:"+collection, "create", out)
		writeJSON(w, http.StatusCreated, out)
	case http.MethodPut:
		if docID == "" {
			http.NotFound(w, r)
			return
		}
		body, err := readJSONBody(w, r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		doc, err := s.Store.PutDocument(r.Context(), site, collection, docID, string(body), actor)
		if err != nil {
			s.writeStoreError(w, err)
			return
		}
		out := documentJSON(doc)
		s.Realtime.Publish(site, "db:"+collection, "update", out)
		writeJSON(w, http.StatusOK, out)
	case http.MethodPatch:
		if docID == "" {
			http.NotFound(w, r)
			return
		}
		patch, err := readJSONObject(w, r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		old, err := s.Store.GetDocument(r.Context(), site, collection, docID)
		if err != nil {
			s.writeStoreError(w, err)
			return
		}
		var current map[string]any
		if err := json.Unmarshal([]byte(old.DataJSON), &current); err != nil || current == nil {
			current = map[string]any{}
		}
		for k, v := range patch {
			current[k] = v
		}
		body, _ := json.Marshal(current)
		doc, err := s.Store.PutDocument(r.Context(), site, collection, docID, string(body), actor)
		if err != nil {
			s.writeStoreError(w, err)
			return
		}
		out := documentJSON(doc)
		s.Realtime.Publish(site, "db:"+collection, "update", out)
		writeJSON(w, http.StatusOK, out)
	case http.MethodDelete:
		if docID == "" {
			http.NotFound(w, r)
			return
		}
		if err := s.Store.DeleteDocument(r.Context(), site, collection, docID); err != nil {
			s.writeStoreError(w, err)
			return
		}
		s.Realtime.Publish(site, "db:"+collection, "delete", map[string]any{"id": docID})
		w.WriteHeader(http.StatusNoContent)
	default:
		methodNotAllowed(w)
	}
}

func splitDBPath(p string) (collection, id string, ok bool) {
	rest := strings.TrimPrefix(p, "/_quick/db/")
	parts := strings.Split(strings.Trim(rest, "/"), "/")
	if len(parts) == 1 && parts[0] != "" {
		return parts[0], "", true
	}
	if len(parts) == 2 && parts[0] != "" && parts[1] != "" {
		return parts[0], parts[1], true
	}
	return "", "", false
}

func documentJSON(d store.Document) map[string]any {
	var data any
	if err := json.Unmarshal([]byte(d.DataJSON), &data); err != nil {
		data = nil
	}
	return map[string]any{"id": d.ID, "data": data, "created_by": d.CreatedBy, "updated_by": d.UpdatedBy, "created_at": d.CreatedAt, "updated_at": d.UpdatedAt}
}

func (s *Server) handleUploads(w http.ResponseWriter, r *http.Request, site string, id *identity.Identity) {
	if s.Uploads == nil {
		http.Error(w, "uploads unavailable", http.StatusServiceUnavailable)
		return
	}
	if !s.dataAllowed(r, id) {
		http.Error(w, "authentication required", http.StatusUnauthorized)
		return
	}
	upID := strings.TrimPrefix(r.URL.Path, "/_quick/uploads")
	upID = strings.Trim(upID, "/")
	actor := identity.SubjectKey(id)
	switch r.Method {
	case http.MethodPost:
		if upID != "" {
			http.NotFound(w, r)
			return
		}
		r.Body = http.MaxBytesReader(w, r.Body, s.Config.MaxUploadBytes+1)
		ct := r.Header.Get("Content-Type")
		media, _, _ := mime.ParseMediaType(ct)
		var u store.Upload
		var err error
		if strings.HasPrefix(media, "multipart/") {
			mr, e := r.MultipartReader()
			if e != nil {
				http.Error(w, e.Error(), http.StatusBadRequest)
				return
			}
			u, err = s.Uploads.SaveMultipart(r.Context(), site, actor, mr)
		} else {
			name := r.URL.Query().Get("name")
			u, err = s.Uploads.SaveRaw(r.Context(), site, actor, name, ct, r.Body)
		}
		if err != nil {
			http.Error(w, err.Error(), uploads.ErrorStatus(err))
			return
		}
		writeJSON(w, http.StatusCreated, uploadJSON(u))
	case http.MethodGet:
		if upID == "" {
			http.NotFound(w, r)
			return
		}
		if err := s.Uploads.Serve(r.Context(), site, upID, w, r); err != nil {
			http.Error(w, err.Error(), uploads.ErrorStatus(err))
		}
	case http.MethodDelete:
		if upID == "" {
			http.NotFound(w, r)
			return
		}
		if err := s.Uploads.Delete(r.Context(), site, upID); err != nil {
			http.Error(w, err.Error(), uploads.ErrorStatus(err))
			return
		}
		w.WriteHeader(http.StatusNoContent)
	default:
		methodNotAllowed(w)
	}
}

func uploadJSON(u store.Upload) map[string]any {
	return map[string]any{"id": u.ID, "url": uploads.URL(u.ID), "name": u.Name, "content_type": u.ContentType, "size": u.Size, "created_by": u.CreatedBy, "created_at": u.CreatedAt}
}

func (s *Server) writeStoreError(w http.ResponseWriter, err error) {
	if errors.Is(err, store.ErrNotFound) {
		http.Error(w, "not found", http.StatusNotFound)
		return
	}
	http.Error(w, err.Error(), http.StatusInternalServerError)
}

func readJSONBody(w http.ResponseWriter, r *http.Request) ([]byte, error) {
	r.Body = http.MaxBytesReader(w, r.Body, jsonLimit)
	data, err := io.ReadAll(r.Body)
	if err != nil {
		return nil, err
	}
	data = bytes.TrimSpace(data)
	if len(data) == 0 {
		return nil, errors.New("empty JSON body")
	}
	if !json.Valid(data) {
		return nil, errors.New("invalid JSON")
	}
	var v any
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	if err := dec.Decode(&v); err != nil {
		return nil, err
	}
	if depth(v) > 32 {
		return nil, errors.New("JSON depth exceeds limit")
	}
	return data, nil
}

func readJSONObject(w http.ResponseWriter, r *http.Request) (map[string]any, error) {
	data, err := readJSONBody(w, r)
	if err != nil {
		return nil, err
	}
	var obj map[string]any
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.UseNumber()
	if err := dec.Decode(&obj); err != nil || obj == nil {
		return nil, errors.New("JSON object required")
	}
	return obj, nil
}

func depth(v any) int {
	switch x := v.(type) {
	case []any:
		m := 1
		for _, e := range x {
			if d := 1 + depth(e); d > m {
				m = d
			}
		}
		return m
	case map[string]any:
		m := 1
		for _, e := range x {
			if d := 1 + depth(e); d > m {
				m = d
			}
		}
		return m
	default:
		return 1
	}
}

func randomID() (string, error) {
	var b [12]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return hex.EncodeToString(b[:]), nil
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func methodNotAllowed(w http.ResponseWriter) {
	http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
}

func CleanAPIPath(p string) string { return path.Clean("/" + p) }

func ErrorJSON(w http.ResponseWriter, status int, msg string) {
	writeJSON(w, status, map[string]any{"error": msg})
}

func ErrorFromIdentity(w http.ResponseWriter, err error) {
	status := identity.StatusForError(err)
	ErrorJSON(w, status, err.Error())
}

func FormatError(err error) string {
	if err == nil {
		return ""
	}
	return fmt.Sprintf("%v", err)
}
