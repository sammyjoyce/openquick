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
	Archive       string `json:"archive,omitempty"`
}

type RestoreResult struct {
	FormatVersion  string `json:"format_version"`
	Site           string `json:"site"`
	Archive        string `json:"archive"`
	Release        string `json:"release,omitempty"`
	URL            string `json:"url,omitempty"`
	Restored       bool   `json:"restored"`
	CleanupWarning string `json:"cleanup_warning,omitempty"`
}

type PurgeResult struct {
	FormatVersion string `json:"format_version"`
	Archive       string `json:"archive"`
	Purged        bool   `json:"purged"`
}

type CleanupResult struct {
	FormatVersion string `json:"format_version"`
	Site          string `json:"site"`
	DeployID      string `json:"deploy_id"`
	Path          string `json:"path"`
	Cleaned       bool   `json:"cleaned"`
}

type RollbackOptions struct {
	Release  string
	Deployer string
}

type RollbackResult struct {
	FormatVersion   string `json:"format_version"`
	Site            string `json:"site"`
	Release         string `json:"release"`
	PreviousRelease string `json:"previous_release,omitempty"`
	URL             string `json:"url,omitempty"`
	Deployer        string `json:"deployer,omitempty"`
	RolledBack      bool   `json:"rolled_back"`
}

type ReleaseRecord struct {
	Release      string `json:"release"`
	Current      bool   `json:"current"`
	Previous     bool   `json:"previous"`
	Deployer     string `json:"deployer,omitempty"`
	CreatedAt    string `json:"created_at,omitempty"`
	Verified     bool   `json:"verified"`
	Verification string `json:"verification"`
}

type ReleaseListResult struct {
	FormatVersion string          `json:"format_version"`
	Site          string          `json:"site"`
	Releases      []ReleaseRecord `json:"releases"`
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

const deleteArchiveTimestampLayout = "20060102T150405.000000000Z"

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
	archive := ""
	if _, err := os.Stat(siteDir); err == nil {
		deleted = true
	} else if err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}
	if deleted {
		archive = filepath.Join(s.Config.RemoteRoot, ".trash", "sites", fmt.Sprintf("%s-%s", site, time.Now().UTC().Format(deleteArchiveTimestampLayout)))
		if err := os.MkdirAll(archive, 0o770); err != nil {
			return nil, err
		}
		if err := os.Rename(siteDir, filepath.Join(archive, "site")); err != nil {
			return nil, err
		}
		if _, err := os.Stat(uploadsDir); err == nil {
			if err := os.Rename(uploadsDir, filepath.Join(archive, "uploads")); err != nil {
				return nil, err
			}
		} else if err != nil && !errors.Is(err, os.ErrNotExist) {
			return nil, err
		}
	} else if err := os.RemoveAll(uploadsDir); err != nil {
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
	return &DeleteResult{FormatVersion: "1.0", Site: site, Deleted: deleted, Archive: archive}, nil
}

func (s *Service) RestoreSite(ctx context.Context, site, archive string) (*RestoreResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	archive = strings.TrimSpace(archive)
	if archive == "" {
		return nil, fmt.Errorf("archive path is required")
	}
	archiveRoot := filepath.Join(s.Config.RemoteRoot, ".trash", "sites")
	if err := ensureDirectChildUnder(archiveRoot, archive); err != nil {
		return nil, err
	}
	if err := validateRestoreArchiveName(site, archive); err != nil {
		return nil, err
	}
	archivedSite := filepath.Join(archive, "site")
	if info, err := os.Stat(archivedSite); err != nil {
		return nil, err
	} else if !info.IsDir() {
		return nil, fmt.Errorf("archive site payload is not a directory")
	}
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	if _, err := os.Stat(siteDir); err == nil {
		return nil, fmt.Errorf("site %s already exists", site)
	} else if err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}
	subdomain := site
	if cfg, err := sites.ReadSiteConfig(archivedSite); err != nil {
		return nil, err
	} else if cfg.Subdomain != "" {
		subdomain = strings.TrimSpace(cfg.Subdomain)
	}
	if err := sites.ValidateSubdomain(subdomain, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	if err := s.ensureSubdomainAvailable(ctx, site, subdomain); err != nil {
		return nil, err
	}
	release := linkBase(filepath.Join(archivedSite, "current"))

	uploadsSrc := filepath.Join(archive, "uploads")
	uploadsDir := sites.UploadsDir(s.Config.RemoteRoot, site)
	siteMoved := false
	uploadsMoved := false
	rollback := func(cause error) error {
		var rollbackErrs []string
		if uploadsMoved {
			if err := os.Rename(uploadsDir, uploadsSrc); err != nil {
				rollbackErrs = append(rollbackErrs, fmt.Sprintf("uploads: %v", err))
			}
		}
		if siteMoved {
			if err := os.Rename(siteDir, archivedSite); err != nil {
				rollbackErrs = append(rollbackErrs, fmt.Sprintf("site: %v", err))
			}
		}
		if len(rollbackErrs) > 0 {
			return fmt.Errorf("%w (filesystem rollback failed: %s)", cause, strings.Join(rollbackErrs, "; "))
		}
		return cause
	}
	if err := os.MkdirAll(filepath.Dir(siteDir), 0o770); err != nil {
		return nil, err
	}
	if err := os.Rename(archivedSite, siteDir); err != nil {
		return nil, err
	}
	siteMoved = true
	if _, err := os.Stat(uploadsSrc); err == nil {
		if err := os.MkdirAll(filepath.Dir(uploadsDir), 0o770); err != nil {
			return nil, rollback(err)
		}
		if err := os.Rename(uploadsSrc, uploadsDir); err != nil {
			return nil, rollback(err)
		}
		uploadsMoved = true
	} else if err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, rollback(err)
	}

	if s.Store != nil {
		if release != "" {
			if err := s.Store.RecordDeploy(ctx, site, release, "restore", 0, 0, store.DeployAudit{Subdomain: subdomain}); err != nil {
				return nil, rollback(err)
			}
		} else if _, err := s.Store.EnsureSite(ctx, site, subdomain); err != nil {
			return nil, rollback(err)
		}
	}
	out := &RestoreResult{FormatVersion: "1.0", Site: site, Archive: archive, Release: release, URL: sites.URLForSubdomain(site, subdomain, s.Config), Restored: true}
	if err := os.RemoveAll(archive); err != nil {
		out.CleanupWarning = fmt.Sprintf("failed to remove archive %s: %v", archive, err)
	}
	return out, nil
}

