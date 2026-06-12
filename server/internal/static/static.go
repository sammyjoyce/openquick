package static

import (
	"errors"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"
	"syscall"
	"time"

	"openquick.dev/quickd/internal/api"
	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
	"openquick.dev/quickd/internal/sites"
	"openquick.dev/quickd/internal/store"
)

type Handler struct {
	Config  config.Config
	Store   *store.Store
	API     http.Handler
	Adapter identity.Adapter
	DevDir  string
	DevSite string
}

func New(cfg config.Config, st *store.Store, adapter identity.Adapter, apiHandler http.Handler) *Handler {
	cfg.ApplyDefaults()
	return &Handler{Config: cfg, Store: st, Adapter: adapter, API: apiHandler}
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	stripQuickHeaders(r)
	if r.URL.Path == "/_quick/health" {
		h.API.ServeHTTP(w, r)
		return
	}
	site, stripped, ok := h.route(r)
	if !ok || site == "" {
		http.NotFound(w, r)
		return
	}
	if err := sites.ValidateSiteName(site, h.Config.Deploy.ReservedNames); err != nil {
		http.NotFound(w, r)
		return
	}
	if !h.siteExists(site) {
		http.NotFound(w, r)
		return
	}
	id, err := h.authenticate(r)
	if err != nil {
		api.ErrorFromIdentity(w, err)
		return
	}
	if (h.Config.Viewer.RequireIdentity || !h.Config.Viewer.AllowAnonymous) && (id == nil || !id.Authenticated) {
		api.ErrorFromIdentity(w, identity.ErrAnonymousNotAllowed)
		return
	}
	r = r.WithContext(identity.WithIdentity(api.WithSite(r.Context(), api.SiteContext{Name: site, Dev: h.DevDir != ""}), id))
	if strings.HasPrefix(stripped, "/_quick/") || stripped == "/_quick" {
		r2 := cloneWithPath(r, stripped)
		h.API.ServeHTTP(w, r2)
		return
	}
	h.serveStatic(w, r, site, stripped)
}

func stripQuickHeaders(r *http.Request) {
	for name := range r.Header {
		if strings.HasPrefix(strings.ToLower(name), "x-quick-") {
			delete(r.Header, name)
		}
	}
}

func (h *Handler) route(r *http.Request) (site, stripped string, ok bool) {
	if s, p, found := sites.SplitPathFallback(r.URL.Path); found {
		return s, p, true
	}
	if s, found := sites.SiteFromHost(r.Host, h.Config); found {
		return s, r.URL.Path, true
	}
	if h.DevDir != "" && h.DevSite != "" {
		return h.DevSite, r.URL.Path, true
	}
	return "", "", false
}

func (h *Handler) siteExists(site string) bool {
	if h.DevDir != "" && site == h.DevSite {
		return true
	}
	if _, err := os.Lstat(sites.CurrentPath(h.Config.RemoteRoot, site)); err == nil {
		return true
	}
	return false
}

func (h *Handler) authenticate(r *http.Request) (*identity.Identity, error) {
	if h.Adapter == nil {
		return identity.Anonymous(), nil
	}
	return h.Adapter.Authenticate(r.Context(), r)
}

func cloneWithPath(r *http.Request, p string) *http.Request {
	r2 := new(http.Request)
	*r2 = *r
	u := *r.URL
	u.Path = path.Clean("/" + p)
	if p == "/" {
		u.Path = "/"
	}
	u.RawPath = ""
	r2.URL = &u
	return r2
}

func (h *Handler) releaseRoot(site string) (string, error) {
	if h.DevDir != "" && site == h.DevSite {
		return filepath.Abs(h.DevDir)
	}
	current := sites.CurrentPath(h.Config.RemoteRoot, site)
	resolved, err := evalCurrentSymlink(current)
	if err != nil {
		return "", err
	}
	abs, err := filepath.Abs(resolved)
	if err != nil {
		return "", err
	}
	siteDir := sites.SiteDir(h.Config.RemoteRoot, site)
	if resolvedSiteDir, err := filepath.EvalSymlinks(siteDir); err == nil {
		siteDir = resolvedSiteDir
	}
	if !sites.PathWithin(siteDir, abs) {
		return "", errors.New("current symlink escapes site")
	}
	return abs, nil
}

