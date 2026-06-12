package deploy

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"syscall"
	"time"

	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/sites"
	"openquick.dev/quickd/internal/store"
)

type Service struct {
	Config config.Config
	Store  *store.Store
}

type PrepareResult struct {
	FormatVersion string  `json:"format_version"`
	Site          string  `json:"site"`
	DeployID      string  `json:"deploy_id"`
	StagingPath   string  `json:"staging_path"`
	LinkDest      *string `json:"link_dest"`
}

type ActivateResult struct {
	FormatVersion string `json:"format_version"`
	Site          string `json:"site"`
	Release       string `json:"release"`
	URL           string `json:"url"`
	Files         int    `json:"files"`
	Bytes         int64  `json:"bytes"`
}

type DeleteResult struct {
	FormatVersion string `json:"format_version"`
	Site          string `json:"site"`
	Deleted       bool   `json:"deleted"`
}

type Manifest struct {
	FormatVersion string `json:"format_version"`
	Site          string `json:"site"`
	Release       string `json:"release"`
	Files         int    `json:"files"`
	Bytes         int64  `json:"bytes"`
	ContentHash   string `json:"content_hash"`
	Deployer      string `json:"deployer"`
	SourceHost    string `json:"source_host"`
	CreatedAt     string `json:"created_at"`
}

func New(cfg config.Config, st *store.Store) *Service {
	cfg.ApplyDefaults()
	return &Service{Config: cfg, Store: st}
}

func (s *Service) Prepare(ctx context.Context, site string) (*PrepareResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	if err := os.MkdirAll(filepath.Join(siteDir, ".incoming"), 0o770); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(filepath.Join(siteDir, "releases"), 0o770); err != nil {
		return nil, err
	}
	if err := sites.EnsureSiteConfig(siteDir, site); err != nil {
		return nil, err
	}
	var out *PrepareResult
	err := withLock(filepath.Join(siteDir, "deploy.lock"), func() error {
		deployID, err := newDeployID()
		if err != nil {
			return err
		}
		staging := filepath.Join(siteDir, ".incoming", deployID, "files")
		if err := os.MkdirAll(staging, 0o770); err != nil {
			return err
		}
		absStaging, err := filepath.Abs(staging)
		if err != nil {
			return err
		}
		var linkDest *string
		if resolved, err := filepath.EvalSymlinks(filepath.Join(siteDir, "current")); err == nil {
			if abs, err := filepath.Abs(resolved); err == nil {
				linkDest = &abs
			}
		}
		out = &PrepareResult{FormatVersion: "1.0", Site: site, DeployID: deployID, StagingPath: absStaging, LinkDest: linkDest}
		return nil
	})
	if err != nil {
		return nil, err
	}
	if s.Store != nil {
		_, _ = s.Store.EnsureSite(ctx, site, site)
	}
	return out, nil
}

func (s *Service) DeleteSite(ctx context.Context, site string) (*DeleteResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	uploadsDir := sites.UploadsDir(s.Config.RemoteRoot, site)
	deleted := false
	if _, err := os.Stat(siteDir); err == nil {
		deleted = true
	} else if err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}
	if err := os.RemoveAll(siteDir); err != nil {
		return nil, err
	}
	if err := os.RemoveAll(uploadsDir); err != nil {
		return nil, err
	}
	if s.Store != nil {
		err := s.Store.DeleteSite(ctx, site)
		if err == nil {
			deleted = true
		} else if !errors.Is(err, store.ErrNotFound) {
			return nil, err
		}
	}
	return &DeleteResult{FormatVersion: "1.0", Site: site, Deleted: deleted}, nil
}

