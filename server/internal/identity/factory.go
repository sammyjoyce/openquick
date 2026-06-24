package identity

import (
	"fmt"
	"strings"

	"openquick.dev/quickd/internal/config"
)

func NewAdapter(cfg config.Config, devIdentity string, allowPublicUnsafe bool) (Adapter, error) {
	t := strings.ToLower(strings.TrimSpace(cfg.IAP.Type))
	switch t {
	case "", "none", "anonymous":
		if devIdentity != "" {
			return DevAdapter{Email: devIdentity, AllowPublicUnsafe: allowPublicUnsafe}, nil
		}
		return NoneAdapter{AllowPublicUnsafe: allowPublicUnsafe}, nil
	case "dev":
		return DevAdapter{Email: devIdentity, AllowPublicUnsafe: allowPublicUnsafe}, nil
	case "tailscale", "tailscale-localapi":
		mode := strings.ToLower(strings.TrimSpace(cfg.IAP.Mode))
		if mode == "serve" || t == "tailscale-serve" {
			return TailscaleServeAdapter{}, nil
		}
		if mode == "tsnet" {
			return newTailscaleTSNetAdapter()
		}
		proxies, err := ParseTrustedProxies(cfg.IAP.TrustedProxies)
		if err != nil {
			return nil, fmt.Errorf("%w: %v", ErrMisconfiguredAdapter, err)
		}
		return TailscaleLocalAPIAdapter{TrustedProxies: proxies, SourceIPHeader: cfg.IAP.SourceIPHeader, Client: &LocalAPIWhoIsClient{}}, nil
	case "tailscale-serve":
		return TailscaleServeAdapter{}, nil
	case "tailscale-tsnet":
		return newTailscaleTSNetAdapter()
	case "cloudflare", "cloudflare-access":
		return &CloudflareAccessAdapter{TeamDomain: cfg.IAP.TeamDomain, Audience: cfg.IAP.Audience, JWKSURL: cfg.IAP.JWKSURL, EmailDomainAllowlist: cfg.IAP.EmailDomainAllowlist}, nil
	default:
		return nil, fmt.Errorf("%w: unknown iap.type %q", ErrMisconfiguredAdapter, cfg.IAP.Type)
	}
}
