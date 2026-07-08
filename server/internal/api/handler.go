package api

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
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
	"sort"
	"strconv"
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
	if r.URL.Path == "/_quick/warehouse" || strings.HasPrefix(r.URL.Path, "/_quick/warehouse/") {
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
			limit, err := parseListLimit(r.URL.Query().Get("limit"))
			if err != nil {
				http.Error(w, err.Error(), http.StatusBadRequest)
				return
			}
			filterRaw := r.URL.Query().Get("filter")
			sortKey := r.URL.Query().Get("sort")
			if filterRaw != "" || sortKey != "" {
				sortSpec, err := parseDocumentSort(sortKey)
				if err != nil {
					http.Error(w, err.Error(), http.StatusBadRequest)
					return
				}
				docs, err := s.Store.ListDocuments(r.Context(), site, collection)
				if err != nil {
					s.writeStoreError(w, err)
					return
				}
				docs, err = filterSortDocuments(docs, filterRaw, sortSpec)
				if err != nil {
					http.Error(w, err.Error(), http.StatusBadRequest)
					return
				}
				docs, nextCursor, err := pageFilteredDocuments(docs, limit, r.URL.Query().Get("cursor"), sortSpec)
				if err != nil {
					s.writeStoreError(w, err)
					return
				}
				out := make([]map[string]any, 0, len(docs))
				for _, d := range docs {
					out = append(out, documentJSON(d))
				}
				writeJSON(w, http.StatusOK, map[string]any{"documents": out, "next_cursor": nextCursor, "filter": filterRaw, "sort": sortKey})
				return
			}
			page, err := s.Store.ListDocumentsPage(r.Context(), site, collection, limit, r.URL.Query().Get("cursor"))
			if err != nil {
				s.writeStoreError(w, err)
				return
			}
			out := make([]map[string]any, 0, len(page.Documents))
			for _, d := range page.Documents {
				out = append(out, documentJSON(d))
			}
			writeJSON(w, http.StatusOK, map[string]any{"documents": out, "next_cursor": page.NextCursor})
			return
		}
		doc, err := s.Store.GetDocument(r.Context(), site, collection, docID)
		if err != nil {
			s.writeStoreError(w, err)
			return
		}
		w.Header().Set("ETag", documentETag(doc))
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
		w.Header().Set("ETag", documentETag(doc))
		writeJSON(w, http.StatusCreated, out)
	case http.MethodPut:
		if docID == "" {
			http.NotFound(w, r)
			return
		}
		if documentWildcardPrecondition(r) {
			http.Error(w, "wildcard revision precondition is not supported", http.StatusBadRequest)
			return
		}
		body, err := readJSONBody(w, r)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadRequest)
			return
		}
		var doc store.Document
		if documentPreconditionPresent(r) {
			current, err := s.Store.GetDocument(r.Context(), site, collection, docID)
			if err != nil {
				s.writeStoreError(w, err)
				return
			}
			if !documentPreconditionMatches(r, current) {
				writeRevisionConflict(w, current)
				return
			}
			doc, err = s.Store.PutDocumentIfUnchanged(r.Context(), site, collection, docID, string(body), actor, current.UpdatedAt, current.DataJSON)
		} else {
			doc, err = s.Store.PutDocument(r.Context(), site, collection, docID, string(body), actor)
		}
		if err != nil {
			s.writeDocumentWriteError(w, r, site, collection, docID, err)
			return
		}
		out := documentJSON(doc)
		s.Realtime.Publish(site, "db:"+collection, "update", out)
		w.Header().Set("ETag", documentETag(doc))
		writeJSON(w, http.StatusOK, out)
	case http.MethodPatch:
		if docID == "" {
			http.NotFound(w, r)
			return
		}
		if documentWildcardPrecondition(r) {
			http.Error(w, "wildcard revision precondition is not supported", http.StatusBadRequest)
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
		if documentPreconditionPresent(r) && !documentPreconditionMatches(r, old) {
			writeRevisionConflict(w, old)
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
		var doc store.Document
		if documentPreconditionPresent(r) {
			doc, err = s.Store.PutDocumentIfUnchanged(r.Context(), site, collection, docID, string(body), actor, old.UpdatedAt, old.DataJSON)
		} else {
			doc, err = s.Store.PutDocument(r.Context(), site, collection, docID, string(body), actor)
		}
		if err != nil {
			s.writeDocumentWriteError(w, r, site, collection, docID, err)
			return
		}
		out := documentJSON(doc)
		s.Realtime.Publish(site, "db:"+collection, "update", out)
		w.Header().Set("ETag", documentETag(doc))
		writeJSON(w, http.StatusOK, out)
	case http.MethodDelete:
		if docID == "" {
			http.NotFound(w, r)
			return
		}
		if documentWildcardPrecondition(r) {
			http.Error(w, "wildcard revision precondition is not supported", http.StatusBadRequest)
			return
		}
		var err error
		if documentPreconditionPresent(r) {
			old, err := s.Store.GetDocument(r.Context(), site, collection, docID)
			if err != nil {
				s.writeStoreError(w, err)
				return
			}
			if !documentPreconditionMatches(r, old) {
				writeRevisionConflict(w, old)
				return
			}
			err = s.Store.DeleteDocumentIfUnchanged(r.Context(), site, collection, docID, old.UpdatedAt, old.DataJSON)
		} else {
			err = s.Store.DeleteDocument(r.Context(), site, collection, docID)
		}
		if err != nil {
			s.writeDocumentWriteError(w, r, site, collection, docID, err)
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

func parseListLimit(raw string) (int, error) {
	if raw == "" {
		return 100, nil
	}
	limit, err := strconv.Atoi(raw)
	if err != nil || limit < 1 {
		return 0, fmt.Errorf("limit must be a positive integer")
	}
	if limit > 500 {
		limit = 500
	}
	return limit, nil
}

type documentSortSpec struct {
	Key  string
	Desc bool
}

type filteredDocumentCursor struct {
	Key   string `json:"key"`
	Desc  bool   `json:"desc"`
	Value string `json:"value"`
	ID    string `json:"id"`
}

func parseDocumentSort(sortKey string) (documentSortSpec, error) {
	key := strings.TrimSpace(sortKey)
	if key == "" {
		key = "created_at"
	}
	desc := strings.HasPrefix(key, "-")
	key = strings.TrimPrefix(key, "-")
	if key != "created_at" && key != "updated_at" && key != "id" {
		return documentSortSpec{}, fmt.Errorf("unsupported sort key")
	}
	return documentSortSpec{Key: key, Desc: desc}, nil
}

func filterSortDocuments(docs []store.Document, filterRaw string, spec documentSortSpec) ([]store.Document, error) {
	filtered := docs
	if strings.TrimSpace(filterRaw) != "" {
		var filters map[string]any
		if err := json.Unmarshal([]byte(filterRaw), &filters); err != nil {
			return nil, fmt.Errorf("filter must be a JSON object")
		}
		for _, v := range filters {
			if _, nested := v.(map[string]any); nested {
				return nil, fmt.Errorf("unsupported filter operator")
			}
		}
		filtered = filtered[:0]
		for _, d := range docs {
			var data map[string]any
			if err := json.Unmarshal([]byte(d.DataJSON), &data); err != nil {
				continue
			}
			match := true
			for k, want := range filters {
				if fmt.Sprint(data[k]) != fmt.Sprint(want) {
					match = false
					break
				}
			}
			if match {
				filtered = append(filtered, d)
			}
		}
	}
	sort.SliceStable(filtered, func(i, j int) bool {
		a, b := documentSortValue(filtered[i], spec.Key), documentSortValue(filtered[j], spec.Key)
		if a == b {
			a, b = filtered[i].ID, filtered[j].ID
		}
		if spec.Desc {
			return a > b
		}
		return a < b
	})
	return filtered, nil
}

func pageFilteredDocuments(docs []store.Document, limit int, cursor string, spec documentSortSpec) ([]store.Document, string, error) {
	start := 0
	if cursor != "" {
		cur, err := decodeFilteredDocumentCursor(cursor, spec)
		if err != nil {
			return nil, "", err
		}
		for start < len(docs) && !documentAfterFilteredCursor(docs[start], cur, spec) {
			start++
		}
	}
	remaining := docs[start:]
	if len(remaining) <= limit {
		return remaining, "", nil
	}
	page := remaining[:limit]
	return page, encodeFilteredDocumentCursor(spec, page[len(page)-1]), nil
}

func encodeFilteredDocumentCursor(spec documentSortSpec, d store.Document) string {
	body, _ := json.Marshal(filteredDocumentCursor{Key: spec.Key, Desc: spec.Desc, Value: documentSortValue(d, spec.Key), ID: d.ID})
	return base64.RawURLEncoding.EncodeToString(body)
}

func decodeFilteredDocumentCursor(cursor string, spec documentSortSpec) (filteredDocumentCursor, error) {
	raw, err := base64.RawURLEncoding.DecodeString(cursor)
	if err != nil {
		return filteredDocumentCursor{}, store.ErrInvalidCursor
	}
	var cur filteredDocumentCursor
	if err := json.Unmarshal(raw, &cur); err != nil {
		return filteredDocumentCursor{}, store.ErrInvalidCursor
	}
	if cur.Key != spec.Key || cur.Desc != spec.Desc || cur.Value == "" || cur.ID == "" {
		return filteredDocumentCursor{}, store.ErrInvalidCursor
	}
	return cur, nil
}

func documentAfterFilteredCursor(d store.Document, cur filteredDocumentCursor, spec documentSortSpec) bool {
	value := documentSortValue(d, spec.Key)
	if value == cur.Value {
		if spec.Desc {
			return d.ID < cur.ID
		}
		return d.ID > cur.ID
	}
	if spec.Desc {
		return value < cur.Value
	}
	return value > cur.Value
}

func documentSortValue(d store.Document, key string) string {
	switch key {
	case "updated_at":
		return d.UpdatedAt
	case "id":
		return d.ID
	default:
		return d.CreatedAt
	}
}

func documentRevision(d store.Document) string {
	sum := sha256.Sum256([]byte(d.ID + "\x00" + d.UpdatedAt + "\x00" + d.DataJSON))
	return hex.EncodeToString(sum[:])
}

func documentETag(d store.Document) string {
	return `"` + documentRevision(d) + `"`
}

func documentPreconditionPresent(r *http.Request) bool {
	return strings.TrimSpace(r.Header.Get("If-Match")) != "" || strings.TrimSpace(r.URL.Query().Get("revision")) != ""
}

func documentPreconditionValue(r *http.Request) string {
	want := strings.TrimSpace(r.Header.Get("If-Match"))
	if want == "" {
		want = strings.TrimSpace(r.URL.Query().Get("revision"))
	}
	return want
}

func documentWildcardPrecondition(r *http.Request) bool {
	return documentPreconditionValue(r) == "*"
}

func documentPreconditionMatches(r *http.Request, d store.Document) bool {
	want := documentPreconditionValue(r)
	if want == "" {
		return true
	}
	rev := documentRevision(d)
	return want == rev || strings.Trim(want, `"`) == rev
}

func (s *Server) writeDocumentWriteError(w http.ResponseWriter, r *http.Request, site, collection, docID string, err error) {
	if errors.Is(err, store.ErrRevisionMismatch) {
		if current, getErr := s.Store.GetDocument(r.Context(), site, collection, docID); getErr == nil {
			writeRevisionConflict(w, current)
			return
		}
		writeRevisionMismatch(w)
		return
	}
	s.writeStoreError(w, err)
}

func writeRevisionConflict(w http.ResponseWriter, d store.Document) {
	writeJSON(w, http.StatusConflict, map[string]any{"error": "revision mismatch", "code": "revision_mismatch", "current_revision": documentRevision(d)})
}

func writeRevisionMismatch(w http.ResponseWriter) {
	writeJSON(w, http.StatusConflict, map[string]any{"error": "revision mismatch", "code": "revision_mismatch"})
}

func documentJSON(d store.Document) map[string]any {
	var data any
	if err := json.Unmarshal([]byte(d.DataJSON), &data); err != nil {
		data = nil
	}
	return map[string]any{"id": d.ID, "data": data, "revision": documentRevision(d), "created_by": d.CreatedBy, "updated_by": d.UpdatedBy, "created_at": d.CreatedAt, "updated_at": d.UpdatedAt}
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
	case http.MethodGet, http.MethodHead:
		if upID == "" {
			if r.Method == http.MethodHead {
				http.NotFound(w, r)
				return
			}
			limit, err := parseListLimit(r.URL.Query().Get("limit"))
			if err != nil {
				http.Error(w, err.Error(), http.StatusBadRequest)
				return
			}
			page, err := s.Store.ListUploadsPage(r.Context(), site, limit, r.URL.Query().Get("cursor"))
			if err != nil {
				s.writeStoreError(w, err)
				return
			}
			out := make([]map[string]any, 0, len(page.Uploads))
			for _, u := range page.Uploads {
				out = append(out, uploadJSON(u))
			}
			writeJSON(w, http.StatusOK, map[string]any{"uploads": out, "next_cursor": page.NextCursor})
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
	if errors.Is(err, store.ErrInvalidCursor) {
		http.Error(w, "invalid cursor", http.StatusBadRequest)
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