func (s *Service) Activate(ctx context.Context, site, deployID string) (*ActivateResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	if err := sites.ValidateDeployID(deployID); err != nil {
		return nil, err
	}
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	var res *ActivateResult
	err := withLock(filepath.Join(siteDir, "deploy.lock"), func() error {
		staging := filepath.Join(siteDir, ".incoming", deployID, "files")
		if err := ensurePathUnder(siteDir, staging); err != nil {
			return err
		}
		info, err := os.Stat(staging)
		if err != nil {
			return err
		}
		if !info.IsDir() {
			return fmt.Errorf("staging path is not a directory")
		}
		siteCfg, err := sites.ReadSiteConfig(siteDir)
		if err != nil {
			return err
		}
		if err := validateEntrypoint(staging, siteCfg); err != nil {
			return err
		}
		files, bytes, hash, err := validateTreeAndHash(staging)
		if err != nil {
			return err
		}
		manifest := Manifest{
			FormatVersion: "1.0",
			Site:          site,
			Release:       deployID,
			Files:         files,
			Bytes:         bytes,
			ContentHash:   "sha256:" + hash,
			Deployer:      deployer(),
			SourceHost:    sourceHost(),
			CreatedAt:     time.Now().UTC().Format(time.RFC3339Nano),
		}
		if err := writeManifest(staging, manifest); err != nil {
			return err
		}
		releaseDir := filepath.Join(siteDir, "releases", deployID)
		if err := os.MkdirAll(filepath.Dir(releaseDir), 0o770); err != nil {
			return err
		}
		if _, err := os.Stat(releaseDir); err == nil {
			return fmt.Errorf("release %s already exists", deployID)
		} else if !errors.Is(err, os.ErrNotExist) {
			return err
		}
		if err := os.Rename(staging, releaseDir); err != nil {
			return err
		}
		oldTarget, _ := os.Readlink(filepath.Join(siteDir, "current"))
		if err := atomicSymlinkSwap(siteDir, "current", filepath.Join("releases", deployID)); err != nil {
			return err
		}
		if oldTarget != "" {
			if err := atomicSymlinkSwap(siteDir, "previous", oldTarget); err != nil {
				return err
			}
		} else {
			_ = os.Remove(filepath.Join(siteDir, "previous"))
		}
		if s.Store != nil {
			if err := s.Store.RecordDeploy(ctx, site, deployID, manifest.Deployer, bytes, files); err != nil {
				return err
			}
		}
		if err := pruneReleases(siteDir, s.Config.RetainedReleases); err != nil {
			return err
		}
		if err := gcIncoming(siteDir, 24*time.Hour); err != nil {
			return err
		}
		res = &ActivateResult{FormatVersion: "1.0", Site: site, Release: deployID, URL: sites.URLFor(site, s.Config), Files: files, Bytes: bytes}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return res, nil
}

func withLock(path string, fn func() error) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o770); err != nil {
		return err
	}
	f, err := os.OpenFile(path, os.O_CREATE|os.O_RDWR, 0o660)
	if err != nil {
		return err
	}
	defer f.Close()
	if err := syscall.Flock(int(f.Fd()), syscall.LOCK_EX); err != nil {
		return err
	}
	defer syscall.Flock(int(f.Fd()), syscall.LOCK_UN)
	return fn()
}

func newDeployID() (string, error) {
	var b [3]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	return time.Now().UTC().Format("20060102T150405Z") + "-" + hex.EncodeToString(b[:]), nil
}

func ensurePathUnder(root, p string) error {
	rootAbs, err := filepath.Abs(root)
	if err != nil {
		return err
	}
	pAbs, err := filepath.Abs(p)
	if err != nil {
		return err
	}
	rel, err := filepath.Rel(rootAbs, pAbs)
	if err != nil {
		return err
	}
	if rel == ".." || strings.HasPrefix(rel, ".."+string(filepath.Separator)) || filepath.IsAbs(rel) {
		return fmt.Errorf("path %s is outside site directory", p)
	}
	return nil
}

func validateEntrypoint(root string, cfg sites.SiteConfig) error {
	if _, err := os.Stat(filepath.Join(root, "index.html")); err == nil {
		return nil
	} else if !errors.Is(err, os.ErrNotExist) {
		return err
	}
	fb := sites.SPAFallbackPath(cfg)
	if fb == "" || fb == "/index.html" {
		return fmt.Errorf("missing index.html")
	}
	candidate := filepath.Join(root, filepath.FromSlash(strings.TrimPrefix(fb, "/")))
	if !sites.PathWithin(root, candidate) {
		return fmt.Errorf("invalid spa fallback")
	}
	info, err := os.Stat(candidate)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return fmt.Errorf("missing index.html and spa fallback %s", fb)
		}
		return err
	}
	if info.IsDir() {
		return fmt.Errorf("spa fallback %s is a directory", fb)
	}
	return nil
}