func (s *Service) PurgeArchive(ctx context.Context, archive string) (*PurgeResult, error) {
	_ = ctx
	archive = strings.TrimSpace(archive)
	if archive == "" {
		return nil, fmt.Errorf("archive path is required")
	}
	archiveRoot := filepath.Join(s.Config.RemoteRoot, ".trash", "sites")
	if err := ensureDirectChildUnder(archiveRoot, archive); err != nil {
		return nil, err
	}
	if err := os.RemoveAll(archive); err != nil {
		return nil, err
	}
	return &PurgeResult{FormatVersion: "1.0", Archive: archive, Purged: true}, nil
}

func (s *Service) CleanupIncoming(ctx context.Context, site, deployID string) (*CleanupResult, error) {
	_ = ctx
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	if err := sites.ValidateDeployID(deployID); err != nil {
		return nil, err
	}
	incoming := sites.IncomingDir(s.Config.RemoteRoot, site)
	target := filepath.Join(incoming, deployID)
	if err := ensurePathUnder(incoming, target); err != nil {
		return nil, err
	}
	cleaned := true
	if err := os.RemoveAll(target); err != nil {
		return nil, err
	}
	return &CleanupResult{FormatVersion: "1.0", Site: site, DeployID: deployID, Path: target, Cleaned: cleaned}, nil
}

