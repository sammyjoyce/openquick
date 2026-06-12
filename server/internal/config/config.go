package config

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
)

const (
	DefaultRoot       = "/srv/quick"
	DefaultConfigPath = "/etc/openquick/quickd.json"
)

type Config struct {
	Schema           string          `json:"$schema,omitempty"`
	Listen           string          `json:"listen"`
	PublicBaseDomain string          `json:"public_base_domain"`
	BaseURL          string          `json:"base_url"`
	RemoteRoot       string          `json:"remote_root"`
	DataDir          string          `json:"data_dir"`
	RetainedReleases int             `json:"retained_releases"`
	MaxUploadBytes   int64           `json:"max_upload_bytes"`
	IAP              IAPConfig       `json:"iap"`
	Deploy           DeployConfig    `json:"deploy"`
	Viewer           ViewerConfig    `json:"viewer"`
	Directory        DirectoryConfig `json:"directory"`
}

type IAPConfig struct {
	Type                 string   `json:"type"`
	Mode                 string   `json:"mode"`
	TrustedProxies       []string `json:"trusted_proxies"`
	SourceIPHeader       string   `json:"source_ip_header"`
	TeamDomain           string   `json:"team_domain"`
	Audience             string   `json:"audience"`
	JWKSURL              string   `json:"jwks_url"`
	EmailDomainAllowlist []string `json:"email_domain_allowlist"`
}

type DeployConfig struct {
	Policy        string   `json:"policy"`
	ReservedNames []string `json:"reserved_names"`
}

type ViewerConfig struct {
	RequireIdentity bool `json:"require_identity"`
	AllowAnonymous  bool `json:"allow_anonymous"`
}

type DirectoryConfig struct {
	Enabled bool `json:"enabled"`
}

func Default(root string) Config {
	if root == "" {
		root = DefaultRoot
	}
	return Config{
		Listen:           "127.0.0.1:9366",
		RemoteRoot:       root,
		DataDir:          filepath.Join(root, "data"),
		RetainedReleases: 10,
		MaxUploadBytes:   100 << 20,
		IAP: IAPConfig{
			Type:           "none",
			SourceIPHeader: "X-Forwarded-For",
		},
		Deploy: DeployConfig{
			Policy:        "any_ssh_deployer",
			ReservedNames: []string{"api", "admin", "www", "_quick"},
		},
		Viewer:    ViewerConfig{AllowAnonymous: true},
		Directory: DirectoryConfig{Enabled: true},
	}
}

func Load(path string) (Config, error) {
	f, err := os.Open(path)
	if err != nil {
		return Config{}, err
	}
	defer f.Close()
	cfg, err := Decode(f)
	if err != nil {
		return Config{}, err
	}
	return cfg, nil
}

func Decode(r interface{ Read([]byte) (int, error) }) (Config, error) {
	data, err := io.ReadAll(r)
	if err != nil {
		return Config{}, err
	}
	cfg := Default("")
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&cfg); err != nil {
		return Config{}, err
	}
	var extra any
	if err := dec.Decode(&extra); err == nil {
		return Config{}, errors.New("config: trailing JSON data")
	}
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(data, &fields); err != nil {
		return Config{}, err
	}
	if _, ok := fields["data_dir"]; !ok {
		cfg.DataDir = ""
	}
	cfg.ApplyDefaults()
	return cfg, nil
}

func LoadForRoot(root string) (Config, error) {
	cfg := Default(root)
	path := filepath.Join(root, "config", "quickd.json")
	if _, err := os.Stat(path); err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return cfg, nil
		}
		return Config{}, err
	}
	loaded, err := Load(path)
	if err != nil {
		return Config{}, err
	}
	if loaded.RemoteRoot == "" || loaded.RemoteRoot == DefaultRoot {
		loaded.RemoteRoot = root
	}
	loaded.ApplyDefaults()
	return loaded, nil
}

func (c *Config) ApplyDefaults() {
	root := c.RemoteRoot
	if root == "" {
		root = DefaultRoot
		c.RemoteRoot = root
	}
	if c.Listen == "" {
		c.Listen = "127.0.0.1:9366"
	}
	if c.DataDir == "" {
		c.DataDir = filepath.Join(root, "data")
	}
	if c.RetainedReleases <= 0 {
		c.RetainedReleases = 10
	}
	if c.MaxUploadBytes <= 0 {
		c.MaxUploadBytes = 100 << 20
	}
	if c.IAP.Type == "" {
		c.IAP.Type = "none"
	}
	if c.IAP.SourceIPHeader == "" {
		c.IAP.SourceIPHeader = "X-Forwarded-For"
	}
	if c.Deploy.Policy == "" {
		c.Deploy.Policy = "any_ssh_deployer"
	}
	if len(c.Deploy.ReservedNames) == 0 {
		c.Deploy.ReservedNames = []string{"api", "admin", "www", "_quick"}
	}
}

func (c Config) ConfigPathFromEnv() string {
	if p := os.Getenv("QUICKD_CONFIG"); p != "" {
		return p
	}
	return DefaultConfigPath
}

func RootFromEnv() string {
	if root := os.Getenv("QUICKD_ROOT"); root != "" {
		return root
	}
	return DefaultRoot
}

func IsLoopbackListen(addr string) bool {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		// Allow bare host in tests, but fail closed for wildcard-looking strings.
		host = addr
	}
	if host == "" || host == "0.0.0.0" || host == "::" || host == "[::]" {
		return false
	}
	if strings.EqualFold(host, "localhost") {
		return true
	}
	ip := net.ParseIP(strings.Trim(host, "[]"))
	return ip != nil && ip.IsLoopback()
}

func (c Config) ValidateServe(allowPublicUnsafe bool) error {
	t := strings.ToLower(c.IAP.Type)
	if t == "" {
		t = "none"
	}
	if (t == "none" || t == "dev") && !allowPublicUnsafe && !IsLoopbackListen(c.Listen) {
		return fmt.Errorf("iap=%s may only listen on loopback unless --allow-public-unsafe is set", t)
	}
	if c.Viewer.RequireIdentity && c.Viewer.AllowAnonymous {
		return errors.New("viewer.require_identity and viewer.allow_anonymous cannot both be true")
	}
	return nil
}

func NormalizeBaseURL(s string) string {
	return strings.TrimRight(s, "/")
}