func validateTreeAndHash(root string) (files int, bytes int64, sum string, err error) {
	h := sha256.New()
	rootAbs, err := filepath.Abs(root)
	if err != nil {
		return 0, 0, "", err
	}
	err = filepath.WalkDir(root, func(p string, d os.DirEntry, walkErr error) error {
		if walkErr != nil {
			return walkErr
		}
		if p == root {
			return nil
		}
		rel, err := filepath.Rel(root, p)
		if err != nil {
			return err
		}
		if d.Type()&os.ModeSymlink != 0 {
			target, err := os.Readlink(p)
			if err != nil {
				return err
			}
			resolved := target
			if !filepath.IsAbs(resolved) {
				resolved = filepath.Join(filepath.Dir(p), resolved)
			}
			resolved = filepath.Clean(resolved)
			if !sites.PathWithin(rootAbs, resolved) {
				return fmt.Errorf("symlink %s escapes release", rel)
			}
			return nil
		}
		info, err := d.Info()
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}
		if rel == ".quick-release.json" {
			return nil
		}
		f, err := os.Open(p)
		if err != nil {
			return err
		}
		defer f.Close()
		files++
		bytes += info.Size()
		io.WriteString(h, filepath.ToSlash(rel))
		io.WriteString(h, "\x00")
		io.WriteString(h, fmt.Sprintf("%d\x00", info.Size()))
		if _, err := io.Copy(h, f); err != nil {
			return err
		}
		io.WriteString(h, "\x00")
		return nil
	})
	if err != nil {
		return 0, 0, "", err
	}
	return files, bytes, hex.EncodeToString(h.Sum(nil)), nil
}

func writeManifest(root string, m Manifest) error {
	b, err := json.MarshalIndent(m, "", "  ")
	if err != nil {
		return err
	}
	b = append(b, '\n')
	return os.WriteFile(filepath.Join(root, ".quick-release.json"), b, 0o660)
}

func atomicSymlinkSwap(siteDir, name, target string) error {
	next := filepath.Join(siteDir, "."+name+".next")
	_ = os.Remove(next)
	if err := os.Symlink(target, next); err != nil {
		return err
	}
	return os.Rename(next, filepath.Join(siteDir, name))
}

func pruneReleases(siteDir string, retain int) error {
	if retain <= 0 {
		retain = 10
	}
	releasesDir := filepath.Join(siteDir, "releases")
	entries, err := os.ReadDir(releasesDir)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil
		}
		return err
	}
	type releaseInfo struct {
		name    string
		modTime time.Time
	}
	var releases []releaseInfo
	for _, e := range entries {
		if e.IsDir() {
			info, err := e.Info()
			if err != nil {
				return err
			}
			releases = append(releases, releaseInfo{name: e.Name(), modTime: info.ModTime()})
		}
	}
	sort.Slice(releases, func(i, j int) bool {
		if releases[i].modTime.Equal(releases[j].modTime) {
			return releases[i].name > releases[j].name
		}
		return releases[i].modTime.After(releases[j].modTime)
	})
	currentBase := linkBase(filepath.Join(siteDir, "current"))
	kept := map[string]bool{}
	for i, n := range releases {
		if i < retain || n.name == currentBase {
			kept[n.name] = true
		}
	}
	for _, n := range releases {
		if !kept[n.name] {
			if err := os.RemoveAll(filepath.Join(releasesDir, n.name)); err != nil {
				return err
			}
		}
	}
	if prev := linkBase(filepath.Join(siteDir, "previous")); prev != "" && !kept[prev] {
		_ = os.Remove(filepath.Join(siteDir, "previous"))
	}
	return nil
}

func linkBase(p string) string {
	t, err := os.Readlink(p)
	if err != nil {
		return ""
	}
	return filepath.Base(t)
}

func gcIncoming(siteDir string, olderThan time.Duration) error {
	incoming := filepath.Join(siteDir, ".incoming")
	entries, err := os.ReadDir(incoming)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil
		}
		return err
	}
	cutoff := time.Now().Add(-olderThan)
	for _, e := range entries {
		info, err := e.Info()
		if err != nil {
			continue
		}
		if info.IsDir() && info.ModTime().Before(cutoff) {
			if err := os.RemoveAll(filepath.Join(incoming, e.Name())); err != nil {
				return err
			}
		}
	}
	return nil
}

func deployer() string {
	for _, k := range []string{"QUICKD_DEPLOYER", "USER", "LOGNAME"} {
		if v := strings.TrimSpace(os.Getenv(k)); v != "" {
			return v
		}
	}
	return "unknown"
}

func sourceHost() string {
	if v := strings.TrimSpace(os.Getenv("QUICKD_SOURCE_HOST")); v != "" {
		return v
	}
	h, err := os.Hostname()
	if err != nil || h == "" {
		return "unknown"
	}
	return h
}
