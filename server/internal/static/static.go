package static

import (
	"bytes"
	"context"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"html/template"
	"io"
	"mime"
	"net"
	"net/http"
	"net/http/httputil"
	"net/netip"
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
	"openquick.dev/quickd/internal/deploy"
	"openquick.dev/quickd/internal/identity"
	"openquick.dev/quickd/internal/sites"
	"openquick.dev/quickd/internal/store"
)

type Handler struct {
	Config         config.Config
	Store          *store.Store
	API            http.Handler
	Adapter        identity.Adapter
	DevDir         string
	DevSite        string
	RemoteAPI      string
	RemoteAPIToken string
}

func New(cfg config.Config, st *store.Store, adapter identity.Adapter, apiHandler http.Handler) *Handler {
	cfg.ApplyDefaults()
	return &Handler{Config: cfg, Store: st, Adapter: adapter, API: apiHandler}
}

func (h *Handler) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	devProxyToken := strings.TrimSpace(r.Header.Get("X-Quick-Dev-Token"))
	stripQuickHeaders(r)
	if r.URL.Path == "/_quick/health" {
		if h.remoteAPIEnabled() && h.shouldProxyQuickPath(r.URL.Path) {
			h.proxyRemoteAPI(w, r, r.URL.Path)
			return
		}
		h.API.ServeHTTP(w, r)
		return
	}
	if r.URL.Path == "/_quick/sdk.js" {
		// The SDK is site-independent. Serve it at the root as well as under
		// /~/site so examples using /_quick/sdk.js keep working on path-fallback
		// hosts; the SDK discovers the active /~/site API prefix in the browser.
		r = r.WithContext(identity.WithIdentity(api.WithSite(r.Context(), api.SiteContext{Name: "_sdk"}), identity.Anonymous()))
		h.API.ServeHTTP(w, r)
		return
	}
	if r.URL.Path == "/_quick/domains/ask" {
		h.handleDomainAsk(w, r)
		return
	}
	if strings.HasPrefix(r.URL.Path, "/_quick/deploy/") && h.isApexHost(r.Host) {
		h.handleHTTPDeploy(w, r)
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
		h.serveDirectory(w, r.WithContext(identity.WithIdentity(r.Context(), id)), id)
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
	isQuick := strings.HasPrefix(stripped, "/_quick/") || stripped == "/_quick"
	if isQuick && h.remoteAPIEnabled() && h.shouldProxyQuickPath(stripped) {
		r2 := cloneWithPath(r, stripped)
		h.proxyRemoteAPI(w, r2, stripped)
		return
	}
	publicStatic := h.publicStaticAllowed(r, site, isQuick)
	var id *identity.Identity
	var err error
	if isQuick && devProxyToken != "" && h.Config.DevProxy.Enabled {
		id, err = h.devProxyIdentity(r.Context(), site, devProxyToken)
		if err != nil {
			api.ErrorFromIdentity(w, err)
			return
		}
	} else if publicStatic {
		id = identity.Anonymous()
	} else {
		id, err = h.authenticateViewer(r)
		if err != nil {
			api.ErrorFromIdentity(w, err)
			return
		}
		if isQuick && h.sitePublic(site) && (id == nil || !id.Authenticated) {
			api.ErrorFromIdentity(w, identity.ErrAnonymousNotAllowed)
			return
		}
	}
	r = r.WithContext(identity.WithIdentity(api.WithSite(r.Context(), api.SiteContext{Name: site, Dev: h.DevDir != ""}), id))
	if isQuick {
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

func (h *Handler) remoteAPIEnabled() bool {
	return h.DevDir != "" && strings.TrimSpace(h.RemoteAPI) != "" && strings.TrimSpace(h.RemoteAPIToken) != ""
}

func (h *Handler) shouldProxyQuickPath(p string) bool {
	clean := path.Clean("/" + p)
	return strings.HasPrefix(clean, "/_quick/") && clean != "/_quick/sdk.js"
}

func (h *Handler) proxyRemoteAPI(w http.ResponseWriter, r *http.Request, stripped string) {
	target, err := url.Parse(strings.TrimSpace(h.RemoteAPI))
	if err != nil || target.Scheme == "" || target.Host == "" {
		http.Error(w, "remote api unavailable", http.StatusBadGateway)
		return
	}
	proxy := &httputil.ReverseProxy{
		Director: func(req *http.Request) {
			req.URL.Scheme = target.Scheme
			req.URL.Host = target.Host
			req.URL.Path = singleJoiningSlash(target.Path, path.Clean("/"+stripped))
			req.URL.RawPath = ""
			req.Host = target.Host
			req.Header.Set("X-Quick-Dev-Token", h.RemoteAPIToken)
		},
		ErrorHandler: func(w http.ResponseWriter, r *http.Request, err error) {
			http.Error(w, err.Error(), http.StatusBadGateway)
		},
	}
	proxy.ServeHTTP(w, r)
}

func singleJoiningSlash(a, b string) string {
	aslash := strings.HasSuffix(a, "/")
	bslash := strings.HasPrefix(b, "/")
	switch {
	case aslash && bslash:
		return a + b[1:]
	case !aslash && !bslash:
		return a + "/" + b
	default:
		return a + b
	}
}

func (h *Handler) devProxyIdentity(ctx context.Context, site, token string) (*identity.Identity, error) {
	if h.Store == nil {
		return nil, identity.ErrProviderUnavailable
	}
	_, ok, err := h.Store.ValidateDevToken(ctx, site, token, time.Now())
	if err != nil {
		return nil, fmt.Errorf("%w: %v", identity.ErrProviderUnavailable, err)
	}
	if !ok {
		return nil, identity.ErrInvalidCredential
	}
	return &identity.Identity{Authenticated: true, Provider: "dev-proxy", Subject: "dev-token:" + site}, nil
}

func (h *Handler) route(r *http.Request) (site, stripped string, ok bool) {
	if s, p, found := sites.SplitPathFallback(r.URL.Path); found {
		return s, p, true
	}
	if s, found := h.siteFromHost(r.Host); found {
		return s, r.URL.Path, true
	}
	if h.DevDir != "" && h.DevSite != "" {
		return h.DevSite, r.URL.Path, true
	}
	return "", "", false
}

func (h *Handler) siteFromHost(host string) (string, bool) {
	hostName := normalizedHostName(host)
	if hostName == "" {
		return "", false
	}
	if h.Store != nil {
		if site, err := h.Store.SiteForDomain(context.Background(), hostName); err == nil && site != "" {
			return site, true
		}
	}
	if label, found := sites.SiteFromHost(host, h.Config); found {
		if h.Store != nil {
			if rec, err := h.Store.GetSiteBySubdomain(context.Background(), label); err == nil && rec.Name != "" {
				return rec.Name, true
			}
		}
		return label, true
	}
	return "", false
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

func (h *Handler) sitePublic(site string) bool {
	if h.Store == nil || !h.Config.PublicStatic.Enabled {
		return false
	}
	pub, err := h.Store.IsSitePublic(context.Background(), site)
	return err == nil && pub
}

func (h *Handler) publicStaticAllowed(r *http.Request, site string, isQuick bool) bool {
	if isQuick || r.Method != http.MethodGet && r.Method != http.MethodHead {
		return false
	}
	return h.sitePublic(site)
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
	Public    bool
}

type directoryData struct {
	Sites             []directoryRow
	DeployPanel       bool
	HTTPDeployEnabled bool
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
.badge { display: inline-block; padding: .12rem .45rem; border-radius: 999px; background: color-mix(in srgb, LinkText 16%, transparent); font-size: .78rem; font-weight: 700; }
.panel { margin: 0 0 1.5rem; padding: 1rem; border: 1px solid color-mix(in srgb, CanvasText 16%, transparent); border-radius: 16px; }
.panel input { margin: .25rem .5rem .25rem 0; }
</style>
</head>
<body>
<main>
<h1>OpenQuick sites</h1>
<p>Every visible site can teach the next teammate what is possible.</p>
{{if .DeployPanel}}
<section class="panel" aria-label="Deploy ZIP">
<strong>Deploy a ZIP</strong>
<form method="post" enctype="multipart/form-data" action="/_quick/deploy/">
<input name="site" placeholder="site-name" pattern="[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?" required>
<input type="file" name="zip" accept=".zip,application/zip" required>
<button type="submit">Deploy</button>
</form>
<p>Drag-drop clients can POST a ZIP to <code>/_quick/deploy/&lt;site&gt;</code>; add <code>?confirm=&lt;site&gt;</code> when overwrite confirmation is required.</p>
</section>
{{end}}
{{if .Sites}}
<table>
<thead><tr><th>Site</th><th>Release</th><th>Updated</th><th>Deployer</th><th>Visibility</th></tr></thead>
<tbody>
{{range .Sites}}
<tr><td><a href="{{.URL}}">{{.Name}}</a></td><td><code>{{.Release}}</code></td><td>{{.UpdatedAt}}</td><td>{{.Deployer}}</td><td>{{if .Public}}<span class="badge">public</span>{{else}}private{{end}}</td></tr>
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

func (h *Handler) serveDirectory(w http.ResponseWriter, r *http.Request, id *identity.Identity) {
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
	data := directoryData{Sites: make([]directoryRow, 0, len(recs)), HTTPDeployEnabled: h.Config.HTTPDeploy.Enabled, DeployPanel: h.Config.HTTPDeploy.Enabled && h.portalIdentityAuthorized(id)}
	for _, rec := range recs {
		data.Sites = append(data.Sites, directoryRow{
			Name:      rec.Name,
			URL:       sites.URLForSubdomain(rec.Name, rec.Subdomain, h.Config),
			Release:   displayOrDash(rec.Release),
			UpdatedAt: displayOrDash(rec.UpdatedAt),
			Deployer:  displayOrDash(rec.Deployer),
			Public:    rec.Public,
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

func (h *Handler) handleDomainAsk(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.NotFound(w, r)
		return
	}
	if !h.trustedPeer(r.RemoteAddr) || h.Store == nil {
		http.NotFound(w, r)
		return
	}
	domain, err := sites.ValidateDomain(r.URL.Query().Get("domain"), h.Config)
	if err != nil {
		http.NotFound(w, r)
		return
	}
	if _, err := h.Store.SiteForDomain(r.Context(), domain); err != nil {
		http.NotFound(w, r)
		return
	}
	w.Header().Set("Cache-Control", "no-cache")
	w.WriteHeader(http.StatusOK)
}

func (h *Handler) trustedPeer(remote string) bool {
	host, _, err := net.SplitHostPort(remote)
	if err != nil {
		host = remote
	}
	addr, err := netip.ParseAddr(strings.Trim(host, "[]"))
	if err != nil {
		return false
	}
	if addr.IsLoopback() {
		return true
	}
	for _, raw := range h.Config.IAP.TrustedProxies {
		raw = strings.TrimSpace(raw)
		if raw == "" {
			continue
		}
		if p, err := netip.ParsePrefix(raw); err == nil && p.Contains(addr) {
			return true
		}
		if a, err := netip.ParseAddr(raw); err == nil && a == addr {
			return true
		}
	}
	return false
}

func (h *Handler) handleHTTPDeploy(w http.ResponseWriter, r *http.Request) {
	if !h.Config.HTTPDeploy.Enabled {
		http.NotFound(w, r)
		return
	}
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if !h.originOK(r) {
		writeJSON(w, http.StatusForbidden, map[string]any{"error": "origin_denied"})
		return
	}
	site := strings.Trim(strings.TrimPrefix(r.URL.Path, "/_quick/deploy/"), "/")
	if err := sites.ValidateSiteName(site, h.Config.Deploy.ReservedNames); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": err.Error()})
		return
	}
	id, tokenOK, err := h.authorizeHTTPDeploy(r)
	if err != nil {
		api.ErrorFromIdentity(w, err)
		return
	}
	if !tokenOK && !h.portalIdentityAuthorized(id) {
		writeJSON(w, http.StatusForbidden, map[string]any{"error": "deploy_not_authorized"})
		return
	}
	deployerName := "http:token"
	if id != nil && id.Authenticated {
		deployerName = identity.SubjectKey(id)
	}
	zipPath, err := h.saveDeployZip(w, r)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": err.Error()})
		return
	}
	defer os.Remove(zipPath)
	svc := deploy.New(h.Config, h.Store)
	prep, err := svc.PrepareWithOptions(r.Context(), site, deploy.PrepareOptions{Deployer: deployerName})
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": err.Error()})
		return
	}
	if prep.LastDeployer != nil && *prep.LastDeployer != "" && *prep.LastDeployer != deployerName && r.URL.Query().Get("confirm") != site {
		h.cleanupIncoming(site, prep.DeployID)
		writeJSON(w, http.StatusConflict, map[string]any{"error": "confirm_overwrite", "deployed_by": *prep.LastDeployer, "last_deployer": prep.LastDeployer, "last_release": prep.LastRelease, "last_deployed_at": prep.LastDeployedAt})
		return
	}
	if err := deploy.ExtractZip(zipPath, prep.StagingPath, deploy.DefaultZipCaps()); err != nil {
		h.cleanupIncoming(site, prep.DeployID)
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": err.Error()})
		return
	}
	act, err := svc.ActivateWithOptions(r.Context(), site, prep.DeployID, deploy.ActivateOptions{Deployer: deployerName})
	if err != nil {
		h.cleanupIncoming(site, prep.DeployID)
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": err.Error()})
		return
	}
	writeJSON(w, http.StatusOK, act)
}

func (h *Handler) originOK(r *http.Request) bool {
	origin := strings.TrimSpace(r.Header.Get("Origin"))
	if origin == "" {
		return true
	}
	u, err := url.Parse(origin)
	if err != nil || u.Host == "" {
		return false
	}
	return strings.EqualFold(normalizedHostName(u.Host), normalizedHostName(r.Host))
}

func (h *Handler) authorizeHTTPDeploy(r *http.Request) (*identity.Identity, bool, error) {
	if h.deployBearerOK(r) {
		return nil, true, nil
	}
	id, err := h.authenticateViewer(r)
	if err != nil {
		return nil, false, err
	}
	return id, false, nil
}

func (h *Handler) deployBearerOK(r *http.Request) bool {
	auth := strings.TrimSpace(r.Header.Get("Authorization"))
	if !strings.HasPrefix(strings.ToLower(auth), "bearer ") {
		return false
	}
	token := strings.TrimSpace(auth[len("Bearer "):])
	if token == "" {
		return false
	}
	sum := sha256.Sum256([]byte(token))
	hexSum := strings.ToLower(hex.EncodeToString(sum[:]))
	for _, want := range h.Config.HTTPDeploy.Tokens {
		want = strings.ToLower(strings.TrimSpace(want))
		if len(want) == len(hexSum) && subtle.ConstantTimeCompare([]byte(want), []byte(hexSum)) == 1 {
			return true
		}
	}
	return false
}

func (h *Handler) portalIdentityAuthorized(id *identity.Identity) bool {
	if id == nil || !id.Authenticated {
		return false
	}
	allowed := h.Config.HTTPDeploy.AllowIdentities
	if len(allowed) == 0 {
		return false
	}
	candidates := []string{id.Subject, id.Email, id.Login}
	for _, want := range allowed {
		want = strings.TrimSpace(want)
		if want == "" {
			continue
		}
		for _, got := range candidates {
			if got != "" && got == want {
				return true
			}
		}
	}
	return false
}

func (h *Handler) saveDeployZip(w http.ResponseWriter, r *http.Request) (string, error) {
	const maxZipBody = 55 << 20
	r.Body = http.MaxBytesReader(w, r.Body, maxZipBody)
	f, err := os.CreateTemp("", "openquick-deploy-*.zip")
	if err != nil {
		return "", err
	}
	path := f.Name()
	ok := false
	defer func() {
		f.Close()
		if !ok {
			os.Remove(path)
		}
	}()
	media, _, _ := mime.ParseMediaType(r.Header.Get("Content-Type"))
	if strings.HasPrefix(media, "multipart/") {
		mr, err := r.MultipartReader()
		if err != nil {
			return "", err
		}
		found := false
		for {
			part, err := mr.NextPart()
			if errors.Is(err, io.EOF) {
				break
			}
			if err != nil {
				return "", err
			}
			if part.FormName() != "zip" {
				part.Close()
				continue
			}
			if _, err := io.Copy(f, io.LimitReader(part, maxZipBody)); err != nil {
				part.Close()
				return "", err
			}
			part.Close()
			found = true
			break
		}
		if !found {
			return "", fmt.Errorf("multipart field %q is required", "zip")
		}
	} else {
		if _, err := io.Copy(f, r.Body); err != nil {
			return "", err
		}
	}
	ok = true
	return path, nil
}

func (h *Handler) cleanupIncoming(site, deployID string) {
	_ = os.RemoveAll(filepath.Join(sites.IncomingDir(h.Config.RemoteRoot, site), deployID))
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
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
	if rel == "" {
		rel = "."
	}
	if rel != "." && containsDotfile(rel) {
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
		dirRel := rel
		indexRel := "index.html"
		if dirRel != "." {
			indexRel = path.Join(dirRel, "index.html")
		}
		idx, idxStat, idxErr := safeOpen(root, indexRel)
		if idxErr == nil && !idxStat.IsDir() {
			defer idx.Close()
			serveOpened(w, r, indexRel, idx, idxStat)
			return
		}
		if idx != nil {
			idx.Close()
		}
		cfg := h.siteConfigForRequest(site, root)
		if cfg.Routing.DirectoryListing {
			h.serveDirectoryListing(w, r, root, dirRel, file)
			return
		}
		if h.trySPAFallback(w, r, site, root) {
			return
		}
		http.NotFound(w, r)
		return
	}
	serveOpened(w, r, rel, file, stat)
}

func (h *Handler) trySPAFallback(w http.ResponseWriter, r *http.Request, site, root string) bool {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		return false
	}
	cfg := h.siteConfigForRequest(site, root)
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

func (h *Handler) siteConfigForRequest(site, root string) sites.SiteConfig {
	var cfg sites.SiteConfig
	if h.DevDir != "" && site == h.DevSite {
		cfg, _ = sites.ReadSiteConfig(h.DevDir)
	} else {
		cfg, _ = sites.ReadSiteConfig(sites.SiteDir(h.Config.RemoteRoot, site))
	}
	if releaseCfg, err := sites.ReadReleaseConfig(root); err == nil {
		cfg = sites.MergeSiteConfig(cfg, releaseCfg)
	}
	return cfg
}

type listingEntry struct {
	Name string
	URL  string
	Dir  bool
}

type listingData struct {
	Path    string
	Entries []listingEntry
}

var listingTemplate = template.Must(template.New("listing").Parse(`<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Index of {{.Path}}</title>
<style>:root{font-family:ui-sans-serif,system-ui,sans-serif;color-scheme:light dark}body{margin:2rem}a{color:LinkText;font-weight:700}ul{line-height:1.8}</style>
</head><body><main><h1>Index of {{.Path}}</h1><ul>{{range .Entries}}<li><a href="{{.URL}}">{{.Name}}{{if .Dir}}/{{end}}</a></li>{{else}}<li>No files.</li>{{end}}</ul></main></body></html>`))

func (h *Handler) serveDirectoryListing(w http.ResponseWriter, r *http.Request, root, rel string, dir *os.File) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	entries, err := dir.ReadDir(-1)
	if err != nil {
		http.Error(w, "forbidden", http.StatusForbidden)
		return
	}
	data := listingData{Path: "/" + strings.Trim(strings.TrimPrefix(rel, "."), "/")}
	if data.Path == "/" {
		data.Path = "/"
	}
	for _, e := range entries {
		name := e.Name()
		if strings.HasPrefix(name, ".") {
			continue
		}
		entryRel := name
		if rel != "." {
			entryRel = path.Join(rel, name)
		}
		if containsDotfile(entryRel) {
			continue
		}
		opened, _, err := safeOpen(root, entryRel)
		if err != nil {
			continue
		}
		opened.Close()
		urlName := url.PathEscape(name)
		if e.IsDir() {
			urlName += "/"
		}
		data.Entries = append(data.Entries, listingEntry{Name: name, URL: urlName, Dir: e.IsDir()})
	}
	var buf bytes.Buffer
	if err := listingTemplate.Execute(&buf, data); err != nil {
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
