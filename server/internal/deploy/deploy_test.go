package deploy

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"testing"
	"time"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/sites"
	"openquick.dev/quickd/internal/store"
)

func newTestService(t *testing.T, retain int) (*Service, *store.Store, string) {
	t.Helper()
	root := t.TempDir()
	cfg := config.Default(root)
	cfg.RetainedReleases = retain
	st, err := store.Open(filepath.Join(root, "data"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { st.Close() })
	return New(cfg, st), st, root
}

func stageFile(t *testing.T, res *PrepareResult, name, body string) {
	t.Helper()
	p := filepath.Join(res.StagingPath, filepath.FromSlash(name))
	if err := os.MkdirAll(filepath.Dir(p), 0o770); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, []byte(body), 0o660); err != nil {
		t.Fatal(err)
	}
}

func readCurrentIndex(path string) ([]byte, error) {
	b, err := os.ReadFile(path)
	if err == nil || !transientCurrentReadError(err) {
		return b, err
	}
	// Darwin/APFS can transiently report EINVAL from open(2) while namei
	// walks through "current" as os.Rename atomically replaces that symlink
	// dirent. Retry once; persistent errors still fail the test.
	time.Sleep(time.Millisecond)
	return os.ReadFile(path)
}

func transientCurrentReadError(err error) bool {
	return errors.Is(err, syscall.EINVAL)
}

func TestActivateAtomicRetentionAndGC(t *testing.T) {
	svc, _, root := newTestService(t, 2)
	ctx := context.Background()

	first, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, first, "index.html", "one")
	if _, err := svc.Activate(ctx, "demo", first.DeployID); err != nil {
		t.Fatal(err)
	}

	oldIncoming := filepath.Join(root, "sites", "demo", ".incoming", "20200101T000000Z-deadbe")
	if err := os.MkdirAll(filepath.Join(oldIncoming, "files"), 0o770); err != nil {
		t.Fatal(err)
	}
	old := time.Now().Add(-25 * time.Hour)
	if err := os.Chtimes(oldIncoming, old, old); err != nil {
		t.Fatal(err)
	}

	second, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, second, "index.html", "two")

	currentIndex := filepath.Join(root, "sites", "demo", "current", "index.html")
	stop := make(chan struct{})
	errs := make(chan string, 1)
	go func() {
		for {
			select {
			case <-stop:
				return
			default:
				b, err := readCurrentIndex(currentIndex)
				if err != nil {
					select {
					case errs <- err.Error():
					default:
					}
					return
				}
				s := string(b)
				if s != "one" && s != "two" {
					select {
					case errs <- s:
					default:
					}
					return
				}
			}
		}
	}()
	if _, err := svc.Activate(ctx, "demo", second.DeployID); err != nil {
		t.Fatal(err)
	}
	close(stop)
	select {
	case msg := <-errs:
		t.Fatalf("concurrent reader saw partial/error: %s", msg)
	default:
	}
	if b, err := os.ReadFile(currentIndex); err != nil || string(b) != "two" {
		t.Fatalf("current index = %q err=%v", b, err)
	}
	if _, err := os.Stat(oldIncoming); !os.IsNotExist(err) {
		t.Fatalf("old incoming was not GCed: %v", err)
	}
	prev, err := os.Readlink(filepath.Join(root, "sites", "demo", "previous"))
	if err != nil || filepath.Base(prev) != first.DeployID {
		t.Fatalf("previous=%q err=%v", prev, err)
	}

	third, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, third, "index.html", "three")
	if _, err := svc.Activate(ctx, "demo", third.DeployID); err != nil {
		t.Fatal(err)
	}
	entries, err := os.ReadDir(filepath.Join(root, "sites", "demo", "releases"))
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 2 {
		names := make([]string, 0, len(entries))
		for _, e := range entries {
			names = append(names, e.Name())
		}
		t.Fatalf("retained releases=%v want 2", names)
	}
	if _, err := os.Stat(filepath.Join(root, "sites", "demo", "releases", first.DeployID)); !os.IsNotExist(err) {
		t.Fatalf("first release was not pruned: %v", err)
	}
}

