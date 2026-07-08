package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/deploy"
	"openquick.dev/quickd/internal/store"
)

func TestAdminMintDevTokenStoresHashAndExpiry(t *testing.T) {
	root := t.TempDir()
	st, err := store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := st.EnsureSite(context.Background(), "demo", "demo"); err != nil {
		t.Fatal(err)
	}
	st.Close()

	out := captureStdout(t, func() {
		if err := adminCmd([]string{"mint-dev-token", "--site", "demo", "--ttl", "3600", "--json", "--root", root}); err != nil {
			t.Fatalf("admin mint-dev-token: %v", err)
		}
	})
	var res struct {
		Token     string `json:"token"`
		Site      string `json:"site"`
		ExpiresAt string `json:"expires_at"`
	}
	if err := json.Unmarshal([]byte(out), &res); err != nil {
		t.Fatalf("decode %q: %v", out, err)
	}
	if res.Token == "" || res.Site != "demo" {
		t.Fatalf("unexpected response: %+v", res)
	}
	expires, err := time.Parse("2006-01-02T15:04:05.000000000Z07:00", res.ExpiresAt)
	if err != nil || time.Until(expires) <= 0 {
		t.Fatalf("expires_at=%q err=%v", res.ExpiresAt, err)
	}

	st, err = store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	defer st.Close()
	var tokenHash, subject string
	if err := st.DB.QueryRow(`SELECT token_hash, subject FROM dev_tokens WHERE site='demo'`).Scan(&tokenHash, &subject); err != nil {
		t.Fatal(err)
	}
	sum := sha256.Sum256([]byte(res.Token))
	if tokenHash != hex.EncodeToString(sum[:]) || strings.Contains(tokenHash, res.Token) || subject == "" {
		t.Fatalf("token storage hash=%q subject=%q", tokenHash, subject)
	}
	if _, ok, err := st.ValidateDevToken(context.Background(), "wrong", res.Token, time.Now()); err != nil || ok {
		t.Fatalf("wrong-site validation ok=%v err=%v", ok, err)
	}
	if _, ok, err := st.ValidateDevToken(context.Background(), "demo", res.Token, time.Now()); err != nil || !ok {
		t.Fatalf("token validation ok=%v err=%v", ok, err)
	}
	if _, ok, err := st.ValidateDevToken(context.Background(), "demo", res.Token, time.Now().Add(2*time.Hour)); err != nil || ok {
		t.Fatalf("expired validation ok=%v err=%v", ok, err)
	}
	var remaining int
	if err := st.DB.QueryRow(`SELECT COUNT(*) FROM dev_tokens WHERE site='demo'`).Scan(&remaining); err != nil || remaining != 0 {
		t.Fatalf("expired prune remaining=%d err=%v", remaining, err)
	}
}

func TestTSNetListenAddrUsesConfiguredPort(t *testing.T) {
	t.Parallel()
	tests := map[string]string{
		"127.0.0.1:9366": ":9366",
		":9443":          ":9443",
		"9367":           ":9367",
		"[::1]:9368":     ":9368",
	}
	for in, want := range tests {
		got, err := tsnetListenAddr(in)
		if err != nil || got != want {
			t.Fatalf("tsnetListenAddr(%q)=%q,%v want %q,nil", in, got, err, want)
		}
	}
	if _, err := tsnetListenAddr("localhost"); err == nil {
		t.Fatalf("expected missing-port error")
	}
}

func TestServeRemoteAPIValidation(t *testing.T) {
	if err := serveCmd([]string{"--remote-api", "https://example.com", "--remote-api-token", "tok"}); err == nil || !strings.Contains(err.Error(), "--dev") {
		t.Fatalf("remote without dev err=%v", err)
	}
	if err := serveCmd([]string{"--dev", "--listen", "0.0.0.0:9366", "--remote-api", "https://example.com", "--remote-api-token", "tok"}); err == nil || !strings.Contains(err.Error(), "loopback") {
		t.Fatalf("remote public listen err=%v", err)
	}
	if err := serveCmd([]string{"--dev", "--remote-api", "http://example.com", "--remote-api-token", "tok"}); err == nil || !strings.Contains(err.Error(), "https") {
		t.Fatalf("remote http err=%v", err)
	}
}

