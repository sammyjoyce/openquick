//go:build !tsnet

package main

import (
	"errors"

	"openquick.dev/quickd/internal/config"
)

func openServeBinding(cfg config.Config, devIdentity string, allowPublicUnsafe bool) (*serveBinding, error) {
	if cfg.IAPTSNetRequested() {
		return nil, errors.New("iap.type=tailscale-tsnet/iap.mode=tsnet requires quickd built with -tags tsnet")
	}
	return openTCPServeBinding(cfg, devIdentity, allowPublicUnsafe)
}

func tsnetServeSupported() bool { return false }
