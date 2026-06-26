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
	Schema           string             `json:"$schema,omitempty"`
	Listen           string             `json:"listen"`
	PublicBaseDomain string             `json:"public_base_domain"`
	BaseURL          string             `json:"base_url"`
	RemoteRoot       string             `json:"remote_root"`
	DataDir          string             `json:"data_dir"`
	RetainedReleases int                `json:"retained_releases"`
	MaxUploadBytes   int64              `json:"max_upload_bytes"`
	IAP              IAPConfig          `json:"iap"`
	Deploy           DeployConfig       `json:"deploy"`
	Viewer           ViewerConfig       `json:"viewer"`
	Directory        DirectoryConfig    `json:"directory"`
	PublicStatic     PublicStaticConfig `json:"public_static"`
	HTTPDeploy       HTTPDeployConfig   `json:"http_deploy"`
	AI               AIConfig           `json:"ai"`
	Warehouse        WarehouseConfig    `json:"warehouse"`
	DevProxy         DevProxyConfig     `json:"dev_proxy"`
}

type IAPConfig struct {
	Type                 string      `json:"type"`
	Mode                 string      `json:"mode"`
	TrustedProxies       []string    `json:"trusted_proxies"`
	SourceIPHeader       string      `json:"source_ip_header"`
	TeamDomain           string      `json:"team_domain"`
	Audience             string      `json:"audience"`
	JWKSURL              string      `json:"jwks_url"`
	EmailDomainAllowlist []string    `json:"email_domain_allowlist"`
	TSNet                TSNetConfig `json:"tsnet"`
}

type TSNetConfig struct {
	Hostname   string `json:"hostname"`
	StateDir   string `json:"state_dir"`
	AuthKeyEnv string `json:"auth_key_env"`
	Ephemeral  bool   `json:"ephemeral"`
	ControlURL string `json:"control_url"`
}

type DeployConfig struct {
	Policy         string        `json:"policy"`
	ReservedNames  []string      `json:"reserved_names"`
	Signing        SigningConfig `json:"signing"`
	RequireSSHCert bool          `json:"require_ssh_cert"`
}

type SigningConfig struct {
	Enabled  bool `json:"enabled"`
	Required bool `json:"required"`
}

type PublicStaticConfig struct {
	Enabled bool `json:"enabled"`
}

type HTTPDeployConfig struct {
	Enabled         bool     `json:"enabled"`
	Tokens          []string `json:"tokens"`
	AllowIdentities []string `json:"allow_identities"`
}

type AIConfig struct {
	Enabled         bool               `json:"enabled"`
	Providers       []AIProviderConfig `json:"providers"`
	DefaultProvider string             `json:"default_provider"`
	Limits          AILimitsConfig     `json:"limits"`
}

type AIProviderConfig struct {
	Name         string   `json:"name"`
	Type         string   `json:"type"`
	BaseURL      string   `json:"base_url"`
	APIKeyEnv    string   `json:"api_key_env"`
	Models       []string `json:"models"`
	DefaultModel string   `json:"default_model"`

	APIKey    string `json:"-"`
	Available bool   `json:"-"`
}

type AILimitsConfig struct {
	RequestsPerMinutePerIdentity int   `json:"requests_per_minute_per_identity"`
	RequestsPerDayPerSite        int   `json:"requests_per_day_per_site"`
	MaxRequestBytes              int64 `json:"max_request_bytes"`
}

type WarehouseConfig struct {
	Enabled bool                   `json:"enabled"`
	Queries []WarehouseQueryConfig `json:"queries"`
}

type WarehouseQueryConfig struct {
	Name    string                 `json:"name"`
	SQL     string                 `json:"sql"`
	Params  []WarehouseParamConfig `json:"params"`
	MaxRows int                    `json:"max_rows"`
}

type WarehouseParamConfig struct {
	Name string `json:"name"`
	Type string `json:"type"`
}