func TestDeleteSiteRemovesCatalogAndFilesystem(t *testing.T) {
	svc, st, root := newTestService(t, 10)
	ctx := context.Background()
	res, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, res, "index.html", "demo")
	if _, err := svc.Activate(ctx, "demo", res.DeployID); err != nil {
		t.Fatal(err)
	}
	if _, err := st.GetSite(ctx, "demo"); err != nil {
		t.Fatalf("site not recorded before delete: %v", err)
	}
	if err := os.MkdirAll(filepath.Join(root, "uploads", "demo"), 0o750); err != nil {
		t.Fatal(err)
	}
	deleted, err := svc.DeleteSite(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	if !deleted.Deleted || deleted.Archive == "" {
		t.Fatalf("DeleteSite result=%+v", deleted)
	}
	if _, err := os.Stat(filepath.Join(deleted.Archive, "site", "current")); err != nil {
		t.Fatalf("archive missing site payload: %v", err)
	}
	if _, err := os.Stat(filepath.Join(root, "sites", "demo")); !os.IsNotExist(err) {
		t.Fatalf("site directory still exists: %v", err)
	}
	if _, err := os.Stat(filepath.Join(root, "uploads", "demo")); !os.IsNotExist(err) {
		t.Fatalf("uploads directory still exists: %v", err)
	}
	if _, err := st.GetSite(ctx, "demo"); err == nil {
		t.Fatalf("site catalog row still exists")
	}
	restored, err := svc.RestoreSite(ctx, "demo", deleted.Archive)
	if err != nil {
		t.Fatal(err)
	}
	if !restored.Restored || restored.Release != res.DeployID {
		t.Fatalf("restore=%+v", restored)
	}
	if _, err := os.Stat(filepath.Join(root, "sites", "demo", "current")); err != nil {
		t.Fatalf("site was not restored: %v", err)
	}
	rec, err := st.GetSite(ctx, "demo")
	if err != nil || rec.Release != res.DeployID || rec.Deployer != "restore" {
		t.Fatalf("restored site rec=%+v err=%v", rec, err)
	}
	purge, err := svc.PurgeArchive(ctx, filepath.Join(root, ".trash", "sites", "purge-me"))
	if err != nil || !purge.Purged {
		t.Fatalf("purge=%+v err=%v", purge, err)
	}
}

func TestCleanupIncomingRemovesPreparedStaging(t *testing.T) {
	svc, _, root := newTestService(t, 10)
	ctx := context.Background()
	prep, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, prep, "index.html", "staged")
	res, err := svc.CleanupIncoming(ctx, "demo", prep.DeployID)
	if err != nil {
		t.Fatal(err)
	}
	if !res.Cleaned || res.DeployID != prep.DeployID || res.Path == "" {
		t.Fatalf("cleanup=%+v", res)
	}
	if _, err := os.Stat(filepath.Join(root, "sites", "demo", ".incoming", prep.DeployID)); !os.IsNotExist(err) {
		t.Fatalf("staging still exists: %v", err)
	}
}

