package deploy

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
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
	"openquick.dev/quickd/internal/scan"
	"openquick.dev/quickd/internal/sites"
	"openquick.dev/quickd/internal/store"
)

type Service struct {
	Config config.Config
	Store  *store.Store
}

type PrepareOptions struct {
	Subdomain     string
	Deployer      string
	SSHUser       string
	SSHKeyID      string
	SSHPrincipals string
}

type ActivateOptions struct {
	Subdomain     string
	Deployer      string
	SSHUser       string
	SSHKeyID      string
	SSHPrincipals string
}

type PrepareResult struct {
	FormatVersion  string  `json:"format_version"`
	Site           string  `json:"site"`
	Subdomain      string  `json:"subdomain"`
	DeployID       string  `json:"deploy_id"`
	StagingPath    string  `json:"staging_path"`
	LinkDest       *string `json:"link_dest"`
	LastDeployer   *string `json:"last_deployer"`
	LastRelease    *string `json:"last_release"`
	LastDeployedAt *string `json:"last_deployed_at"`
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
	SSHUser       string `json:"ssh_user,omitempty"`
	SSHKeyID      string `json:"ssh_key_id,omitempty"`
	SSHPrincipals string `json:"ssh_principals,omitempty"`
	PublicKey     string `json:"public_key,omitempty"`
	Signature     string `json:"signature,omitempty"`
}

type VerifyResult struct {
	FormatVersion string   `json:"format_version"`
	OK            bool     `json:"ok"`
	Checked       int      `json:"checked"`
	Failures      []string `json:"failures"`
}

type deployMetadata struct {
	FormatVersion  string  `json:"format_version"`
	Site           string  `json:"site"`
	Subdomain      string  `json:"subdomain"`
	Deployer       string  `json:"deployer,omitempty"`
	SSHUser        string  `json:"ssh_user,omitempty"`
	SSHKeyID       string  `json:"ssh_key_id,omitempty"`
	SSHPrincipals  string  `json:"ssh_principals,omitempty"`
	LastDeployer   *string `json:"last_deployer"`
	LastRelease    *string `json:"last_release"`
	LastDeployedAt *string `json:"last_deployed_at"`
}

func New(cfg config.Config, st *store.Store) *Service {
	cfg.ApplyDefaults()
	return &Service{Config: cfg, Store: st}
}

func (s *Service) prepareSubdomain(ctx context.Context, site, requested string) (string, error) {
	requested = strings.ToLower(strings.TrimSpace(requested))
	if requested != "" {
		if err := sites.ValidateSubdomain(requested, s.Config.Deploy.ReservedNames); err != nil {
			return "", err
		}
		if err := s.ensureSubdomainAvailable(ctx, site, requested); err != nil {
			return "", err
		}
		return requested, nil
	}
	if s.Store != nil {
		if rec, err := s.Store.GetSite(ctx, site); err == nil && rec.Subdomain != "" {
			return rec.Subdomain, nil
		} else if err != nil && !errors.Is(err, store.ErrNotFound) {
			return "", err
		}
	}
	return site, nil
}

func (s *Service) ensureSubdomainAvailable(ctx context.Context, site, subdomain string) error {
	if s.Store == nil {
		return nil
	}
	rec, err := s.Store.GetSiteBySubdomain(ctx, subdomain)
	if err == nil && rec.Name != site {
		return fmt.Errorf("subdomain %q is already used by site %q", subdomain, rec.Name)
	}
	if err != nil && !errors.Is(err, store.ErrNotFound) {
		return err
	}
	return nil
}

func (s *Service) lastDeployPointers(ctx context.Context, site string) (*string, *string, *string) {
	if s.Store == nil {
		return nil, nil, nil
	}
	last, err := s.Store.LastDeploy(ctx, site)
	if err != nil {
		return nil, nil, nil
	}
	return ptrOrNil(last.Deployer), ptrOrNil(last.Release), ptrOrNil(last.CreatedAt)
}

func ptrOrNil(v string) *string {
	if strings.TrimSpace(v) == "" {
		return nil
	}
	vv := v
	return &vv
}

