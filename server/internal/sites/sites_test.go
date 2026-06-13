package sites

import (
	"os"
	"path/filepath"
	"testing"

	"openquick.dev/quickd/internal/config"
)

func TestValidateSiteNameReservedAndSlug(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name string
		ok   bool
	}{
		{"demo", true},
		{"demo-1", true},
		{"api", false},
		{"www", false},
		{"Admin", false},
		{"-bad", false},
		{"bad-", false},
		{"bad_name", false},
		{"_quick", false},
		{"_private", false},
		{"_doctor-abc123", true},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			err := ValidateSiteName(tt.name, []string{"api", "admin", "www", "_quick"})
			if (err == nil) != tt.ok {
				t.Fatalf("ValidateSiteName(%q) err=%v ok=%v", tt.name, err, tt.ok)
			}
		})
	}
}

func TestReadReleaseConfigToleratesCLIFields(t *testing.T) {
	t.Parallel()
	dir := t.TempDir()
	// A deployed quick.json carries CLI-only fields (source, output, build,
	// profile, sdk). The host must accept it and read the shared fields.
	scaffold := `{
  "$schema": "https://openquick.dev/schemas/site.v1.json",
  "name": "demo",
  "source": ".",
  "output": ".",
  "build": null,
  "profile": null,
  "subdomain": "demo",
  "sdk": { "enabled": true, "import": "/_quick/sdk.js" }
}`
	if err := os.WriteFile(filepath.Join(dir, "quick.json"), []byte(scaffold), 0o600); err != nil {
		t.Fatal(err)
	}
	cfg, err := ReadReleaseConfig(dir)
	if err != nil {
		t.Fatalf("ReadReleaseConfig rejected CLI quick.json: %v", err)
	}
	if cfg.Name != "demo" || cfg.Subdomain != "demo" {
		t.Fatalf("unexpected config: %+v", cfg)
	}
}

func TestReadSiteConfigStaysStrict(t *testing.T) {
	t.Parallel()
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "site.json"), []byte(`{"name":"demo","bogus":true}`), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := ReadSiteConfig(dir); err == nil {
		t.Fatal("host-managed site.json with unknown fields should be rejected")
	}
}

func TestSplitPathFallback(t *testing.T) {
	site, p, ok := SplitPathFallback("/~/demo/assets/app.js")
	if !ok || site != "demo" || p != "/assets/app.js" {
		t.Fatalf("unexpected split: site=%q p=%q ok=%v", site, p, ok)
	}
}

func TestValidateSubdomainAndDomainConflicts(t *testing.T) {
	if err := ValidateSubdomain("api", []string{"api"}); err == nil {
		t.Fatalf("reserved subdomain accepted")
	}
	cfg := config.Default("/tmp/q")
	cfg.PublicBaseDomain = "quick.example.com"
	if _, err := ValidateDomain("quick.example.com", cfg); err == nil {
		t.Fatalf("apex accepted")
	}
	if _, err := ValidateDomain("api.quick.example.com", cfg); err == nil {
		t.Fatalf("reserved host accepted")
	}
	if d, err := ValidateDomain("App.Example.Org.", cfg); err != nil || d != "app.example.org" {
		t.Fatalf("domain=%q err=%v", d, err)
	}
}
