package sites

import (
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"

	"openquick.dev/quickd/internal/config"
)

var dnsLabelRE = regexp.MustCompile(`^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$`)
var deployIDRE = regexp.MustCompile(`^[0-9]{8}T[0-9]{6}Z-[0-9a-f]{6,}$`)

type SiteConfig struct {
	Schema    string        `json:"$schema,omitempty"`
	Name      string        `json:"name,omitempty"`
	Subdomain string        `json:"subdomain,omitempty"`
	Routing   RoutingConfig `json:"routing,omitempty"`
}

type RoutingConfig struct {
	SPAFallback string `json:"spa_fallback,omitempty"`
}

func ValidateSlug(slug string) error {
	if !dnsLabelRE.MatchString(slug) {
		return fmt.Errorf("invalid site slug %q: must be a lowercase DNS label", slug)
	}
	return nil
}

func ValidateSiteName(name string, reserved []string) error {
	if strings.HasPrefix(name, "_doctor-") {
		return nil
	}
	if err := ValidateSlug(name); err != nil {
		return err
	}
	return CheckReserved(name, reserved)
}

func ValidateDeployID(id string) error {
	if !deployIDRE.MatchString(id) {
		return fmt.Errorf("invalid deploy id %q", id)
	}
	return nil
}

func CheckReserved(name string, reserved []string) error {
	for _, r := range ReservedNames(reserved) {
		if name == r {
			return fmt.Errorf("site name %q is reserved", name)
		}
	}
	if strings.HasPrefix(name, "_") && !strings.HasPrefix(name, "_doctor-") {
		return fmt.Errorf("site names beginning with underscore are reserved")
	}
	return nil
}

func ReservedNames(configured []string) []string {
	seen := map[string]bool{}
	out := make([]string, 0, len(configured)+4)
	for _, n := range append([]string{"api", "admin", "www", "_quick"}, configured...) {
		n = strings.ToLower(strings.TrimSpace(n))
		if n == "" || seen[n] {
			continue
		}
		seen[n] = true
		out = append(out, n)
	}
	return out
}

func SiteDir(root, site string) string     { return filepath.Join(root, "sites", site) }
func CurrentPath(root, site string) string { return filepath.Join(SiteDir(root, site), "current") }
func ReleasesDir(root, site string) string { return filepath.Join(SiteDir(root, site), "releases") }
func IncomingDir(root, site string) string { return filepath.Join(SiteDir(root, site), ".incoming") }
func UploadsDir(root, site string) string  { return filepath.Join(root, "uploads", site) }

func ReadSiteConfig(siteDir string) (SiteConfig, error) {
	path := filepath.Join(siteDir, "site.json")
	f, err := os.Open(path)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return SiteConfig{}, nil
		}
		return SiteConfig{}, err
	}
	defer f.Close()
	dec := json.NewDecoder(f)
	dec.DisallowUnknownFields()
	var cfg SiteConfig
	if err := dec.Decode(&cfg); err != nil {
		return SiteConfig{}, err
	}
	return cfg, nil
}

func WriteSiteConfig(siteDir string, cfg SiteConfig) error {
	if cfg.Name == "" {
		cfg.Name = filepath.Base(siteDir)
	}
	if cfg.Subdomain == "" {
		cfg.Subdomain = cfg.Name
	}
	if err := os.MkdirAll(siteDir, 0o770); err != nil {
		return err
	}
	b, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return err
	}
	b = append(b, '\n')
	return os.WriteFile(filepath.Join(siteDir, "site.json"), b, 0o660)
}

func EnsureSiteConfig(siteDir, name string) error {
	path := filepath.Join(siteDir, "site.json")
	if _, err := os.Stat(path); err == nil {
		return nil
	} else if !errors.Is(err, os.ErrNotExist) {
		return err
	}
	return WriteSiteConfig(siteDir, SiteConfig{Name: name, Subdomain: name})
}

func URLFor(site string, cfg config.Config) string {
	if cfg.PublicBaseDomain != "" {
		return "https://" + site + "." + strings.TrimPrefix(strings.TrimSuffix(cfg.PublicBaseDomain, "."), ".")
	}
	if cfg.BaseURL != "" {
		base := strings.TrimRight(cfg.BaseURL, "/")
		return base + "/~/" + url.PathEscape(site) + "/"
	}
	_, port, err := net.SplitHostPort(cfg.Listen)
	if err == nil && port != "" {
		return "http://" + site + ".localhost:" + port
	}
	return "http://" + site + ".localhost"
}

func SiteFromHost(host string, cfg config.Config) (string, bool) {
	h, _, err := net.SplitHostPort(host)
	if err != nil {
		h = host
	}
	h = strings.ToLower(strings.TrimSuffix(h, "."))
	if strings.HasSuffix(h, ".localhost") {
		site := strings.TrimSuffix(h, ".localhost")
		if site != "" && !strings.Contains(site, ".") {
			return site, true
		}
	}
	base := strings.ToLower(strings.TrimSuffix(cfg.PublicBaseDomain, "."))
	if base != "" && strings.HasSuffix(h, "."+base) {
		site := strings.TrimSuffix(h, "."+base)
		if site != "" && !strings.Contains(site, ".") {
			return site, true
		}
	}
	return "", false
}

func SplitPathFallback(p string) (site, stripped string, ok bool) {
	clean := path.Clean("/" + p)
	if !strings.HasPrefix(clean, "/~/") {
		return "", "", false
	}
	rest := strings.TrimPrefix(clean, "/~/")
	parts := strings.SplitN(rest, "/", 2)
	if parts[0] == "" {
		return "", "", false
	}
	if len(parts) == 1 {
		return parts[0], "/", true
	}
	return parts[0], "/" + parts[1], true
}

func SPAFallbackPath(cfg SiteConfig) string {
	fb := strings.TrimSpace(cfg.Routing.SPAFallback)
	if fb == "" {
		return ""
	}
	fb = "/" + strings.TrimLeft(fb, "/")
	return path.Clean(fb)
}

func PathWithin(root, candidate string) bool {
	rootAbs, err := filepath.Abs(root)
	if err != nil {
		return false
	}
	candAbs, err := filepath.Abs(candidate)
	if err != nil {
		return false
	}
	rel, err := filepath.Rel(rootAbs, candAbs)
	if err != nil {
		return false
	}
	return rel == "." || (rel != "" && !strings.HasPrefix(rel, "..") && !filepath.IsAbs(rel))
}
