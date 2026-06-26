//go:build tsnet

package main

import (
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/identity"
	"tailscale.com/tsnet"
)

func TestOpenServeBindingUsesTSNetListener(t *testing.T) {
	root := t.TempDir()
	cfg := config.Default(root)
	cfg.IAP.Type = "tailscale"
	cfg.IAP.Mode = "tsnet"
	cfg.IAP.TSNet.Hostname = "quick-test"
	cfg.IAP.TSNet.AuthKeyEnv = "QUICKD_TSNET_TEST_AUTHKEY"
	cfg.IAP.TSNet.Ephemeral = true
	cfg.ApplyDefaults()
	t.Setenv("QUICKD_TSNET_TEST_AUTHKEY", "secret-auth-key")

	oldListen := tsnetListen
	defer func() { tsnetListen = oldListen }()
	var gotNetwork, gotAddr string
	var gotServer *tsnet.Server
	tsnetListen = func(server *tsnet.Server, network, addr string) (net.Listener, error) {
		gotServer = server
		gotNetwork = network
		gotAddr = addr
		return net.Listen("tcp", "127.0.0.1:0")
	}

	binding, err := openServeBinding(cfg, "", false)
	if err != nil {
		t.Fatal(err)
	}
	defer binding.Listener.Close()
	defer func() {
		if binding.Close != nil {
			_ = binding.Close()
		}
	}()

	if gotNetwork != "tcp" || gotAddr != ":9366" {
		t.Fatalf("tsnet listen called with %q %q", gotNetwork, gotAddr)
	}
	if gotServer == nil || gotServer.Hostname != "quick-test" || gotServer.Dir != filepath.Join(root, "data", "tsnet") || gotServer.AuthKey != "secret-auth-key" || !gotServer.Ephemeral {
		t.Fatalf("tsnet server not configured from config: %+v", gotServer)
	}
	adapter, ok := binding.Adapter.(identity.TailscaleTSNetAdapter)
	if !ok || adapter.Server != gotServer {
		t.Fatalf("adapter does not share tsnet server: %#v", binding.Adapter)
	}
}

func TestDoctorAcceptsTSNetAdapterWithBuildTag(t *testing.T) {
	root := t.TempDir()
	if err := os.MkdirAll(filepath.Join(root, "config"), 0o750); err != nil {
		t.Fatal(err)
	}
	cfg := `{"remote_root":"` + root + `","iap":{"type":"tailscale","mode":"tsnet"},"viewer":{"allow_anonymous":false}}`
	if err := os.WriteFile(filepath.Join(root, "config", "quickd.json"), []byte(cfg), 0o600); err != nil {
		t.Fatal(err)
	}
	out := captureStdout(t, func() {
		if err := doctorCmd([]string{"--root", root, "--json"}); err != nil {
			t.Fatalf("doctor: %v", err)
		}
	})
	if !strings.Contains(out, `"name":"iap-adapter"`) || !strings.Contains(out, `"detail":"tailscale-tsnet"`) || !strings.Contains(out, `"status":"ok"`) {
		t.Fatalf("doctor did not accept tsnet adapter: %s", out)
	}
}
