//go:build !tsnet

package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestServeRejectsTSNetWithoutBuildTag(t *testing.T) {
	t.Parallel()
	root := t.TempDir()
	cfgPath := filepath.Join(root, "quickd.json")
	cfg := `{"remote_root":"` + root + `","iap":{"type":"tailscale","mode":"tsnet"},"viewer":{"allow_anonymous":false}}`
	if err := os.WriteFile(cfgPath, []byte(cfg), 0o600); err != nil {
		t.Fatal(err)
	}
	err := serveCmd([]string{"--config", cfgPath})
	if err == nil || !strings.Contains(err.Error(), "-tags tsnet") {
		t.Fatalf("serve tsnet without build tag err=%v, want clear tsnet build-tag error", err)
	}
}
