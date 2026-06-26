//go:build !tsnet

package config

import (
	"strings"
	"testing"
)

func TestDefaultBuildRejectsTSNetIAP(t *testing.T) {
	t.Parallel()
	for _, body := range []string{
		`{"remote_root":"/tmp/q","iap":{"type":"tailscale-tsnet"}}`,
		`{"remote_root":"/tmp/q","iap":{"type":"tailscale","mode":"tsnet"}}`,
	} {
		_, err := Decode(strings.NewReader(body))
		if err == nil || !strings.Contains(err.Error(), "-tags tsnet") {
			t.Fatalf("Decode(%s) err=%v, want clear tsnet build-tag error", body, err)
		}
	}
}