func (s *Service) ensureSiteConfig(siteDir, site, subdomain string, force bool) error {
	cfg, err := sites.ReadSiteConfig(siteDir)
	if err != nil {
		return err
	}
	if cfg.Name == "" {
		cfg.Name = site
	}
	if cfg.Subdomain == "" || force {
		cfg.Subdomain = subdomain
	}
	return sites.WriteSiteConfig(siteDir, cfg)
}

func writeDeployMetadata(incoming string, meta deployMetadata) error {
	b, err := json.MarshalIndent(meta, "", "  ")
	if err != nil {
		return err
	}
	b = append(b, '\n')
	return os.WriteFile(filepath.Join(incoming, ".quick-deploy.json"), b, 0o660)
}

func readDeployMetadata(incoming string) (deployMetadata, error) {
	b, err := os.ReadFile(filepath.Join(incoming, ".quick-deploy.json"))
	if err != nil {
		return deployMetadata{}, err
	}
	var meta deployMetadata
	if err := json.Unmarshal(b, &meta); err != nil {
		return deployMetadata{}, err
	}
	return meta, nil
}

func applyActivateOptions(meta *deployMetadata, opts ActivateOptions) {
	if opts.Subdomain != "" {
		meta.Subdomain = strings.ToLower(strings.TrimSpace(opts.Subdomain))
	}
	if opts.Deployer != "" {
		meta.Deployer = opts.Deployer
	}
	if opts.SSHUser != "" {
		meta.SSHUser = opts.SSHUser
	}
	if opts.SSHKeyID != "" {
		meta.SSHKeyID = opts.SSHKeyID
	}
	if opts.SSHPrincipals != "" {
		meta.SSHPrincipals = opts.SSHPrincipals
	}
}

func (s *Service) Prepare(ctx context.Context, site string) (*PrepareResult, error) {
	return s.PrepareWithOptions(ctx, site, PrepareOptions{})
}

