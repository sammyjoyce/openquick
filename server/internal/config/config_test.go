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
	if cfg.DataDir != "/tmp/q/data" || cfg.RetainedReleases != 10 || cfg.MaxUploadBytes == 0 {
		t.Fatalf("defaults not applied: %+v", cfg)
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
