package static

import (
	"bytes"
	"errors"
	"html/template"
	"net"
	"net/http"
	"net/url"
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
	if h.isDirectoryRequest(r) {
		if !h.Config.Directory.Enabled {
			http.NotFound(w, r)
			return
		}
		id, err := h.authenticateViewer(r)
		if err != nil {
			api.ErrorFromIdentity(w, err)
			return
		}
		h.serveDirectory(w, r.WithContext(identity.WithIdentity(r.Context(), id)))
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
	id, err := h.authenticateViewer(r)
	if err != nil {
		api.ErrorFromIdentity(w, err)
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

func (h *Handler) authenticateViewer(r *http.Request) (*identity.Identity, error) {
	id, err := h.authenticate(r)
	if err != nil {
		return nil, err
	}
	if (h.Config.Viewer.RequireIdentity || !h.Config.Viewer.AllowAnonymous) && (id == nil || !id.Authenticated) {
		return nil, identity.ErrAnonymousNotAllowed
	}
	return id, nil
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

func (h *Handler) isDirectoryRequest(r *http.Request) bool {
	clean := path.Clean("/" + r.URL.Path)
	switch clean {
	case "/":
		return h.isApexHost(r.Host)
	case "/~":
		return h.isBaseHost(r.Host)
	default:
		return false
	}
}

func (h *Handler) isApexHost(host string) bool {
	if _, found := sites.SiteFromHost(host, h.Config); found {
		return false
	}
	return h.isBaseHost(host)
}

func (h *Handler) isBaseHost(host string) bool {
	hostName := normalizedHostName(host)
	if hostName == "" {
		return false
	}
	base := strings.ToLower(strings.Trim(strings.TrimSuffix(h.Config.PublicBaseDomain, "."), "."))
	if base != "" && hostName == base {
		return true
	}
	if h.Config.BaseURL != "" {
		if u, err := url.Parse(h.Config.BaseURL); err == nil && normalizedHostName(u.Host) == hostName {
			return true
		}
	}
	return hostName == "localhost"
}

func normalizedHostName(host string) string {
	h, _, err := net.SplitHostPort(host)
	if err != nil {
		h = host
	}
	h = strings.Trim(h, "[]")
	h = strings.TrimSuffix(h, ".")
	return strings.ToLower(h)
}

type directoryRow struct {
	Name      string
	URL       string
	Release   string
	UpdatedAt string
	Deployer  string
}

type directoryData struct {
	Sites []directoryRow
}

var directoryTemplate = template.Must(template.New("directory").Parse(`<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>OpenQuick site directory</title>
<style>
:root { color-scheme: light dark; font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; }
body { margin: 0; padding: 2rem; background: Canvas; color: CanvasText; }
main { max-width: 72rem; margin: 0 auto; }
h1 { margin: 0 0 .25rem; font-size: clamp(2rem, 6vw, 4rem); letter-spacing: -0.05em; }
p { color: color-mix(in srgb, CanvasText 70%, transparent); margin: 0 0 1.5rem; }
table { width: 100%; border-collapse: collapse; background: color-mix(in srgb, Canvas 96%, CanvasText); border: 1px solid color-mix(in srgb, CanvasText 16%, transparent); border-radius: 16px; overflow: hidden; }
th, td { padding: .75rem 1rem; text-align: left; border-bottom: 1px solid color-mix(in srgb, CanvasText 12%, transparent); vertical-align: top; }
th { font-size: .78rem; text-transform: uppercase; letter-spacing: .08em; color: color-mix(in srgb, CanvasText 62%, transparent); }
tr:last-child td { border-bottom: 0; }
a { color: LinkText; font-weight: 700; text-decoration-thickness: .08em; text-underline-offset: .18em; }
.empty { padding: 1rem; border: 1px dashed color-mix(in srgb, CanvasText 25%, transparent); border-radius: 12px; }
code { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: .9em; }
</style>
</head>
<body>
<main>
<h1>OpenQuick sites</h1>
<p>Every visible site can teach the next teammate what is possible.</p>
{{if .Sites}}
<table>
<thead><tr><th>Site</th><th>Release</th><th>Updated</th><th>Deployer</th></tr></thead>
<tbody>
{{range .Sites}}
<tr><td><a href="{{.URL}}">{{.Name}}</a></td><td><code>{{.Release}}</code></td><td>{{.UpdatedAt}}</td><td>{{.Deployer}}</td></tr>
{{end}}
</tbody>
</table>
{{else}}
<p class="empty">No sites have been deployed yet.</p>
{{end}}
</main>
</body>
</html>
`))

func (h *Handler) serveDirectory(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if h.Store == nil {
		http.Error(w, "store unavailable", http.StatusServiceUnavailable)
		return
	}
	recs, err := h.Store.ListSites(r.Context())
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	data := directoryData{Sites: make([]directoryRow, 0, len(recs))}
	for _, rec := range recs {
		data.Sites = append(data.Sites, directoryRow{
			Name:      rec.Name,
			URL:       sites.URLFor(rec.Name, h.Config),
			Release:   displayOrDash(rec.Release),
			UpdatedAt: displayOrDash(rec.UpdatedAt),
			Deployer:  displayOrDash(rec.Deployer),
		})
	}
	var buf bytes.Buffer
	if err := directoryTemplate.Execute(&buf, data); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	if r.Method == http.MethodHead {
		w.WriteHeader(http.StatusOK)
		return
	}
	_, _ = w.Write(buf.Bytes())
}

func displayOrDash(s string) string {
	if strings.TrimSpace(s) == "" {
		return "—"
	}
	return s
}

func (h *Handler) setStaticCORS(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		return
	}
	origin := strings.TrimSpace(r.Header.Get("Origin"))
	if origin == "" || !h.isSiblingSiteOrigin(origin, r.Host) {
		return
	}
	w.Header().Set("Access-Control-Allow-Origin", origin)
	addVary(w.Header(), "Origin")
	w.Header().Set("Access-Control-Allow-Credentials", "true")
}

func (h *Handler) isSiblingSiteOrigin(origin, requestHost string) bool {
	u, err := url.Parse(origin)
	if err != nil || u.Host == "" {
		return false
	}
	if u.Scheme != "http" && u.Scheme != "https" {
		return false
	}
	if _, found := sites.SiteFromHost(u.Host, h.Config); !found {
		return false
	}
	if _, found := sites.SiteFromHost(requestHost, h.Config); found {
		return sameSiteNamespace(u.Host, requestHost, h.Config)
	}
	return h.isBaseHost(requestHost) && sameSiteNamespace(u.Host, requestHost, h.Config)
}

func sameSiteNamespace(originHost, requestHost string, cfg config.Config) bool {
	originName, originPort := normalizedHostAndPort(originHost)
	requestName, requestPort := normalizedHostAndPort(requestHost)
	if !samePort(originPort, requestPort) {
		return false
	}
	if strings.HasSuffix(originName, ".localhost") && (requestName == "localhost" || strings.HasSuffix(requestName, ".localhost")) {
		return true
	}
	base := strings.ToLower(strings.Trim(strings.TrimSuffix(cfg.PublicBaseDomain, "."), "."))
	return base != "" && strings.HasSuffix(originName, "."+base) && (requestName == base || strings.HasSuffix(requestName, "."+base))
}

func normalizedHostAndPort(host string) (string, string) {
	h, p, err := net.SplitHostPort(host)
	if err != nil {
		return normalizedHostName(host), ""
	}
	return normalizedHostName(h), p
}

func samePort(a, b string) bool {
	if a == b {
		return true
	}
	if a == "" && (b == "80" || b == "443") {
		return true
	}
	return b == "" && (a == "80" || a == "443")
}

func addVary(h http.Header, value string) {
	for _, part := range strings.Split(h.Get("Vary"), ",") {
		if strings.EqualFold(strings.TrimSpace(part), value) {
			return
		}
	}
	if h.Get("Vary") == "" {
		h.Set("Vary", value)
		return
	}
	h.Set("Vary", h.Get("Vary")+", "+value)
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
	h.setStaticCORS(w, r)
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
