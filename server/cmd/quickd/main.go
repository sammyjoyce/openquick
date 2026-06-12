package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"

	"openquick.dev/quickd/internal/api"
	"openquick.dev/quickd/internal/config"
	"openquick.dev/quickd/internal/deploy"
	"openquick.dev/quickd/internal/identity"
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
		return errors.New("usage: quickd <serve|deploy|list|sites|doctor>")
	}
	switch args[0] {
	case "serve":
		return serveCmd(args[1:])
	case "deploy":
		return deployCmd(args[1:])
	case "list":
		return listCmd(args[1:])
	case "sites":
		return sitesCmd(args[1:])
	case "doctor":
		return doctorCmd(args[1:])
	case "help", "-h", "--help":
		fmt.Println("usage: quickd <serve|deploy|list|sites|doctor>")
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

func deployCmd(args []string) error {
	if len(args) == 0 {
		return errors.New("usage: quickd deploy <prepare|activate>")
	}
	switch args[0] {
	case "prepare":
		fs := flag.NewFlagSet("quickd deploy prepare", flag.ContinueOnError)
		fs.SetOutput(os.Stderr)
		var af adminFlags
		var site string
		addAdminFlags(fs, &af)
		fs.StringVar(&site, "site", "", "site slug")
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
		res, err := deploy.New(cfg, st).Prepare(context.Background(), site)
		if err != nil {
			return err
		}
		return printResult(af.json, res)
	case "activate":
		fs := flag.NewFlagSet("quickd deploy activate", flag.ContinueOnError)
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
		res, err := deploy.New(cfg, st).Activate(context.Background(), site, deployID)
		if err != nil {
			return err
		}
		return printResult(af.json, res)
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
		recs[i].URL = sites.URLFor(recs[i].Name, cfg)
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
		rec.URL = sites.URLFor(rec.Name, cfg)
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
	default:
		return fmt.Errorf("unknown sites command %q", args[0])
	}
}

type doctorCheck struct {
	Name        string `json:"name"`
	Group       string `json:"group"`
	Status      string `json:"status"`
	Detail      string `json:"detail"`
	Remediation string `json:"remediation"`
}

func checkWritableDir(dir string) error {
	info, err := os.Stat(dir)
	if err != nil {
		return err
	}
	if !info.IsDir() {
		return fmt.Errorf("%s is not a directory", dir)
	}
	probe := filepath.Join(dir, ".quickd-doctor-write-test")
	if err := os.WriteFile(probe, []byte("ok"), 0o600); err != nil {
		return err
	}
	return os.Remove(probe)
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
			checks = append(checks, doctorCheck{Name: "remote_root_writable", Group: "host", Status: "fail", Detail: err.Error(), Remediation: "create /srv/quick and make it writable by quick"})
		} else {
			checks = append(checks, doctorCheck{Name: "remote_root_writable", Group: "host", Status: "ok", Detail: cfg.RemoteRoot, Remediation: ""})
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
	var configPath, devDir, devSite, listen, devIdentity string
	var dev, allowPublicUnsafe bool
	fs.StringVar(&configPath, "config", os.Getenv("QUICKD_CONFIG"), "config path")
	fs.BoolVar(&dev, "dev", false, "dev mode")
	fs.StringVar(&devDir, "dir", "", "local site directory")
	fs.StringVar(&devSite, "site", "", "site name")
	fs.StringVar(&listen, "listen", "", "listen address")
	fs.StringVar(&devIdentity, "identity", "", "synthetic dev identity email")
	fs.BoolVar(&allowPublicUnsafe, "allow-public-unsafe", false, "allow public anonymous/dev listener")
	if err := fs.Parse(args); err != nil {
		return err
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
	adapter, err := identity.NewAdapter(cfg, devIdentity, allowPublicUnsafe)
	if err != nil {
		return err
	}
	apiHandler := api.New(cfg, st)
	staticHandler := static.New(cfg, st, adapter, apiHandler)
	staticHandler.DevDir = devDir
	staticHandler.DevSite = devSite
	log.Printf("quickd listening on %s", cfg.Listen)
	return http.ListenAndServe(cfg.Listen, staticHandler)
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
