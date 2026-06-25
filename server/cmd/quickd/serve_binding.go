package main

import (
	"fmt"
	"net"
	"strings"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
)

type serveBinding struct {
	Listener net.Listener
	Adapter  identity.Adapter
	Close    func() error
	Addr     string
}

func openTCPServeBinding(cfg config.Config, devIdentity string, allowPublicUnsafe bool) (*serveBinding, error) {
	adapter, err := identity.NewAdapter(cfg, devIdentity, allowPublicUnsafe)
	if err != nil {
		return nil, err
	}
	ln, err := net.Listen("tcp", cfg.Listen)
	if err != nil {
		return nil, listenError(cfg.Listen, err)
	}
	return &serveBinding{Listener: ln, Adapter: adapter, Addr: cfg.Listen}, nil
}

func tsnetListenAddr(listen string) (string, error) {
	listen = strings.TrimSpace(listen)
	if listen == "" {
		return ":9366", nil
	}
	_, port, err := net.SplitHostPort(listen)
	if err == nil {
		if port == "" {
			return "", fmt.Errorf("tsnet listen address %q must include a port", listen)
		}
		return ":" + port, nil
	}
	if strings.HasPrefix(listen, ":") && len(listen) > 1 {
		return listen, nil
	}
	for _, ch := range listen {
		if ch < '0' || ch > '9' {
			return "", fmt.Errorf("tsnet listen address %q must include a port", listen)
		}
	}
	return ":" + listen, nil
}