func (s *Service) PrepareWithOptions(ctx context.Context, site string, opts PrepareOptions) (*PrepareResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	subdomain, err := s.prepareSubdomain(ctx, site, opts.Subdomain)
	if err != nil {
		return nil, err
	}
	lastDeployer, lastRelease, lastDeployedAt := s.lastDeployPointers(ctx, site)
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	if err := os.MkdirAll(filepath.Join(siteDir, ".incoming"), 0o770); err != nil {
		return nil, err
	}
	if err := os.MkdirAll(filepath.Join(siteDir, "releases"), 0o770); err != nil {
		return nil, err
	}
	if err := s.ensureSiteConfig(siteDir, site, subdomain, opts.Subdomain != ""); err != nil {
		return nil, err
	}
	var out *PrepareResult
	err = withLock(filepath.Join(siteDir, "deploy.lock"), func() error {
		deployID, err := newDeployID()
		if err != nil {
			return err
		}
		incoming := filepath.Join(siteDir, ".incoming", deployID)
		staging := filepath.Join(incoming, "files")
		if err := os.MkdirAll(staging, 0o770); err != nil {
			return err
		}
		meta := deployMetadata{FormatVersion: "1.0", Site: site, Subdomain: subdomain, Deployer: opts.Deployer, SSHUser: opts.SSHUser, SSHKeyID: opts.SSHKeyID, SSHPrincipals: opts.SSHPrincipals, LastDeployer: lastDeployer, LastRelease: lastRelease, LastDeployedAt: lastDeployedAt}
		if err := writeDeployMetadata(incoming, meta); err != nil {
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
		out = &PrepareResult{FormatVersion: "1.0", Site: site, Subdomain: subdomain, DeployID: deployID, StagingPath: absStaging, LinkDest: linkDest, LastDeployer: lastDeployer, LastRelease: lastRelease, LastDeployedAt: lastDeployedAt}
		return nil
	})
	if err != nil {
		return nil, err
	}
	if s.Store != nil {
		if _, err := s.Store.EnsureSite(ctx, site, subdomain); err != nil {
			return nil, err
		}
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
	return s.ActivateWithOptions(ctx, site, deployID, ActivateOptions{})
}

func (s *Service) ActivateWithOptions(ctx context.Context, site, deployID string, opts ActivateOptions) (*ActivateResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	if err := sites.ValidateDeployID(deployID); err != nil {
		return nil, err
	}
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	var res *ActivateResult
	err := withLock(filepath.Join(siteDir, "deploy.lock"), func() error {
		incoming := filepath.Join(siteDir, ".incoming", deployID)
		staging := filepath.Join(incoming, "files")
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
		meta, _ := readDeployMetadata(incoming)
		if meta.Subdomain == "" {
			if subdomain, err := s.prepareSubdomain(ctx, site, opts.Subdomain); err == nil {
				meta.Subdomain = subdomain
			} else {
				return err
			}
		}
		applyActivateOptions(&meta, opts)
		if err := sites.ValidateSubdomain(meta.Subdomain, s.Config.Deploy.ReservedNames); err != nil {
			return err
		}
		if err := s.ensureSubdomainAvailable(ctx, site, meta.Subdomain); err != nil {
			return err
		}
		if meta.Deployer == "" {
			meta.Deployer = deployer()
		}
		if meta.SSHUser == "" {
			meta.SSHUser = sshUser()
		}
		if s.Config.Deploy.RequireSSHCert && strings.TrimSpace(meta.SSHKeyID) == "" {
			return fmt.Errorf("deploy requires SSH certificate key id")
		}
		siteCfg, err := sites.ReadSiteConfig(siteDir)
		if err != nil {
			return err
		}
		releaseCfg, err := sites.ReadReleaseConfig(staging)
		if err != nil {
			return err
		}
		siteCfg = sites.MergeSiteConfig(siteCfg, releaseCfg)
		if err := validateEntrypoint(staging, siteCfg); err != nil {
			return err
		}
		if err := s.validatePublicRelease(ctx, site, staging); err != nil {
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
			Deployer:      meta.Deployer,
			SourceHost:    sourceHost(),
			CreatedAt:     time.Now().UTC().Format(time.RFC3339Nano),
			SSHUser:       meta.SSHUser,
			SSHKeyID:      meta.SSHKeyID,
			SSHPrincipals: meta.SSHPrincipals,
		}
		if err := s.signManifestIfConfigured(&manifest); err != nil {
			return err
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
			audit := store.DeployAudit{Subdomain: meta.Subdomain, SSHUser: meta.SSHUser, SSHKeyID: meta.SSHKeyID, SSHPrincipals: meta.SSHPrincipals}
			if err := s.Store.RecordDeploy(ctx, site, deployID, manifest.Deployer, bytes, files, audit); err != nil {
				return err
			}
		}
		if err := pruneReleases(siteDir, s.Config.RetainedReleases); err != nil {
			return err
		}
		if err := gcIncoming(siteDir, 24*time.Hour); err != nil {
			return err
		}
		res = &ActivateResult{FormatVersion: "1.0", Site: site, Release: deployID, URL: sites.URLForSubdomain(site, meta.Subdomain, s.Config), Files: files, Bytes: bytes}
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
	if cfg.Routing.DirectoryListing {
		return nil
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

func (s *Service) validatePublicRelease(ctx context.Context, site, root string) error {
	if s.Store == nil {
		return nil
	}
	public, err := s.Store.IsSitePublic(ctx, site)
	if errors.Is(err, store.ErrNotFound) {
		return nil
	}
	if err != nil {
		return err
	}
	if !public {
		return nil
	}
	report, err := scan.Scan(root, scan.DefaultLimits())
	if err != nil {
		return err
	}
	if !report.Static {
		return fmt.Errorf("public site deploy rejected: release uses /_quick APIs")
	}
	return nil
}

func (s *Service) signManifestIfConfigured(m *Manifest) error {
	if !s.Config.Deploy.Signing.Enabled && !s.Config.Deploy.Signing.Required {
		return nil
	}
	priv, pub, err := s.signingKey()
	if err != nil {
		if s.Config.Deploy.Signing.Required {
			return err
		}
		return nil
	}
	m.PublicKey = base64.StdEncoding.EncodeToString(pub)
	m.Signature = ""
	canonical, err := canonicalManifest(*m)
	if err != nil {
		return err
	}
	m.Signature = base64.StdEncoding.EncodeToString(ed25519.Sign(priv, canonical))
	return nil
}

func (s *Service) signingKey() (ed25519.PrivateKey, ed25519.PublicKey, error) {
	if err := os.MkdirAll(s.Config.DataDir, 0o750); err != nil {
		return nil, nil, err
	}
	path := filepath.Join(s.Config.DataDir, "signing.key")
	if b, err := os.ReadFile(path); err == nil {
		_ = os.Chmod(path, 0o600)
		key, err := decodePrivateKey(b)
		if err != nil {
			return nil, nil, err
		}
		pub, ok := key.Public().(ed25519.PublicKey)
		if !ok {
			return nil, nil, fmt.Errorf("invalid signing public key")
		}
		return key, pub, nil
	} else if !errors.Is(err, os.ErrNotExist) {
		return nil, nil, err
	}
	pub, priv, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return nil, nil, err
	}
	encoded := []byte(base64.StdEncoding.EncodeToString(priv))
	encoded = append(encoded, '\n')
	if err := os.WriteFile(path, encoded, 0o600); err != nil {
		return nil, nil, err
	}
	return priv, pub, nil
}

func decodePrivateKey(b []byte) (ed25519.PrivateKey, error) {
	trimmed := strings.TrimSpace(string(b))
	if decoded, err := base64.StdEncoding.DecodeString(trimmed); err == nil {
		b = decoded
	}
	if len(b) != ed25519.PrivateKeySize {
		return nil, fmt.Errorf("invalid signing key length")
	}
	return ed25519.PrivateKey(append([]byte(nil), b...)), nil
}

func canonicalManifest(m Manifest) ([]byte, error) {
	m.Signature = ""
	return json.Marshal(m)
}

func (s *Service) VerifyReleases(ctx context.Context, site, release string) (*VerifyResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	if release != "" {
		if err := sites.ValidateDeployID(release); err != nil {
			return nil, err
		}
	}
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	var releases []string
	if release != "" {
		releases = []string{release}
	} else if current := linkBase(filepath.Join(siteDir, "current")); current != "" {
		releases = []string{current}
	} else {
		entries, err := os.ReadDir(filepath.Join(siteDir, "releases"))
		if err != nil {
			if errors.Is(err, os.ErrNotExist) {
				return &VerifyResult{FormatVersion: "1.0", OK: true, Checked: 0}, nil
			}
			return nil, err
		}
		for _, e := range entries {
			if e.IsDir() {
				releases = append(releases, e.Name())
			}
		}
		sort.Strings(releases)
	}
	out := &VerifyResult{FormatVersion: "1.0", OK: true}
	for _, rel := range releases {
		out.Checked++
		if err := verifyRelease(filepath.Join(siteDir, "releases", rel)); err != nil {
			out.OK = false
			out.Failures = append(out.Failures, fmt.Sprintf("%s: %v", rel, err))
		}
	}
	return out, nil
}

func verifyRelease(root string) error {
	b, err := os.ReadFile(filepath.Join(root, ".quick-release.json"))
	if err != nil {
		return err
	}
	var m Manifest
	if err := json.Unmarshal(b, &m); err != nil {
		return err
	}
	if m.Signature == "" || m.PublicKey == "" {
		return fmt.Errorf("manifest is unsigned")
	}
	pub, err := base64.StdEncoding.DecodeString(m.PublicKey)
	if err != nil || len(pub) != ed25519.PublicKeySize {
		return fmt.Errorf("invalid public key")
	}
	sig, err := base64.StdEncoding.DecodeString(m.Signature)
	if err != nil || len(sig) != ed25519.SignatureSize {
		return fmt.Errorf("invalid signature")
	}
	canonical, err := canonicalManifest(m)
	if err != nil {
		return err
	}
	if !ed25519.Verify(ed25519.PublicKey(pub), canonical, sig) {
		return fmt.Errorf("signature verification failed")
	}
	files, bytes, hash, err := validateTreeAndHash(root)
	if err != nil {
		return err
	}
	if files != m.Files || bytes != m.Bytes || "sha256:"+hash != m.ContentHash {
		return fmt.Errorf("manifest content hash mismatch")
	}
	return nil
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

func sshUser() string {
	for _, k := range []string{"QUICK_SSH_USER", "SSH_USER", "USER", "LOGNAME"} {
		if v := strings.TrimSpace(os.Getenv(k)); v != "" {
			return v
		}
	}
	return ""
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
