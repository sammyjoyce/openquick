package config

import (
	"strings"
	"testing"
)

func TestDecodeStrictAndDefaults(t *testing.T) {
	_, err := Decode(strings.NewReader(`{"listen":"127.0.0.1:9366","remote_root":"/tmp/q","iap":{"type":"none"},"unknown":true}`))
	if err == nil {
		t.Fatalf("expected unknown field error")
	}
	cfg, err := Decode(strings.NewReader(`{"remote_root":"/tmp/q","iap":{"type":"none"}}`))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.DataDir != "/tmp/q/data" || cfg.RetainedReleases != 10 || cfg.MaxUploadBytes == 0 || !cfg.Directory.Enabled || cfg.Deploy.Signing.Enabled || cfg.Deploy.RequireSSHCert || cfg.PublicStatic.Enabled || cfg.HTTPDeploy.Enabled {
		t.Fatalf("defaults not applied: %+v", cfg)
	}
	cfg, err = Decode(strings.NewReader(`{"remote_root":"/tmp/q","deploy":{"signing":{"enabled":true,"required":true},"require_ssh_cert":true},"public_static":{"enabled":true},"http_deploy":{"enabled":true,"tokens":["abc"],"allow_identities":["dev:sam@example.com"]},"iap":{"type":"none"}}`))
	if err != nil {
		t.Fatal(err)
	}
	if !cfg.Deploy.Signing.Enabled || !cfg.Deploy.Signing.Required || !cfg.Deploy.RequireSSHCert || !cfg.PublicStatic.Enabled || !cfg.HTTPDeploy.Enabled || len(cfg.HTTPDeploy.Tokens) != 1 || len(cfg.HTTPDeploy.AllowIdentities) != 1 {
		t.Fatalf("new config fields not decoded: %+v", cfg)
	}
	cfg, err = Decode(strings.NewReader(`{"remote_root":"/tmp/q","directory":{"enabled":false},"iap":{"type":"none"}}`))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.Directory.Enabled {
		t.Fatalf("directory.enabled=false was not preserved: %+v", cfg.Directory)
	}
	_, err = Decode(strings.NewReader(`{"remote_root":"/tmp/q","directory":{"enabled":true,"extra":1},"iap":{"type":"none"}}`))
	if err == nil {
		t.Fatalf("expected unknown directory field error")
	}
}

func TestValidateServeRejectsPublicNone(t *testing.T) {
	cfg := Default("/tmp/q")
	cfg.Listen = "0.0.0.0:9366"
	cfg.IAP.Type = "none"
	if err := cfg.ValidateServe(false); err == nil {
		t.Fatalf("expected public none rejection")
	}
	if err := cfg.ValidateServe(true); err != nil {
		t.Fatalf("allow public unsafe should pass: %v", err)
	}
}
