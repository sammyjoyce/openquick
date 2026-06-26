//go:build !tsnet

package identity

import (
	"errors"
	"strings"
	"testing"

	"openquick.dev/quickd/internal/config"
)

func TestDefaultBuildRejectsTSNetAdapter(t *testing.T) {
	t.Parallel()
	for _, iap := range []config.IAPConfig{
		{Type: "tailscale-tsnet"},
		{Type: "tailscale", Mode: "tsnet"},
	} {
		cfg := config.Default("/tmp/q")
		cfg.IAP = iap
		_, err := NewAdapter(cfg, "", false)
		if !errors.Is(err, ErrMisconfiguredAdapter) || !strings.Contains(err.Error(), "-tags tsnet") {
			t.Fatalf("NewAdapter(%+v) err=%v, want clear tsnet build-tag error", iap, err)
		}
	}
}
