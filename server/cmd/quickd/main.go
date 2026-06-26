package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"openquick.dev/quickd/internal/api"
	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/deploy"
	"openquick.dev/quickd/internal/identity"
	"openquick.dev/quickd/internal/scan"
	"openquick.dev/quickd/internal/sites"
	"openquick.dev/quickd/internal/static"
	"openquick.dev/quickd/internal/store"
)

func main() {
	if err := run(os.Args[1:]); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func run(args []string) error {
	if len(args) == 0 {
		return errors.New("usage: quickd <serve|deploy|releases|list|sites|domains|audit|admin|doctor|config>")
	}
	switch args[0] {
	case "serve":
		return serveCmd(args[1:])
	case "deploy":
		return deployCmd(args[1:])
	case "releases":
		return releasesCmd(args[1:])
	case "list":
		return listCmd(args[1:])
	case "sites":
		return sitesCmd(args[1:])
	case "domains":
		return domainsCmd(args[1:])
	case "audit":
		return auditCmd(args[1:])
	case "admin":
		return adminCmd(args[1:])
	case "doctor":
		return doctorCmd(args[1:])
	case "config":
		return configCmd(args[1:])
	case "help", "-h", "--help":
		fmt.Println("usage: quickd <serve|deploy|releases|list|sites|domains|audit|admin|doctor|config>")
		return nil
	default:
		return fmt.Errorf("unknown command %q", args[0])
	}
}

type adminFlags struct {
	json bool
	root string
}

func addAdminFlags(fs *flag.FlagSet, f *adminFlags) {
	fs.BoolVar(&f.json, "json", false, "emit JSON")
	fs.StringVar(&f.root, "root", config.RootFromEnv(), "quickd root")
}

func loadAdmin(root string) (config.Config, *store.Store, error) {
	cfg, err := config.LoadForRoot(root)
	if err != nil {
		return config.Config{}, nil, err
	}
	cfg.RemoteRoot = root
	cfg.ApplyDefaults()
	st, err := store.Open(cfg.DataDir)
	if err != nil {
		return config.Config{}, nil, err
	}
	return cfg, st, nil
}

func configCmd(args []string) error {
	if len(args) == 0 {
		return errors.New("usage: quickd config <check|explain>")
	}
	fs := flag.NewFlagSet("quickd config "+args[0], flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	var af adminFlags
	addAdminFlags(fs, &af)
	if err := fs.Parse(args[1:]); err != nil {
		return err
	}
	path := filepath.Join(af.root, "config", "quickd.json")
	cfg, err := config.LoadForRoot(af.root)
	if err != nil {
		return err
	}
	summary := map[string]any{
		"format_version":    "1.0",
		"ok":                true,
		"path":              path,
		"exists":            fileExists(path),
		"listen":            cfg.Listen,
		"remote_root":       cfg.RemoteRoot,
		"data_dir":          cfg.DataDir,
		"iap":               cfg.IAP.Type,
		"viewer_anonymous":  cfg.Viewer.AllowAnonymous,
		"directory_enabled": cfg.Directory.Enabled,
		"public_static":     cfg.PublicStatic.Enabled,
		"ai_enabled":        cfg.AI.Enabled,
		"warehouse_enabled": cfg.Warehouse.Enabled,
		"http_deploy":       cfg.HTTPDeploy.Enabled,
		"retained_releases": cfg.RetainedReleases,
		"max_upload_bytes":  cfg.MaxUploadBytes,
	}
	switch args[0] {
	case "check":
		return printResult(af.json, summary)
	case "explain":
		summary["defaults"] = map[string]any{
			"listen":            "127.0.0.1:9366",
			"data_dir":          "<remote_root>/data",
			"remote_root":       af.root,
			"iap":               "none",
			"retained_releases": 10,
			"max_upload_bytes":  100 << 20,
		}
		summary["secret_policy"] = "secret values are read from environment variables such as api_key_env and are not printed"
		return printResult(af.json, summary)
	default:
		return fmt.Errorf("unknown config command %q", args[0])
	}
}

func fileExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}

func defaultDeployer() string {
	for _, k := range []string{"QUICKD_DEPLOYER", "USER", "LOGNAME"} {
		if v := strings.TrimSpace(os.Getenv(k)); v != "" {
			return v
		}
	}
	return "unknown"
}

func defaultSSHUser() string {
	for _, k := range []string{"QUICK_SSH_USER", "SSH_USER", "USER", "LOGNAME"} {
		if v := strings.TrimSpace(os.Getenv(k)); v != "" {
			return v
		}
	}
	return ""
}

func deployCmd(args []string) error {
	if len(args) == 0 {
		return errors.New("usage: quickd deploy <prepare|activate|cleanup>")
	}
	switch args[0] {
	case "prepare":
		fs := flag.NewFlagSet("quickd deploy prepare", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var site, subdomain, deployerName, sshUser, sshKeyID, sshPrincipals string
		addAdminFlags(fs, &af)
		fs.StringVar(&site, "site", "", "site slug")
		fs.StringVar(&subdomain, "subdomain", "", "public subdomain label")
		fs.StringVar(&deployerName, "deployer", defaultDeployer(), "deployer identity")
		fs.StringVar(&sshUser, "ssh-user", defaultSSHUser(), "SSH username")
		fs.StringVar(&sshKeyID, "ssh-key-id", os.Getenv("QUICK_SSH_KEY_ID"), "SSH certificate key id")
		fs.StringVar(&sshPrincipals, "ssh-principals", os.Getenv("QUICK_SSH_PRINCIPALS"), "comma-separated SSH principals")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if site == "" {
			return errors.New("--site is required")
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		res, err := deploy.New(cfg, st).PrepareWithOptions(context.Background(), site, deploy.PrepareOptions{Subdomain: subdomain, Deployer: deployerName, SSHUser: sshUser, SSHKeyID: sshKeyID, SSHPrincipals: sshPrincipals})
		if err != nil {
			return err
		}
		return printResult(af.json, res)
	case "activate":
		fs := flag.NewFlagSet("quickd deploy activate", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var site, deployID, subdomain, deployerName, sshUser, sshKeyID, sshPrincipals string
		addAdminFlags(fs, &af)
		fs.StringVar(&site, "site", "", "site slug")
		fs.StringVar(&deployID, "deploy-id", "", "deploy id")
		fs.StringVar(&subdomain, "subdomain", "", "public subdomain label")
		fs.StringVar(&deployerName, "deployer", defaultDeployer(), "deployer identity")
		fs.StringVar(&sshUser, "ssh-user", defaultSSHUser(), "SSH username")
		fs.StringVar(&sshKeyID, "ssh-key-id", os.Getenv("QUICK_SSH_KEY_ID"), "SSH certificate key id")
		fs.StringVar(&sshPrincipals, "ssh-principals", os.Getenv("QUICK_SSH_PRINCIPALS"), "comma-separated SSH principals")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if site == "" || deployID == "" {
			return errors.New("--site and --deploy-id are required")
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		res, err := deploy.New(cfg, st).ActivateWithOptions(context.Background(), site, deployID, deploy.ActivateOptions{Subdomain: subdomain, Deployer: deployerName, SSHUser: sshUser, SSHKeyID: sshKeyID, SSHPrincipals: sshPrincipals})
		if err != nil {
			return err
		}
		return printResult(af.json, res)
	case "cleanup":
		fs := flag.NewFlagSet("quickd deploy cleanup", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var site, deployID string
		addAdminFlags(fs, &af)
		fs.StringVar(&site, "site", "", "site slug")
		fs.StringVar(&deployID, "deploy-id", "", "deploy id")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if site == "" || deployID == "" {
			return errors.New("--site and --deploy-id are required")
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		res, err := deploy.New(cfg, st).CleanupIncoming(context.Background(), site, deployID)
		if err != nil {
			return err
		}
		return printResult(af.json, res)
	case "extract-zip":
		fs := flag.NewFlagSet("quickd deploy extract-zip", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var site, deployID, zipPath string
		addAdminFlags(fs, &af)
		fs.StringVar(&site, "site", "", "site slug")
		fs.StringVar(&deployID, "deploy-id", "", "deploy id")
		fs.StringVar(&zipPath, "zip", "", "zip file path")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if site == "" || deployID == "" || zipPath == "" {
			return errors.New("--site, --deploy-id, and --zip are required")
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		if err := sites.ValidateSiteName(site, cfg.Deploy.ReservedNames); err != nil {
			return err
		}
		if err := sites.ValidateDeployID(deployID); err != nil {
			return err
		}
		staging := filepath.Join(sites.IncomingDir(cfg.RemoteRoot, site), deployID, "files")
		if err := deploy.ExtractZip(zipPath, staging, deploy.DefaultZipCaps()); err != nil {
			return err
		}
		return printResult(af.json, map[string]any{"format_version": "1.0", "site": site, "deploy_id": deployID, "extracted": true, "staging_path": staging})
	default:
		return fmt.Errorf("unknown deploy command %q", args[0])
	}
}

func listCmd(args []string) error {
	fs := flag.NewFlagSet("quickd list", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	var af adminFlags
	addAdminFlags(fs, &af)
	if err := fs.Parse(args); err != nil {
		return err
	}
	cfg, st, err := loadAdmin(af.root)
	if err != nil {
		return err
	}
	defer st.Close()
	recs, err := st.ListSites(context.Background())
	if err != nil {
		return err
	}
	for i := range recs {
		recs[i].URL = sites.URLForSubdomain(recs[i].Name, recs[i].Subdomain, cfg)
	}
	return printResult(af.json, map[string]any{"format_version": "1.0", "sites": recs})
}

func parseSitesAdminArgs(args []string) (adminFlags, string, error) {
	af := adminFlags{root: config.RootFromEnv()}
	var site string
	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch {
		case arg == "--json":
			af.json = true
		case arg == "--root":
			if i+1 >= len(args) {
				return af, site, errors.New("--root requires a value")
			}
			i++
			af.root = args[i]
		case len(arg) > len("--root=") && arg[:len("--root=")] == "--root=":
			af.root = arg[len("--root="):]
		case len(arg) > 0 && arg[0] == '-':
			return af, site, fmt.Errorf("flag provided but not defined: %s", arg)
		default:
			if site != "" {
				return af, site, errors.New("site name is required")
			}
			site = arg
		}
	}
	if site == "" {
		return af, site, errors.New("site name is required")
	}
	return af, site, nil
}

func sitesCmd(args []string) error {
	if len(args) == 0 {
		return errors.New("usage: quickd sites <get|delete> <site> [--json]")
	}
	switch args[0] {
	case "get":
		af, siteName, err := parseSitesAdminArgs(args[1:])
		if err != nil {
			return err
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		rec, err := st.GetSite(context.Background(), siteName)
		if err != nil {
			return err
		}
		rec.URL = sites.URLForSubdomain(rec.Name, rec.Subdomain, cfg)
		return printResult(af.json, map[string]any{"format_version": "1.0", "site": rec})
	case "delete":
		af, siteName, err := parseSitesAdminArgs(args[1:])
		if err != nil {
			return err
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		res, err := deploy.New(cfg, st).DeleteSite(context.Background(), siteName)
		if err != nil {
			return err
		}
		return printResult(af.json, res)
	case "public":
		return sitesPublicCmd(args[1:])
	case "restore":
		return sitesRestoreCmd(args[1:])
	case "purge":
		return sitesPurgeCmd(args[1:])
	default:
		return fmt.Errorf("unknown sites command %q", args[0])
	}
}

func sitesRestoreCmd(args []string) error {
	fs := flag.NewFlagSet("quickd sites restore", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	var af adminFlags
	var archive string
	addAdminFlags(fs, &af)
	fs.StringVar(&archive, "from", "", "archive path returned by delete")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if fs.NArg() != 1 || archive == "" {
		return errors.New("usage: quickd sites restore <site> --from <archive> [--json]")
	}
	cfg, st, err := loadAdmin(af.root)
	if err != nil {
		return err
	}
	defer st.Close()
	res, err := deploy.New(cfg, st).RestoreSite(context.Background(), fs.Arg(0), archive)
	if err != nil {
		return err
	}
	return printResult(af.json, res)
}

func sitesPurgeCmd(args []string) error {
	fs := flag.NewFlagSet("quickd sites purge", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	var af adminFlags
	var archive string
	addAdminFlags(fs, &af)
	fs.StringVar(&archive, "from", "", "archive path returned by delete")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if archive == "" {
		return errors.New("usage: quickd sites purge --from <archive> [--json]")
	}
	cfg, st, err := loadAdmin(af.root)
	if err != nil {
		return err
	}
	defer st.Close()
	res, err := deploy.New(cfg, st).PurgeArchive(context.Background(), archive)
	if err != nil {
		return err
	}
	return printResult(af.json, res)
}

func sitesPublicCmd(args []string) error {
	af := adminFlags{root: config.RootFromEnv()}
	var siteName string
	var on, off bool
	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch {
		case arg == "--json":
			af.json = true
		case arg == "--on":
			on = true
		case arg == "--off":
			off = true
		case arg == "--root":
			if i+1 >= len(args) {
				return errors.New("--root requires a value")
			}
			i++
			af.root = args[i]
		case strings.HasPrefix(arg, "--root="):
			af.root = strings.TrimPrefix(arg, "--root=")
		case strings.HasPrefix(arg, "-"):
			return fmt.Errorf("flag provided but not defined: %s", arg)
		default:
			if siteName != "" {
				return errors.New("site name is required")
			}
			siteName = arg
		}
	}
	if siteName == "" || on == off {
		return errors.New("usage: quickd sites public <site> (--on|--off) [--json]")
	}
	cfg, st, err := loadAdmin(af.root)
	if err != nil {
		return err
	}
	defer st.Close()
	if err := sites.ValidateSiteName(siteName, cfg.Deploy.ReservedNames); err != nil {
		return err
	}
	if _, err := st.GetSite(context.Background(), siteName); err != nil {
		return err
	}
	if on {
		if !cfg.PublicStatic.Enabled {
			return errors.New("public_static.enabled is false")
		}
		root, err := filepath.EvalSymlinks(sites.CurrentPath(cfg.RemoteRoot, siteName))
		if err != nil {
			return err
		}
		report, err := scan.Scan(root, scan.DefaultLimits())
		if err != nil {
			return err
		}
		if !report.Static {
			return fmt.Errorf("site %s is not static-only: %s", siteName, scan.FormatFindings(report.Findings, 5))
		}
	}
	if err := st.SetSitePublic(context.Background(), siteName, on); err != nil {
		return err
	}
	return printResult(af.json, map[string]any{"format_version": "1.0", "site": siteName, "public": on})
}

func releasesCmd(args []string) error {
	if len(args) == 0 {
		return errors.New("usage: quickd releases <list|verify|rollback>")
	}
	switch args[0] {
	case "list":
		fs := flag.NewFlagSet("quickd releases list", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var siteName string
		addAdminFlags(fs, &af)
		fs.StringVar(&siteName, "site", "", "site slug")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if siteName == "" {
			return errors.New("--site is required")
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		res, err := deploy.New(cfg, st).ListReleases(context.Background(), siteName)
		if err != nil {
			return err
		}
		return printResult(af.json, res)
	case "verify":
		fs := flag.NewFlagSet("quickd releases verify", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var siteName, release string
		addAdminFlags(fs, &af)
		fs.StringVar(&siteName, "site", "", "site slug")
		fs.StringVar(&release, "release", "", "release id")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if siteName == "" {
			return errors.New("--site is required")
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		res, err := deploy.New(cfg, st).VerifyReleases(context.Background(), siteName, release)
		if err != nil {
			return err
		}
		return printResult(af.json, res)
	case "rollback":
		fs := flag.NewFlagSet("quickd releases rollback", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var siteName, release, deployerName string
		addAdminFlags(fs, &af)
		fs.StringVar(&siteName, "site", "", "site slug")
		fs.StringVar(&release, "to", "", "release id to restore; defaults to previous")
		fs.StringVar(&release, "release", "", "release id to restore; defaults to previous")
		fs.StringVar(&deployerName, "deployer", defaultDeployer(), "deployer identity")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if siteName == "" {
			return errors.New("--site is required")
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		res, err := deploy.New(cfg, st).Rollback(context.Background(), siteName, deploy.RollbackOptions{Release: release, Deployer: deployerName})
		if err != nil {
			return err
		}
		return printResult(af.json, res)
	default:
		return fmt.Errorf("unknown releases command %q", args[0])
	}
}

func parseDomainAddArgs(args []string) (adminFlags, string, string, error) {
	af := adminFlags{root: config.RootFromEnv()}
	var domainArg, siteName string
	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch {
		case arg == "--json":
			af.json = true
		case arg == "--root":
			if i+1 >= len(args) {
				return af, "", "", errors.New("--root requires a value")
			}
			i++
			af.root = args[i]
		case strings.HasPrefix(arg, "--root="):
			af.root = strings.TrimPrefix(arg, "--root=")
		case arg == "--site":
			if i+1 >= len(args) {
				return af, "", "", errors.New("--site requires a value")
			}
			i++
			siteName = args[i]
		case strings.HasPrefix(arg, "--site="):
			siteName = strings.TrimPrefix(arg, "--site=")
		case strings.HasPrefix(arg, "-"):
			return af, "", "", fmt.Errorf("flag provided but not defined: %s", arg)
		default:
			if domainArg != "" {
				return af, "", "", errors.New("domain is required")
			}
			domainArg = arg
		}
	}
	if domainArg == "" || siteName == "" {
		return af, "", "", errors.New("usage: quickd domains add <domain> --site <site> [--json]")
	}
	return af, domainArg, siteName, nil
}

func parseDomainRemoveArgs(args []string) (adminFlags, string, error) {
	af := adminFlags{root: config.RootFromEnv()}
	var domainArg string
	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch {
		case arg == "--json":
			af.json = true
		case arg == "--root":
			if i+1 >= len(args) {
				return af, "", errors.New("--root requires a value")
			}
			i++
			af.root = args[i]
		case strings.HasPrefix(arg, "--root="):
			af.root = strings.TrimPrefix(arg, "--root=")
		case strings.HasPrefix(arg, "-"):
			return af, "", fmt.Errorf("flag provided but not defined: %s", arg)
		default:
			if domainArg != "" {
				return af, "", errors.New("domain is required")
			}
			domainArg = arg
		}
	}
	if domainArg == "" {
		return af, "", errors.New("usage: quickd domains remove <domain> [--json]")
	}
	return af, domainArg, nil
}

type domainReadiness struct {
	Status      string   `json:"status"`
	DNS         string   `json:"dns"`
	TLS         string   `json:"tls"`
	Addresses   []string `json:"addresses,omitempty"`
	Remediation string   `json:"remediation,omitempty"`
}

type domainListRecord struct {
	store.DomainRecord
	Readiness domainReadiness `json:"readiness"`
}

type domainLookupFunc func(context.Context, string) ([]string, error)

func assessDomainReadiness(ctx context.Context, domain string, lookup domainLookupFunc) domainReadiness {
	if lookup == nil {
		lookup = net.DefaultResolver.LookupHost
	}
	ctx, cancel := context.WithTimeout(ctx, 500*time.Millisecond)
	defer cancel()
	addrs, err := lookup(ctx, domain)
	if err != nil || len(addrs) == 0 {
		return domainReadiness{
			Status:      "fail",
			DNS:         "fail",
			TLS:         "pending",
			Remediation: "create A/AAAA or CNAME records that point this domain at the OpenQuick host, then retry after DNS propagates",
		}
	}
	return domainReadiness{Status: "ok", DNS: "ok", TLS: "ok", Addresses: addrs}
}

func domainsWithReadiness(ctx context.Context, recs []store.DomainRecord, lookup domainLookupFunc) []domainListRecord {
	type result struct {
		index int
		rec   domainListRecord
	}
	out := make([]domainListRecord, len(recs))
	ch := make(chan result, len(recs))
	for i, rec := range recs {
		go func(index int, record store.DomainRecord) {
			ch <- result{index: index, rec: domainListRecord{DomainRecord: record, Readiness: assessDomainReadiness(ctx, record.Domain, lookup)}}
		}(i, rec)
	}
	for range recs {
		res := <-ch
		out[res.index] = res.rec
	}
	return out
}

func domainsCmd(args []string) error {
	if len(args) == 0 {
		return errors.New("usage: quickd domains <add|remove|list>")
	}
	switch args[0] {
	case "add":
		af, domainArg, siteName, err := parseDomainAddArgs(args[1:])
		if err != nil {
			return err
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		if err := sites.ValidateSiteName(siteName, cfg.Deploy.ReservedNames); err != nil {
			return err
		}
		domain, err := sites.ValidateDomain(domainArg, cfg)
		if err != nil {
			return err
		}
		rec, err := st.AddDomain(context.Background(), domain, siteName)
		if err != nil {
			return err
		}
		return printResult(af.json, map[string]any{"format_version": "1.0", "domain": rec})
	case "remove":
		af, domainArg, err := parseDomainRemoveArgs(args[1:])
		if err != nil {
			return err
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		domain, err := sites.ValidateDomain(domainArg, cfg)
		if err != nil {
			return err
		}
		if err := st.RemoveDomain(context.Background(), domain); err != nil {
			return err
		}
		return printResult(af.json, map[string]any{"format_version": "1.0", "domain": domain, "removed": true})
	case "list":
		fs := flag.NewFlagSet("quickd domains list", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		addAdminFlags(fs, &af)
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		_, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		ctx := context.Background()
		recs, err := st.ListDomains(ctx)
		if err != nil {
			return err
		}
		return printResult(af.json, map[string]any{"format_version": "1.0", "domains": domainsWithReadiness(ctx, recs, nil)})
	default:
		return fmt.Errorf("unknown domains command %q", args[0])
	}
}

func auditCmd(args []string) error {
	if len(args) == 0 || args[0] != "export" {
		return errors.New("usage: quickd audit export [--json]")
	}
	fs := flag.NewFlagSet("quickd audit export", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	var af adminFlags
	addAdminFlags(fs, &af)
	if err := fs.Parse(args[1:]); err != nil {
		return err
	}
	_, st, err := loadAdmin(af.root)
	if err != nil {
		return err
	}
	defer st.Close()
	events, err := st.ExportAudit(context.Background())
	if err != nil {
		return err
	}
	return printResult(af.json, map[string]any{"format_version": "1.0", "events": events})
}

func adminCmd(args []string) error {
	if len(args) == 0 {
		return errors.New("usage: quickd admin <stats|mint-dev-token>")
	}
	switch args[0] {
	case "stats":
		fs := flag.NewFlagSet("quickd admin stats", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		addAdminFlags(fs, &af)
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		siteCount, err := st.CountSites(context.Background())
		if err != nil {
			return err
		}
		releaseCount, err := st.CountReleases(context.Background())
		if err != nil {
			return err
		}
		recent, err := st.RecentDeploys(context.Background(), 10)
		if err != nil {
			return err
		}
		rates, err := st.RateLimitCounts(context.Background())
		if err != nil {
			return err
		}
		stats := map[string]any{
			"format_version": "1.0",
			"sites":          siteCount,
			"releases":       releaseCount,
			"disk": map[string]int64{
				"sites_bytes":   dirSize(filepath.Join(cfg.RemoteRoot, "sites")),
				"uploads_bytes": dirSize(filepath.Join(cfg.RemoteRoot, "uploads")),
				"db_bytes":      dirSize(cfg.DataDir),
			},
			"recent_deploys": recent,
			"rate_limits":    rates,
		}
		return printResult(af.json, stats)
	case "mint-dev-token":
		fs := flag.NewFlagSet("quickd admin mint-dev-token", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var siteName string
		var ttlSeconds int
		addAdminFlags(fs, &af)
		fs.StringVar(&siteName, "site", "", "site slug")
		fs.IntVar(&ttlSeconds, "ttl", 3600, "token lifetime in seconds")
		if err := fs.Parse(args[1:]); err != nil {
			return err
		}
		if siteName == "" {
			return errors.New("--site is required")
		}
		if ttlSeconds <= 0 {
			return errors.New("--ttl must be positive")
		}
		cfg, st, err := loadAdmin(af.root)
		if err != nil {
			return err
		}
		defer st.Close()
		if err := sites.ValidateSiteName(siteName, cfg.Deploy.ReservedNames); err != nil {
			return err
		}
		token, expiresAt, err := st.MintDevToken(context.Background(), siteName, defaultDeployer(), time.Duration(ttlSeconds)*time.Second)
		if err != nil {
			return err
		}
		return printResult(af.json, map[string]any{"token": token, "site": siteName, "expires_at": expiresAt})
	default:
		return fmt.Errorf("unknown admin command %q", args[0])
	}
}

func dirSize(root string) int64 {
	var total int64
	_ = filepath.WalkDir(root, func(p string, d os.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return nil
		}
		info, err := d.Info()
		if err == nil {
			total += info.Size()
		}
		return nil
	})
	return total
}

type doctorCheck struct {
	Name        string `json:"name"`
	Group       string `json:"group"`
	Status      string `json:"status"`
	Detail      string `json:"detail"`
	Remediation string `json:"remediation"`
}

func checkDir(dir string) error {
	info, err := os.Stat(dir)
	if err != nil {
		return err
	}
	if !info.IsDir() {
		return fmt.Errorf("%s is not a directory", dir)
	}
	return nil
}

func checkWritableDir(dir string) error {
	if err := checkDir(dir); err != nil {
		return err
	}
	probe := filepath.Join(dir, ".quickd-doctor-write-test")
	if err := os.WriteFile(probe, []byte("ok"), 0o600); err != nil {
		return err
	}
	return os.Remove(probe)
}

func doctorAIChecks(cfg config.Config) []doctorCheck {
	if !cfg.AI.Enabled || len(cfg.AI.Providers) == 0 {
		return []doctorCheck{{Name: "ai", Group: "ai", Status: "ok", Detail: "disabled", Remediation: ""}}
	}
	checks := make([]doctorCheck, 0, len(cfg.AI.Providers))
	for _, p := range cfg.AI.Providers {
		name := "ai_provider_" + p.Name
		detail := fmt.Sprintf("%s env %s", p.Name, p.APIKeyEnv)
		if strings.TrimSpace(p.APIKeyEnv) == "" {
			checks = append(checks, doctorCheck{Name: name, Group: "ai", Status: "warn", Detail: p.Name + " missing api_key_env", Remediation: "set ai.providers[].api_key_env to an environment variable name"})
			continue
		}
		if strings.TrimSpace(os.Getenv(p.APIKeyEnv)) == "" {
			checks = append(checks, doctorCheck{Name: name, Group: "ai", Status: "warn", Detail: detail + " missing", Remediation: "export " + p.APIKeyEnv + " for quickd"})
			continue
		}
		checks = append(checks, doctorCheck{Name: name, Group: "ai", Status: "ok", Detail: detail + " present", Remediation: ""})
	}
	return checks
}

func doctorWarehouseChecks(cfg config.Config) []doctorCheck {
	if !cfg.Warehouse.Enabled || len(cfg.Warehouse.Queries) == 0 {
		return []doctorCheck{{Name: "warehouse", Group: "warehouse", Status: "ok", Detail: "disabled", Remediation: ""}}
	}
	if err := cfg.ValidateWarehouse(); err != nil {
		return []doctorCheck{{Name: "warehouse_queries", Group: "warehouse", Status: "fail", Detail: err.Error(), Remediation: "fix warehouse query SQL and params"}}
	}
	return []doctorCheck{{Name: "warehouse_queries", Group: "warehouse", Status: "ok", Detail: fmt.Sprintf("%d queries", len(cfg.Warehouse.Queries)), Remediation: ""}}
}

func doctorCmd(args []string) error {
	fs := flag.NewFlagSet("quickd doctor", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	var af adminFlags
	var host bool
	addAdminFlags(fs, &af)
	fs.BoolVar(&host, "host", false, "host checks")
	if err := fs.Parse(args); err != nil {
		return err
	}
	checks := []doctorCheck{}
	cfg, err := config.LoadForRoot(af.root)
	if err != nil {
		checks = append(checks, doctorCheck{Name: "config", Group: "host", Status: "fail", Detail: err.Error(), Remediation: "fix quickd.json"})
	} else {
		cfg.RemoteRoot = af.root
		cfg.ApplyDefaults()
		checks = append(checks, doctorCheck{Name: "config", Group: "host", Status: "ok", Detail: cfg.RemoteRoot, Remediation: ""})
		if err := checkWritableDir(cfg.RemoteRoot); err != nil {
			checks = append(checks, doctorCheck{Name: "remote_root", Group: "host", Status: "fail", Detail: err.Error(), Remediation: "create /srv/quick and make it writable/searchable by quick and deployers"})
		} else {
			checks = append(checks, doctorCheck{Name: "remote_root", Group: "host", Status: "ok", Detail: cfg.RemoteRoot, Remediation: ""})
		}
		st, err := store.Open(cfg.DataDir)
		if err != nil {
			checks = append(checks, doctorCheck{Name: "sqlite", Group: "store", Status: "fail", Detail: err.Error(), Remediation: "check data_dir permissions"})
		} else {
			if err := st.Check(); err != nil {
				checks = append(checks, doctorCheck{Name: "sqlite", Group: "store", Status: "fail", Detail: err.Error(), Remediation: "repair database"})
			} else {
				checks = append(checks, doctorCheck{Name: "sqlite", Group: "store", Status: "ok", Detail: cfg.DataDir, Remediation: ""})
			}
			st.Close()
		}
		if err := cfg.ValidateServe(false); err != nil {
			checks = append(checks, doctorCheck{Name: "iap-listen", Group: "identity", Status: "warn", Detail: err.Error(), Remediation: "bind to loopback or configure an IAP"})
		} else {
			checks = append(checks, doctorCheck{Name: "iap-listen", Group: "identity", Status: "ok", Detail: cfg.IAP.Type, Remediation: ""})
		}
		if cfg.IAPTSNetRequested() && tsnetServeSupported() {
			checks = append(checks, doctorCheck{Name: "iap-adapter", Group: "identity", Status: "ok", Detail: "tailscale-tsnet", Remediation: ""})
		} else {
			adapter, err := identity.NewAdapter(cfg, "", false)
			if err != nil {
				checks = append(checks, doctorCheck{Name: "iap-adapter", Group: "identity", Status: "fail", Detail: err.Error(), Remediation: "fix iap.type/mode or install a quickd build with the requested provider"})
			} else {
				checks = append(checks, doctorCheck{Name: "iap-adapter", Group: "identity", Status: "ok", Detail: adapter.Name(), Remediation: ""})
			}
		}
		if cfg.PublicBaseDomain == "" && cfg.BaseURL == "" {
			checks = append(checks, doctorCheck{Name: "domain", Group: "edge/iap", Status: "warn", Detail: "no public_base_domain or base_url configured", Remediation: "configure a domain/base_url or deploy with --allow-unpublished"})
		} else {
			checks = append(checks, doctorCheck{Name: "domain", Group: "edge/iap", Status: "ok", Detail: sites.URLFor("example", cfg), Remediation: ""})
		}
		if cfg.IAP.Type == "" || cfg.IAP.Type == "none" {
			checks = append(checks, doctorCheck{Name: "iap", Group: "edge/iap", Status: "warn", Detail: "iap.type is none", Remediation: "configure tailscale or cloudflare IAP, or pass --allow-unpublished for deploy"})
		} else {
			checks = append(checks, doctorCheck{Name: "iap", Group: "edge/iap", Status: "ok", Detail: cfg.IAP.Type, Remediation: ""})
		}
		checks = append(checks, doctorAIChecks(cfg)...)
		checks = append(checks, doctorWarehouseChecks(cfg)...)
	}
	ok := true
	for _, c := range checks {
		if c.Status == "fail" {
			ok = false
		}
	}
	_ = host
	return printResult(af.json, map[string]any{"format_version": "1.0", "ok": ok, "checks": checks})
}

func serveCmd(args []string) error {
	fs := flag.NewFlagSet("quickd serve", flag.ContinueOnError)
	fs.SetOutput(os.Stderr)
	var configPath, devDir, devSite, listen, devIdentity, remoteAPI, remoteAPIToken string
	var dev, allowPublicUnsafe bool
	fs.StringVar(&configPath, "config", os.Getenv("QUICKD_CONFIG"), "config path")
	fs.BoolVar(&dev, "dev", false, "dev mode")
	fs.StringVar(&devDir, "dir", "", "local site directory")
	fs.StringVar(&devSite, "site", "", "site name")
	fs.StringVar(&listen, "listen", "", "listen address")
	fs.StringVar(&devIdentity, "identity", "", "synthetic dev identity email")
	fs.StringVar(&remoteAPI, "remote-api", "", "remote site origin for dev API proxy")
	fs.StringVar(&remoteAPIToken, "remote-api-token", "", "dev API proxy token")
	fs.BoolVar(&allowPublicUnsafe, "allow-public-unsafe", false, "allow public anonymous/dev listener")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if (remoteAPI != "" || remoteAPIToken != "") && !dev {
		return errors.New("--remote-api requires --dev")
	}
	var cfg config.Config
	var err error
	if dev {
		if devDir == "" {
			devDir = "."
		}
		abs, err := filepath.Abs(devDir)
		if err != nil {
			return err
		}
		devDir = abs
		if devSite == "" {
			devSite = filepath.Base(devDir)
		}
		if err := sites.ValidateSiteName(devSite, nil); err != nil {
			return err
		}
		cfg = config.Default(filepath.Join(os.TempDir(), "openquick-dev"))
		cfg.IAP.Type = "none"
		cfg.Viewer.AllowAnonymous = true
		if listen != "" {
			cfg.Listen = listen
		}
	} else {
		if configPath == "" {
			configPath = config.DefaultConfigPath
		}
		cfg, err = config.Load(configPath)
		if err != nil {
			return err
		}
		if listen != "" {
			cfg.Listen = listen
		}
	}
	if remoteAPI != "" || remoteAPIToken != "" {
		if remoteAPI == "" || remoteAPIToken == "" {
			return errors.New("--remote-api and --remote-api-token must be provided together")
		}
		u, err := url.Parse(remoteAPI)
		if err != nil || u.Scheme != "https" || u.Host == "" {
			return errors.New("--remote-api must be an https origin URL")
		}
		remoteAPI = config.NormalizeBaseURL(remoteAPI)
		if !config.IsLoopbackListen(cfg.Listen) {
			return errors.New("--remote-api requires a loopback listen address")
		}
	}
	if err := cfg.ValidateServe(allowPublicUnsafe); err != nil {
		return err
	}
	st, err := store.Open(cfg.DataDir)
	if err != nil {
		return err
	}
	defer st.Close()
	if dev {
		_, _ = st.EnsureSite(context.Background(), devSite, devSite)
	}
	binding, err := openServeBinding(cfg, devIdentity, allowPublicUnsafe)
	if err != nil {
		return err
	}
	if binding.Close != nil {
		defer func() { _ = binding.Close() }()
	}
	apiHandler := api.New(cfg, st)
	staticHandler := static.New(cfg, st, binding.Adapter, apiHandler)
	staticHandler.DevDir = devDir
	staticHandler.DevSite = devSite
	staticHandler.RemoteAPI = remoteAPI
	staticHandler.RemoteAPIToken = remoteAPIToken
	log.Printf("quickd listening on %s", binding.Addr)
	return http.Serve(binding.Listener, staticHandler)
}

func listenError(addr string, err error) error {
	if errors.Is(err, syscall.EADDRINUSE) {
		_, port, splitErr := net.SplitHostPort(addr)
		if splitErr != nil || port == "" {
			port = addr
		}
		if alt := freeLoopbackPort(); alt != "" {
			return fmt.Errorf("listen %s failed: address already in use; port %s is occupied; retry with quick serve --dev --port %s or quickd serve --dev --listen 127.0.0.1:%s", addr, port, alt, alt)
		}
		return fmt.Errorf("listen %s failed: address already in use; port %s is occupied; retry with quick serve --dev --port <free-port>", addr, port)
	}
	return fmt.Errorf("listen %s failed: %w", addr, err)
}

func freeLoopbackPort() string {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return ""
	}
	defer ln.Close()
	if tcp, ok := ln.Addr().(*net.TCPAddr); ok {
		return fmt.Sprintf("%d", tcp.Port)
	}
	return ""
}

func printResult(jsonOut bool, v any) error {
	if jsonOut {
		enc := json.NewEncoder(os.Stdout)
		enc.SetEscapeHTML(false)
		return enc.Encode(v)
	}
	b, _ := json.MarshalIndent(v, "", "  ")
	fmt.Println(string(b))
	return nil
}