func (s *Service) Rollback(ctx context.Context, site string, opts RollbackOptions) (*RollbackResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	if info, err := os.Stat(siteDir); err != nil {
		return nil, fmt.Errorf("site %s unavailable: %w", site, err)
	} else if !info.IsDir() {
		return nil, fmt.Errorf("site %s is not a directory", site)
	}
	var out *RollbackResult
	err := withLock(filepath.Join(siteDir, "deploy.lock"), func() error {
		currentTarget, err := os.Readlink(filepath.Join(siteDir, "current"))
		if err != nil {
			return fmt.Errorf("current release unavailable: %w", err)
		}
		currentRelease := filepath.Base(currentTarget)
		target := strings.TrimSpace(opts.Release)
		if target == "" {
			target = linkBase(filepath.Join(siteDir, "previous"))
			if target == "" {
				return fmt.Errorf("previous release unavailable")
			}
		}
		if err := sites.ValidateDeployID(target); err != nil {
			return err
		}
		if target == currentRelease {
			return fmt.Errorf("release %s is already current", target)
		}
		targetDir := filepath.Join(siteDir, "releases", target)
		if info, err := os.Stat(targetDir); err != nil {
			return fmt.Errorf("release %s unavailable: %w", target, err)
		} else if !info.IsDir() {
			return fmt.Errorf("release %s is not a directory", target)
		}
		if err := verifyRelease(targetDir, s.Config.Deploy.Signing.Required); err != nil {
			return err
		}
		files, bytes, _, err := validateTreeAndHash(targetDir)
		if err != nil {
			return err
		}
		if err := atomicSymlinkSwap(siteDir, "current", filepath.Join("releases", target)); err != nil {
			return err
		}
		if currentTarget != "" {
			if err := atomicSymlinkSwap(siteDir, "previous", currentTarget); err != nil {
				return err
			}
		}
		rollbackDeployer := strings.TrimSpace(opts.Deployer)
		if rollbackDeployer == "" {
			rollbackDeployer = deployer()
		}
		auditDeployer := "rollback:" + rollbackDeployer
		subdomain := site
		if s.Store != nil {
			if rec, err := s.Store.GetSite(ctx, site); err == nil && rec.Subdomain != "" {
				subdomain = rec.Subdomain
			}
			if err := s.Store.RecordDeploy(ctx, site, target, auditDeployer, bytes, files, store.DeployAudit{Subdomain: subdomain}); err != nil {
				return err
			}
		}
		out = &RollbackResult{FormatVersion: "1.0", Site: site, Release: target, PreviousRelease: currentRelease, URL: sites.URLForSubdomain(site, subdomain, s.Config), Deployer: auditDeployer, RolledBack: true}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return out, nil
}

func (s *Service) ListReleases(ctx context.Context, site string) (*ReleaseListResult, error) {
	if err := sites.ValidateSiteName(site, s.Config.Deploy.ReservedNames); err != nil {
		return nil, err
	}
	siteDir := sites.SiteDir(s.Config.RemoteRoot, site)
	releaseDir := filepath.Join(siteDir, "releases")
	entries, err := os.ReadDir(releaseDir)
	if err != nil {
		if os.IsNotExist(err) {
			return &ReleaseListResult{FormatVersion: "1.0", Site: site, Releases: []ReleaseRecord{}}, nil
		}
		return nil, err
	}

	current := linkBase(filepath.Join(siteDir, "current"))
	previous := linkBase(filepath.Join(siteDir, "previous"))
	var audits []store.DeployRecord
	if s.Store != nil {
		audits, err = s.Store.DeploysForSite(ctx, site)
		if err != nil {
			return nil, err
		}
	}
	auditByRelease := make(map[string]store.DeployRecord, len(audits))
	for _, audit := range audits {
		if _, exists := auditByRelease[audit.Release]; !exists {
			auditByRelease[audit.Release] = audit
		}
	}

	releases := make([]ReleaseRecord, 0, len(entries))
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		name := entry.Name()
		rec := ReleaseRecord{Release: name, Current: name == current, Previous: name == previous}
		if audit, ok := auditByRelease[name]; ok {
			rec.Deployer = audit.Deployer
			rec.CreatedAt = audit.CreatedAt
		}
		if err := verifyRelease(filepath.Join(releaseDir, name), s.Config.Deploy.Signing.Required); err != nil {
			rec.Verification = err.Error()
		} else {
			rec.Verified = true
			rec.Verification = "ok"
		}
		releases = append(releases, rec)
	}
	sort.Slice(releases, func(i, j int) bool {
		return releases[i].Release > releases[j].Release
	})
	return &ReleaseListResult{FormatVersion: "1.0", Site: site, Releases: releases}, nil
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

func ensureDirectChildUnder(root, p string) error {
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
	if rel == "." || rel == ".." || strings.HasPrefix(rel, ".."+string(filepath.Separator)) || filepath.IsAbs(rel) || strings.Contains(rel, string(filepath.Separator)) {
		return fmt.Errorf("path %s is not a direct archive path", p)
	}
	return nil
}

func validateRestoreArchiveName(site, archive string) error {
	name := filepath.Base(archive)
	prefix := site + "-"
	if !strings.HasPrefix(name, prefix) || !restoreArchiveTimestampIsSafe(name[len(prefix):]) {
		return fmt.Errorf("archive %s does not match delete archive name pattern for site %s", archive, site)
	}
	return nil
}

func restoreArchiveTimestampIsSafe(value string) bool {
	if len(value) != len(deleteArchiveTimestampLayout) {
		return false
	}
	for i := 0; i < 8; i++ {
		if !asciiDigit(value[i]) {
			return false
		}
	}
	if value[8] != 'T' {
		return false
	}
	for i := 9; i < 15; i++ {
		if !asciiDigit(value[i]) {
			return false
		}
	}
	if value[15] != '.' {
		return false
	}
	for i := 16; i < 25; i++ {
		if !asciiDigit(value[i]) {
			return false
		}
	}
	return value[25] == 'Z'
}

func asciiDigit(ch byte) bool {
	return ch >= '0' && ch <= '9'
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
		return fmt.Errorf("public site deploy rejected: release uses /_quick APIs: %s", scan.FormatFindings(report.Findings, 5))
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
		if err := verifyRelease(filepath.Join(siteDir, "releases", rel), s.Config.Deploy.Signing.Required); err != nil {
			out.OK = false
			out.Failures = append(out.Failures, fmt.Sprintf("%s: %v", rel, err))
		}
	}
	return out, nil
}

func verifyRelease(root string, requireSignature bool) error {
	b, err := os.ReadFile(filepath.Join(root, ".quick-release.json"))
	if err != nil {
		return err
	}
	var m Manifest
	if err := json.Unmarshal(b, &m); err != nil {
		return err
	}
	if m.Signature == "" && m.PublicKey == "" {
		if requireSignature {
			return fmt.Errorf("manifest is unsigned")
		}
	} else {
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