func TestRollbackSwitchesCurrentAndRecordsAudit(t *testing.T) {
	svc, st, root := newTestService(t, 10)
	ctx := context.Background()

	first, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, first, "index.html", "one")
	if _, err := svc.ActivateWithOptions(ctx, "demo", first.DeployID, ActivateOptions{Deployer: "alice"}); err != nil {
		t.Fatal(err)
	}

	second, err := svc.Prepare(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, second, "index.html", "two")
	if _, err := svc.ActivateWithOptions(ctx, "demo", second.DeployID, ActivateOptions{Deployer: "bob"}); err != nil {
		t.Fatal(err)
	}

	rolled, err := svc.Rollback(ctx, "demo", RollbackOptions{Deployer: "carol"})
	if err != nil {
		t.Fatal(err)
	}
	if !rolled.RolledBack || rolled.Release != first.DeployID || rolled.PreviousRelease != second.DeployID || rolled.Deployer != "rollback:carol" {
		t.Fatalf("rollback result=%+v", rolled)
	}
	currentIndex := filepath.Join(root, "sites", "demo", "current", "index.html")
	if b, err := os.ReadFile(currentIndex); err != nil || string(b) != "one" {
		t.Fatalf("current index=%q err=%v", b, err)
	}
	prev, err := os.Readlink(filepath.Join(root, "sites", "demo", "previous"))
	if err != nil || filepath.Base(prev) != second.DeployID {
		t.Fatalf("previous=%q err=%v", prev, err)
	}
	rec, err := st.GetSite(ctx, "demo")
	if err != nil || rec.Release != first.DeployID || rec.Deployer != "rollback:carol" {
		t.Fatalf("site rec=%+v err=%v", rec, err)
	}
	history, err := svc.ListReleases(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	if len(history.Releases) != 2 {
		t.Fatalf("release history=%+v", history.Releases)
	}
	var current, previous ReleaseRecord
	for _, rel := range history.Releases {
		if !rel.Verified || rel.Verification != "ok" || rel.Deployer == "" || rel.CreatedAt == "" {
			t.Fatalf("release metadata incomplete: %+v", rel)
		}
		if rel.Current {
			current = rel
		}
		if rel.Previous {
			previous = rel
		}
	}
	if current.Release != first.DeployID || current.Deployer != "rollback:carol" || previous.Release != second.DeployID || previous.Deployer != "bob" {
		t.Fatalf("current=%+v previous=%+v", current, previous)
	}
	if _, err := svc.Rollback(ctx, "demo", RollbackOptions{Release: first.DeployID}); err == nil || !strings.Contains(err.Error(), "already current") {
		t.Fatalf("expected already-current rollback error, got %v", err)
	}
	if _, err := svc.Rollback(ctx, "demo", RollbackOptions{Release: "20260611T000000Z-deadbe"}); err == nil || !strings.Contains(err.Error(), "unavailable") {
		t.Fatalf("expected missing-release rollback error, got %v", err)
	}
}

func TestPrepareMetadataSubdomainAndSSHCertSigning(t *testing.T) {
	svc, st, root := newTestService(t, 10)
	ctx := context.Background()

	first, err := svc.PrepareWithOptions(ctx, "demo", PrepareOptions{Subdomain: "team-demo", Deployer: "alice", SSHKeyID: "key-a"})
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, first, "index.html", "one")
	if _, err := svc.ActivateWithOptions(ctx, "demo", first.DeployID, ActivateOptions{Deployer: "alice", SSHKeyID: "key-a"}); err != nil {
		t.Fatal(err)
	}
	rec, err := st.GetSite(ctx, "demo")
	if err != nil {
		t.Fatal(err)
	}
	if rec.Subdomain != "team-demo" {
		t.Fatalf("subdomain=%q", rec.Subdomain)
	}
	second, err := svc.PrepareWithOptions(ctx, "demo", PrepareOptions{Deployer: "bob"})
	if err != nil {
		t.Fatal(err)
	}
	if second.LastDeployer == nil || *second.LastDeployer != "alice" || second.LastRelease == nil || *second.LastRelease != first.DeployID || second.LastDeployedAt == nil {
		t.Fatalf("missing overwrite metadata: %+v", second)
	}
	if _, err := svc.PrepareWithOptions(ctx, "other", PrepareOptions{Subdomain: "team-demo"}); err == nil || !strings.Contains(err.Error(), "already used") {
		t.Fatalf("expected subdomain uniqueness error, got %v", err)
	}
	verifyUnsigned, err := svc.VerifyReleases(ctx, "demo", first.DeployID)
	if err != nil {
		t.Fatal(err)
	}
	if !verifyUnsigned.OK || verifyUnsigned.Checked != 1 || len(verifyUnsigned.Failures) != 0 {
		t.Fatalf("unsigned verify=%+v", verifyUnsigned)
	}

	svc.Config.Deploy.RequireSSHCert = true
	noCert, err := svc.Prepare(ctx, "certed")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, noCert, "index.html", "x")
	if _, err := svc.Activate(ctx, "certed", noCert.DeployID); err == nil || !strings.Contains(err.Error(), "SSH certificate") {
		t.Fatalf("expected require ssh cert error, got %v", err)
	}

	svc.Config.Deploy.RequireSSHCert = false
	svc.Config.Deploy.Signing.Enabled = true
	signed, err := svc.Prepare(ctx, "signed")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, signed, "index.html", "signed")
	if _, err := svc.Activate(ctx, "signed", signed.DeployID); err != nil {
		t.Fatal(err)
	}
	verify, err := svc.VerifyReleases(ctx, "signed", signed.DeployID)
	if err != nil {
		t.Fatal(err)
	}
	if !verify.OK || verify.Checked != 1 || len(verify.Failures) != 0 {
		t.Fatalf("verify=%+v", verify)
	}
	if err := os.WriteFile(filepath.Join(root, "sites", "signed", "releases", signed.DeployID, "index.html"), []byte("tampered"), 0o660); err != nil {
		t.Fatal(err)
	}
	verify, err = svc.VerifyReleases(ctx, "signed", signed.DeployID)
	if err != nil {
		t.Fatal(err)
	}
	if verify.OK || len(verify.Failures) == 0 {
		t.Fatalf("tamper verify=%+v", verify)
	}
}