func evalCurrentSymlink(current string) (string, error) {
	resolved, err := filepath.EvalSymlinks(current)
	if err == nil || !transientSymlinkSwapError(err) {
		return resolved, err
	}
	// Darwin/APFS can transiently report EINVAL/ENOENT while namei walks
	// through "current" as os.Rename atomically replaces that symlink dirent.
	// Retry once; persistent errors still surface to the caller.
	time.Sleep(time.Millisecond)
	return filepath.EvalSymlinks(current)
}

func transientSymlinkSwapError(err error) bool {
	return errors.Is(err, os.ErrNotExist) || errors.Is(err, syscall.EINVAL)
}

func (h *Handler) serveStatic(w http.ResponseWriter, r *http.Request, site, urlPath string) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if containsTraversal(r.URL.EscapedPath()) || strings.Contains(urlPath, "..") {
		http.Error(w, "bad path", http.StatusBadRequest)
		return
	}
	root, err := h.releaseRoot(site)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	rel := strings.TrimPrefix(path.Clean("/"+urlPath), "/")
	if rel == "." || rel == "" {
		rel = "index.html"
	}
	if containsDotfile(rel) {
		http.NotFound(w, r)
		return
	}
	file, stat, err := safeOpen(root, rel)
	if err != nil && errors.Is(err, os.ErrNotExist) {
		if h.trySPAFallback(w, r, site, root) {
			return
		}
		http.NotFound(w, r)
		return
	}
	if err != nil {
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}
	defer file.Close()
	if stat.IsDir() {
		file.Close()
		rel = path.Join(rel, "index.html")
		if containsDotfile(rel) {
			http.NotFound(w, r)
			return
		}
		file, stat, err = safeOpen(root, rel)
		if err != nil {
			if h.trySPAFallback(w, r, site, root) {
				return
			}
			http.NotFound(w, r)
			return
		}
		defer file.Close()
	}
	serveOpened(w, r, rel, file, stat)
}

func (h *Handler) trySPAFallback(w http.ResponseWriter, r *http.Request, site, root string) bool {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		return false
	}
	cfg, _ := sites.ReadSiteConfig(sites.SiteDir(h.Config.RemoteRoot, site))
	if h.DevDir != "" && site == h.DevSite {
		cfg, _ = sites.ReadSiteConfig(h.DevDir)
	}
	fb := sites.SPAFallbackPath(cfg)
	if fb == "" {
		return false
	}
	rel := strings.TrimPrefix(fb, "/")
	if containsDotfile(rel) {
		return false
	}
	file, stat, err := safeOpen(root, rel)
	if err != nil || stat.IsDir() {
		return false
	}
	defer file.Close()
	serveOpened(w, r, rel, file, stat)
	return true
}

func safeOpen(root, rel string) (*os.File, os.FileInfo, error) {
	rootAbs, err := filepath.Abs(root)
	if err != nil {
		return nil, nil, err
	}
	full := filepath.Join(rootAbs, filepath.FromSlash(rel))
	if !sites.PathWithin(rootAbs, full) {
		return nil, nil, errors.New("path escapes root")
	}
	resolved, err := filepath.EvalSymlinks(full)
	if err != nil {
		return nil, nil, err
	}
	if !sites.PathWithin(rootAbs, resolved) {
		return nil, nil, errors.New("symlink escapes root")
	}
	f, err := os.Open(resolved)
	if err != nil {
		return nil, nil, err
	}
	info, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, nil, err
	}
	return f, info, nil
}

func serveOpened(w http.ResponseWriter, r *http.Request, rel string, f *os.File, info os.FileInfo) {
	w.Header().Set("X-Content-Type-Options", "nosniff")
	if strings.EqualFold(filepath.Ext(rel), ".html") || strings.EqualFold(filepath.Base(rel), "index.html") {
		w.Header().Set("Cache-Control", "no-cache")
	} else if hashedAsset(rel) {
		w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	}
	http.ServeContent(w, r, filepath.Base(rel), info.ModTime(), f)
}

var hashAssetRE = regexp.MustCompile(`(?i)(?:^|[-.])[0-9a-f]{8,}(?:[-.]|$)`)

func hashedAsset(rel string) bool {
	base := filepath.Base(rel)
	return hashAssetRE.MatchString(base)
}

func containsDotfile(rel string) bool {
	for _, seg := range strings.Split(rel, "/") {
		if strings.HasPrefix(seg, ".") {
			return true
		}
	}
	return false
}

func containsTraversal(escaped string) bool {
	escaped = strings.ToLower(escaped)
	return strings.Contains(escaped, "%2e") || strings.Contains(escaped, "..")
}
