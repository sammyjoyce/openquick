//go:build tsnet

package main

import (
	"fmt"
	"log"
	"net"
	"os"
	"strings"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
	"tailscale.com/tsnet"
)

var tsnetListen = func(server *tsnet.Server, network, addr string) (net.Listener, error) {
	return server.Listen(network, addr)
}

func openServeBinding(cfg config.Config, devIdentity string, allowPublicUnsafe bool) (*serveBinding, error) {
	if cfg.IAPTSNetRequested() {
		return openTSNetServeBinding(cfg)
	}
	return openTCPServeBinding(cfg, devIdentity, allowPublicUnsafe)
}

func openTSNetServeBinding(cfg config.Config) (*serveBinding, error) {
	ts := cfg.IAP.TSNet
	if err := os.MkdirAll(ts.StateDir, 0o700); err != nil {
		return nil, fmt.Errorf("create tsnet state dir %s: %w", ts.StateDir, err)
	}
	info, err := os.Lstat(ts.StateDir)
	if err != nil {
		return nil, fmt.Errorf("stat tsnet state dir %s: %w", ts.StateDir, err)
	}
	if info.Mode()&os.ModeSymlink != 0 || !info.IsDir() {
		return nil, fmt.Errorf("tsnet state path %s must be a directory, not a symlink", ts.StateDir)
	}
	if err := os.Chmod(ts.StateDir, 0o700); err != nil {
		return nil, fmt.Errorf("secure tsnet state dir %s: %w", ts.StateDir, err)
	}

	server := &tsnet.Server{
		Hostname:   ts.Hostname,
		Dir:        ts.StateDir,
		Ephemeral:  ts.Ephemeral,
		ControlURL: ts.ControlURL,
		UserLogf: func(format string, args ...any) {
			log.Printf("tsnet: "+format, args...)
		},
	}
	if env := strings.TrimSpace(ts.AuthKeyEnv); env != "" {
		server.AuthKey = os.Getenv(env)
		if server.AuthKey == "" {
			log.Printf("tsnet auth key env %s is not set; if state is not already authorized, tsnet may print an interactive auth URL", env)
		}
	} else if os.Getenv("TS_AUTHKEY") == "" {
		log.Printf("tsnet starting without TS_AUTHKEY; if state is not already authorized, tsnet may print an interactive auth URL")
	}

	listenAddr, err := tsnetListenAddr(cfg.Listen)
	if err != nil {
		_ = server.Close()
		return nil, err
	}
	ln, err := tsnetListen(server, "tcp", listenAddr)
	if err != nil {
		_ = server.Close()
		return nil, fmt.Errorf("tsnet listen %s failed: %w", listenAddr, err)
	}
	return &serveBinding{
		Listener: ln,
		Adapter:  identity.TailscaleTSNetAdapter{Server: server},
		Close:    server.Close,
		Addr:     "tsnet " + listenAddr,
	}, nil
}

func tsnetServeSupported() bool { return true }