func TestPublicSiteDeployRescansStaticOnly(t *testing.T) {
	svc, st, _ := newTestService(t, 10)
	ctx := context.Background()
	if _, err := st.EnsureSite(ctx, "pub", "pub"); err != nil {
		t.Fatal(err)
	}
	if err := st.SetSitePublic(ctx, "pub", true); err != nil {
		t.Fatal(err)
	}
	res, err := svc.Prepare(ctx, "pub")
	if err != nil {
		t.Fatal(err)
	}
	stageFile(t, res, "index.html", `<script>quick.identity.current()</script>`)
	if _, err := svc.Activate(ctx, "pub", res.DeployID); err == nil || !strings.Contains(err.Error(), "public site deploy rejected") || !strings.Contains(err.Error(), "index.html matched") {
		t.Fatalf("expected public scan rejection with findings, got %v", err)
	}
}

func TestActivateRejectsBadStaging(t *testing.T) {
	svc, _, root := newTestService(t, 10)
	ctx := context.Background()
	t.Run("missing index", func(t *testing.T) {
		res, err := svc.Prepare(ctx, "missing")
		if err != nil {
			t.Fatal(err)
		}
		stageFile(t, res, "app.js", "console.log(1)")
		if _, err := svc.Activate(ctx, "missing", res.DeployID); err == nil || !strings.Contains(err.Error(), "missing index.html") {
			t.Fatalf("expected missing index error, got %v", err)
		}
	})
	t.Run("spa fallback allows another entry", func(t *testing.T) {
		res, err := svc.Prepare(ctx, "spa")
		if err != nil {
			t.Fatal(err)
		}
		if err := sites.WriteSiteConfig(filepath.Join(root, "sites", "spa"), sites.SiteConfig{Name: "spa", Routing: sites.RoutingConfig{SPAFallback: "/app.html"}}); err != nil {
			t.Fatal(err)
		}
		stageFile(t, res, "app.html", "app")
		if _, err := svc.Activate(ctx, "spa", res.DeployID); err != nil {
			t.Fatalf("spa fallback activation failed: %v", err)
		}
	})
	t.Run("directory listing allows missing index", func(t *testing.T) {
		res, err := svc.Prepare(ctx, "listing")
		if err != nil {
			t.Fatal(err)
		}
		stageFile(t, res, "quick.json", `{"routing":{"directory_listing":true}}`)
		stageFile(t, res, "docs/readme.txt", "hello")
		if _, err := svc.Activate(ctx, "listing", res.DeployID); err != nil {
			t.Fatalf("directory listing activation failed: %v", err)
		}
	})
	t.Run("symlink escape", func(t *testing.T) {
		res, err := svc.Prepare(ctx, "links")
		if err != nil {
			t.Fatal(err)
		}
		stageFile(t, res, "index.html", "ok")
		outside := filepath.Join(root, "outside.txt")
		if err := os.WriteFile(outside, []byte("secret"), 0o600); err != nil {
			t.Fatal(err)
		}
		if err := os.Symlink(outside, filepath.Join(res.StagingPath, "leak")); err != nil {
			t.Fatal(err)
		}
		if _, err := svc.Activate(ctx, "links", res.DeployID); err == nil || !strings.Contains(err.Error(), "escapes") {
			t.Fatalf("expected symlink escape error, got %v", err)
		}
	})
}
