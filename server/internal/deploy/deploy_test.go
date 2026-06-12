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
	if !deleted.Deleted {
		t.Fatalf("DeleteSite reported deleted=false")
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