func TestConfigCheckAndExplain(t *testing.T) {
	root := t.TempDir()
	if err := os.MkdirAll(filepath.Join(root, "config"), 0o750); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "config", "quickd.json"), []byte(`{"listen":"127.0.0.1:9876","remote_root":"`+root+`","iap":{"type":"none"},"ai":{"enabled":true,"providers":[{"type":"openai","api_key_env":"OPENAI_KEY"}]}}`), 0o600); err != nil {
		t.Fatal(err)
	}
	out := captureStdout(t, func() {
		if err := configCmd([]string{"check", "--root", root, "--json"}); err != nil {
			t.Fatalf("config check: %v", err)
		}
	})
	if !strings.Contains(out, `"ok":true`) || !strings.Contains(out, `"listen":"127.0.0.1:9876"`) || strings.Contains(out, "OPENAI_KEY_VALUE") {
		t.Fatalf("unexpected config check output: %s", out)
	}
	out = captureStdout(t, func() {
		if err := configCmd([]string{"explain", "--root", root, "--json"}); err != nil {
			t.Fatalf("config explain: %v", err)
		}
	})
	if !strings.Contains(out, `"defaults"`) || !strings.Contains(out, "secret values") {
		t.Fatalf("unexpected config explain output: %s", out)
	}
}

func TestDoctorFailsIncompleteCloudflareIAP(t *testing.T) {
	root := t.TempDir()
	if err := os.MkdirAll(filepath.Join(root, "config"), 0o750); err != nil {
		t.Fatal(err)
	}
	configJSON := `{"listen":"127.0.0.1:9876","remote_root":"` + root + `","iap":{"type":"cloudflare"},"viewer":{"allow_anonymous":false}}`
	if err := os.WriteFile(filepath.Join(root, "config", "quickd.json"), []byte(configJSON), 0o600); err != nil {
		t.Fatal(err)
	}
	out := captureStdout(t, func() {
		if err := doctorCmd([]string{"--root", root, "--json"}); err != nil {
			t.Fatalf("doctor: %v", err)
		}
	})
	var res struct {
		OK     bool          `json:"ok"`
		Checks []doctorCheck `json:"checks"`
	}
	if err := json.Unmarshal([]byte(out), &res); err != nil {
		t.Fatalf("decode doctor output: %v\n%s", err, out)
	}
	if res.OK {
		t.Fatalf("doctor reported ok for incomplete cloudflare IAP: %s", out)
	}
	found := false
	for _, check := range res.Checks {
		if check.Name == "iap-adapter" {
			found = true
			if check.Status != "fail" || !strings.Contains(check.Detail, "cloudflare") {
				t.Fatalf("unexpected iap-adapter check: %+v", check)
			}
		}
	}
	if !found {
		t.Fatalf("doctor output missing iap-adapter check: %s", out)
	}
}