type DevProxyConfig struct {
	Enabled bool `json:"enabled"`
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
	if err := cfg.Validate(); err != nil {
		return Config{}, err
	}
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

func cloudflareJWKSURL(teamDomain string) string {
	teamDomain = strings.TrimRight(strings.TrimSpace(teamDomain), "/")
	if teamDomain == "" {
		return ""
	}
	return teamDomain + "/cdn-cgi/access/certs"
}

func isCloudflareIAP(t string) bool {
	t = strings.ToLower(strings.TrimSpace(t))
	return t == "cloudflare" || t == "cloudflare-access"
}

func (c Config) IAPTSNetRequested() bool {
	t := strings.ToLower(strings.TrimSpace(c.IAP.Type))
	mode := strings.ToLower(strings.TrimSpace(c.IAP.Mode))
	return t == "tailscale-tsnet" || (t == "tailscale" && mode == "tsnet")
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
	c.IAP.Type = strings.ToLower(strings.TrimSpace(c.IAP.Type))
	c.IAP.Mode = strings.ToLower(strings.TrimSpace(c.IAP.Mode))
	c.IAP.TSNet.Hostname = strings.TrimSpace(c.IAP.TSNet.Hostname)
	c.IAP.TSNet.StateDir = strings.TrimSpace(c.IAP.TSNet.StateDir)
	c.IAP.TSNet.AuthKeyEnv = strings.TrimSpace(c.IAP.TSNet.AuthKeyEnv)
	c.IAP.TSNet.ControlURL = strings.TrimSpace(c.IAP.TSNet.ControlURL)
	if c.IAP.Type == "tailscale-tsnet" {
		c.IAP.Mode = "tsnet"
	}
	if c.IAPTSNetRequested() {
		if c.IAP.TSNet.Hostname == "" {
			c.IAP.TSNet.Hostname = "openquick"
		}
		if c.IAP.TSNet.StateDir == "" {
			c.IAP.TSNet.StateDir = filepath.Join(c.DataDir, "tsnet")
		}
	}
	if isCloudflareIAP(c.IAP.Type) {
		c.IAP.TeamDomain = strings.TrimRight(strings.TrimSpace(c.IAP.TeamDomain), "/")
		if c.IAP.TeamDomain != "" && !strings.HasPrefix(c.IAP.TeamDomain, "http://") && !strings.HasPrefix(c.IAP.TeamDomain, "https://") {
			c.IAP.TeamDomain = "https://" + c.IAP.TeamDomain
		}
		if c.IAP.JWKSURL == "" {
			c.IAP.JWKSURL = cloudflareJWKSURL(c.IAP.TeamDomain)
		}
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
	if c.AI.Limits.RequestsPerMinutePerIdentity <= 0 {
		c.AI.Limits.RequestsPerMinutePerIdentity = 20
	}
	if c.AI.Limits.RequestsPerDayPerSite <= 0 {
		c.AI.Limits.RequestsPerDayPerSite = 2000
	}
	if c.AI.Limits.MaxRequestBytes <= 0 {
		c.AI.Limits.MaxRequestBytes = 1 << 20
	}
	for i := range c.AI.Providers {
		p := &c.AI.Providers[i]
		p.Type = strings.ToLower(strings.TrimSpace(p.Type))
		if p.Name == "" {
			p.Name = p.Type
		}
		if p.BaseURL == "" {
			switch p.Type {
			case "openai":
				p.BaseURL = "https://api.openai.com/v1"
			case "anthropic":
				p.BaseURL = "https://api.anthropic.com"
			}
		}
		p.BaseURL = strings.TrimRight(p.BaseURL, "/")
		if p.DefaultModel == "" && len(p.Models) > 0 {
			p.DefaultModel = p.Models[0]
		}
		p.APIKey = ""
		p.Available = false
		if p.APIKeyEnv != "" {
			p.APIKey = os.Getenv(p.APIKeyEnv)
			p.Available = p.APIKey != ""
		}
	}
	if c.AI.DefaultProvider == "" && len(c.AI.Providers) > 0 {
		c.AI.DefaultProvider = c.AI.Providers[0].Name
	}
	for i := range c.Warehouse.Queries {
		if c.Warehouse.Queries[i].MaxRows <= 0 {
			c.Warehouse.Queries[i].MaxRows = 1000
		}
	}
}

func (c Config) Validate() error {
	if c.IAPTSNetRequested() && !tsnetBuildEnabled() {
		return errors.New("config: iap.type=tailscale-tsnet/iap.mode=tsnet requires quickd built with -tags tsnet")
	}
	if err := c.ValidateAI(); err != nil {
		return err
	}
	if err := c.ValidateWarehouse(); err != nil {
		return err
	}
	return nil
}

func (c Config) AIConfigured() bool {
	return c.AI.Enabled && len(c.AI.Providers) > 0
}

func (c Config) WarehouseConfigured() bool {
	return c.Warehouse.Enabled && len(c.Warehouse.Queries) > 0
}

func (c Config) ValidateAI() error {
	for i, p := range c.AI.Providers {
		t := strings.ToLower(strings.TrimSpace(p.Type))
		if t != "openai" && t != "anthropic" {
			return fmt.Errorf("config: ai.providers[%d].type must be openai or anthropic", i)
		}
		if strings.TrimSpace(p.Name) == "" {
			return fmt.Errorf("config: ai.providers[%d].name is required", i)
		}
		if p.DefaultModel != "" && len(p.Models) > 0 && !stringIn(p.DefaultModel, p.Models) {
			return fmt.Errorf("config: ai.providers[%d].default_model is not in models allowlist", i)
		}
	}
	return nil
}

func (c Config) ValidateWarehouse() error {
	for i, q := range c.Warehouse.Queries {
		if strings.TrimSpace(q.Name) == "" {
			return fmt.Errorf("config: warehouse.queries[%d].name is required", i)
		}
		if _, err := CleanWarehouseSQL(q.SQL); err != nil {
			return fmt.Errorf("config: warehouse.queries[%d].sql: %w", i, err)
		}
		for j, p := range q.Params {
			if strings.TrimSpace(p.Name) == "" {
				return fmt.Errorf("config: warehouse.queries[%d].params[%d].name is required", i, j)
			}
			switch strings.ToLower(strings.TrimSpace(p.Type)) {
			case "string", "int", "float":
			default:
				return fmt.Errorf("config: warehouse.queries[%d].params[%d].type must be string, int, or float", i, j)
			}
		}
	}
	return nil
}

func CleanWarehouseSQL(sql string) (string, error) {
	trimmed := strings.TrimSpace(sql)
	if trimmed == "" {
		return "", errors.New("sql is required")
	}
	body := strings.TrimSpace(strings.TrimRight(trimmed, "; \t\r\n"))
	upper := strings.ToUpper(body)
	if !hasSQLLeadingKeyword(upper, "SELECT") && !hasSQLLeadingKeyword(upper, "WITH") {
		return "", errors.New("sql must start with SELECT or WITH")
	}
	if strings.Contains(body, ";") {
		return "", errors.New("sql must not contain semicolons except a trailing terminator")
	}
	return body, nil
}

func hasSQLLeadingKeyword(upper, keyword string) bool {
	if upper == keyword {
		return true
	}
	if !strings.HasPrefix(upper, keyword) {
		return false
	}
	next := upper[len(keyword)]
	return next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '('
}

func stringIn(s string, values []string) bool {
	for _, v := range values {
		if v == s {
			return true
		}
	}
	return false
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
	if c.IAPTSNetRequested() && !tsnetBuildEnabled() {
		return errors.New("iap=tailscale-tsnet requires quickd built with -tags tsnet")
	}
	if (t == "none" || t == "dev") && !allowPublicUnsafe && !IsLoopbackListen(c.Listen) {
		return fmt.Errorf("iap=%s may only listen on loopback unless --allow-public-unsafe is set", t)
	}
	if isCloudflareIAP(t) {
		if strings.TrimSpace(c.IAP.TeamDomain) == "" || strings.TrimSpace(c.IAP.Audience) == "" || strings.TrimSpace(c.IAP.JWKSURL) == "" {
			return errors.New("iap=cloudflare requires team_domain, audience, and jwks_url")
		}
	}
	if c.Viewer.RequireIdentity && c.Viewer.AllowAnonymous {
		return errors.New("viewer.require_identity and viewer.allow_anonymous cannot both be true")
	}
	return nil
}

func NormalizeBaseURL(s string) string {
	return strings.TrimRight(s, "/")
}