func TestAuditExportChronologicalAndRedacted(t *testing.T) {
	root := t.TempDir()
	st, err := store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	ctx := context.Background()
	if err := st.RecordDeploy(ctx, "demo", "rel1", "alice", 0, 1, store.DeployAudit{SSHKeyID: "secret-key-value"}); err != nil {
		t.Fatal(err)
	}
	if err := st.SetSitePublic(ctx, "demo", true); err != nil {
		t.Fatal(err)
	}
	if _, err := st.AddDomain(ctx, "app.example.org", "demo"); err != nil {
		t.Fatal(err)
	}
	if err := st.DeleteSite(ctx, "demo"); err != nil {
		t.Fatal(err)
	}
	st.Close()

	out := captureStdout(t, func() {
		if err := auditCmd([]string{"export", "--root", root, "--json"}); err != nil {
			t.Fatalf("audit export: %v", err)
		}
	})
	if strings.Contains(out, "secret-key-value") {
		t.Fatalf("audit leaked secret-like metadata: %s", out)
	}
	var res struct {
		Events []store.AuditEvent `json:"events"`
	}
	if err := json.Unmarshal([]byte(out), &res); err != nil {
		t.Fatalf("decode audit: %v\n%s", err, out)
	}
	want := []string{"deploy", "site.public", "domain.add", "site.delete"}
	if len(res.Events) != len(want) {
		t.Fatalf("events len=%d want %d: %+v", len(res.Events), len(want), res.Events)
	}
	for i, action := range want {
		if res.Events[i].Action != action {
			t.Fatalf("event %d action=%q want %q; events=%+v", i, res.Events[i].Action, action, res.Events)
		}
	}
	if got := res.Events[0].Metadata["ssh_key_id"]; got != "[redacted]" {
		t.Fatalf("ssh_key_id metadata=%q", got)
	}
}

func TestDomainReadinessAssessment(t *testing.T) {
	ok := assessDomainReadiness(context.Background(), "mapped.example.test", func(context.Context, string) ([]string, error) {
		return []string{"127.0.0.1"}, nil
	})
	if ok.Status != "pending" || ok.DNS != "ok" || ok.TLS != "pending" || len(ok.Addresses) != 1 || ok.Remediation == "" {
		t.Fatalf("mapped readiness = %+v", ok)
	}
	fail := assessDomainReadiness(context.Background(), "missing.example.test", func(context.Context, string) ([]string, error) {
		return nil, &net.DNSError{Err: "no such host", Name: "missing.example.test"}
	})
	if fail.Status != "fail" || fail.DNS != "fail" || fail.Remediation == "" {
		t.Fatalf("missing readiness = %+v", fail)
	}
}

func TestDomainsWithReadinessRunsLookupsConcurrentlyAndPreservesOrder(t *testing.T) {
	recs := []store.DomainRecord{{Domain: "slow-a.example", Site: "demo"}, {Domain: "slow-b.example", Site: "demo"}}
	started := make(chan string, len(recs))
	release := make(chan struct{})
	lookup := func(context.Context, string) ([]string, error) {
		started <- "started"
		<-release
		return []string{"127.0.0.1"}, nil
	}
	done := make(chan []domainListRecord, 1)
	go func() { done <- domainsWithReadiness(context.Background(), recs, lookup) }()
	for i := 0; i < len(recs); i++ {
		select {
		case <-started:
		case <-time.After(time.Second):
			t.Fatalf("lookup %d did not start before first lookup was released", i)
		}
	}
	close(release)
	select {
	case out := <-done:
		if len(out) != len(recs) || out[0].Domain != recs[0].Domain || out[1].Domain != recs[1].Domain {
			t.Fatalf("domains order changed: %+v", out)
		}
	case <-time.After(time.Second):
		t.Fatal("domainsWithReadiness did not finish")
	}
}

func TestServeDevPortInUseSuggestsRecovery(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()

	err = serveCmd([]string{"--dev", "--listen", ln.Addr().String()})
	if err == nil || !strings.Contains(err.Error(), "address already in use") || !strings.Contains(err.Error(), "--port") {
		t.Fatalf("expected port recovery hint, got %v", err)
	}
}

func TestSitesPublicReportsScanFindings(t *testing.T) {
	root := t.TempDir()
	if err := os.MkdirAll(filepath.Join(root, "config"), 0o750); err != nil {
		t.Fatal(err)
	}
	configJSON := `{"remote_root":"` + root + `","public_static":{"enabled":true},"viewer":{"allow_anonymous":true}}`
	if err := os.WriteFile(filepath.Join(root, "config", "quickd.json"), []byte(configJSON), 0o600); err != nil {
		t.Fatal(err)
	}
	release := filepath.Join(root, "sites", "demo", "releases", "20260611T000000Z-abcdef")
	if err := os.MkdirAll(release, 0o770); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(release, "index.html"), []byte(`<script>fetch('/_quick/identity')</script>`), 0o660); err != nil {
		t.Fatal(err)
	}
	if err := os.Symlink(filepath.Join("releases", "20260611T000000Z-abcdef"), filepath.Join(root, "sites", "demo", "current")); err != nil {
		t.Fatal(err)
	}
	st, err := store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := st.EnsureSite(context.Background(), "demo", "demo"); err != nil {
		t.Fatal(err)
	}
	st.Close()

	err = sitesPublicCmd([]string{"demo", "--on", "--root", root})
	if err == nil || !strings.Contains(err.Error(), "index.html matched") || !strings.Contains(err.Error(), "/_quick/") {
		t.Fatalf("expected scan findings error, got %v", err)
	}
}

func TestSitesRestoreArgsParseFlagsInEitherPosition(t *testing.T) {
	root := t.TempDir()
	archive := filepath.Join(root, ".trash", "sites", "demo-20260612T000000.000000000Z")
	tests := []struct {
		name string
		args []string
	}{
		{
			name: "site-first",
			args: []string{"demo", "--from", archive, "--json", "--root", root},
		},
		{
			name: "flags-first",
			args: []string{"--from", archive, "--json", "--root", root, "demo"},
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			af, siteName, gotArchive, err := parseSitesRestoreArgs(tc.args)
			if err != nil {
				t.Fatalf("parseSitesRestoreArgs: %v", err)
			}
			if siteName != "demo" || gotArchive != archive || !af.json || af.root != root {
				t.Fatalf("parsed af=%+v site=%q archive=%q", af, siteName, gotArchive)
			}
		})
	}
}

func TestSitesPurgeArgsRejectsPositionals(t *testing.T) {
	_, _, err := parseSitesPurgeArgs([]string{"stale-site", "--from", "/srv/quick/.trash/sites/stale-site-20260612T000000.000000000Z"})
	if err == nil || !strings.Contains(err.Error(), "usage: quickd sites purge") {
		t.Fatalf("expected purge positional usage error, got %v", err)
	}
}

func TestReleasesListCommandReportsHistory(t *testing.T) {
	root := t.TempDir()
	cfg := config.Default(root)
	st, err := store.Open(cfg.DataDir)
	if err != nil {
		t.Fatal(err)
	}
	svc := deploy.New(cfg, st)
	ctx := context.Background()

	first, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(first.StagingPath, "index.html"), []byte("one"), 0o660); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.ActivateWithOptions(ctx, "demo", first.DeployID, deploy.ActivateOptions{Deployer: "alice"}); err != nil {
		t.Fatal(err)
	}
	second, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(second.StagingPath, "index.html"), []byte("two"), 0o660); err != nil {
		t.Fatal(err)
	}
	if _, err := svc.ActivateWithOptions(ctx, "demo", second.DeployID, deploy.ActivateOptions{Deployer: "bob"}); err != nil {
		t.Fatal(err)
	}
	st.Close()

	out := captureStdout(t, func() {
		if err := releasesCmd([]string{"list", "--site", "demo", "--root", root, "--json"}); err != nil {
			t.Fatalf("releases list: %v", err)
		}
	})
	if !strings.Contains(out, `"releases"`) || !strings.Contains(out, `"current":true`) || !strings.Contains(out, `"previous":true`) || !strings.Contains(out, `"deployer":"bob"`) {
		t.Fatalf("unexpected releases output: %s", out)
	}
}

func captureStdout(t *testing.T, fn func()) string {
	t.Helper()
	old := os.Stdout
	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}
	os.Stdout = w
	defer func() { os.Stdout = old }()
	fn()
	_ = w.Close()
	data, err := io.ReadAll(r)
	if err != nil {
		t.Fatal(err)
	}
	return string(data)
}
