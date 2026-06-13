# OpenQuick host setup

Use this reference when no OpenQuick host profile exists, `quickd` is missing, or a user asks to operate the host.
A host runs `quickd`, stores releases under a remote root such as `/srv/quick`, and sits behind an identity-aware edge unless it is local dev.

## Install modes

Run `quick serve install` without `--execute` first when you need a safe plan:

```bash
quick serve install --profile lab --host quick@box --remote-root /srv/quick --domain quick.example.com --iap tailscale
```

Add `--execute` only when you are allowed to change the remote Linux host over SSH:

```bash
quick serve install --profile lab --host quick@box --remote-root /srv/quick --domain quick.example.com --iap tailscale --execute
```

`--execute` requires an SSH host, local `quickd` on `PATH` or `QUICK_QUICKD`, `scp`, `sudo` on the host, and systemd.
It creates the `quick` user and `quick-deploy` group, creates the remote root tree, copies `quickd` to `/usr/local/bin/quickd`, and writes `/etc/openquick/quickd.json`.
It installs `openquick.service`, starts it, and runs `quickd doctor --host --json`.

The install command writes or updates the local profile in `~/.config/openquick/config.json`.

## Tailscale private host

Choose Tailscale for a homelab or small trusted team. Use `--iap tailscale` for the OpenQuick profile:

```bash
quick serve install --profile lab --host quick@box --remote-root /srv/quick --domain quick.example.com --iap tailscale
```

For true wildcard site URLs on a tailnet, use a custom wildcard domain that resolves to the host's Tailscale address for viewers.
Put Caddy or another local proxy on the tailnet side and let `quickd` call Tailscale LocalAPI through trusted proxy source data.

Use this host IAP shape for the localapi path:

```json
{
  "iap": {
    "type": "tailscale",
    "mode": "localapi",
    "trusted_proxies": ["127.0.0.1/32"],
    "source_ip_header": "X-Forwarded-For"
  }
}
```

For the simplest private URL, route Tailscale Serve to the local quickd listener:

```bash
tailscale serve --bg https / http://127.0.0.1:9366
```

Pure `*.ts.net` names are machine-name oriented. Use a path fallback or add custom DNS/TLS when users need `https://site.quick.example.com` style URLs.
Do not treat Tailscale Funnel as an identity boundary; Funnel traffic is public unless another IAP protects it.

## Cloudflare Access host

Choose Cloudflare Access when users need public DNS names protected by organization login and no inbound ports on the origin.

```bash
quick serve install --profile cf --host quick@cf-box --remote-root /srv/quick --domain quick.example.com --iap cloudflare
```

Route both apex and wildcard hostnames through the tunnel:

```yaml
ingress:
  - hostname: "*.quick.example.com"
    service: http://127.0.0.1:9366
  - hostname: quick.example.com
    service: http://127.0.0.1:9366
  - service: http_status:404
```

Create one Cloudflare Access application that covers both `quick.example.com` and `*.quick.example.com`. Configure the host with the Access JWT values:

```json
{
  "iap": {
    "type": "cloudflare",
    "team_domain": "https://example.cloudflareaccess.com",
    "audience": "access-application-aud-tag",
    "jwks_url": "https://example.cloudflareaccess.com/cdn-cgi/access/certs",
    "email_domain_allowlist": ["example.com"]
  }
}
```

`quickd` must validate `Cf-Access-Jwt-Assertion`. Do not rely on `Cf-Access-Authenticated-User-Email` alone. Keep the origin reachable only through the tunnel.

## Bare or local host

Use `iap=none` only for loopback local dev, intentional public/static experiments, or an environment where another reviewed layer provides the identity boundary.
The installer rejects `iap=none` on non-loopback domains unless you pass the explicit unsafe flag.

```bash
quick serve install --profile bare --host quick@vps --remote-root /srv/quick --domain quick.example.com --iap none --allow-public-unsafe
```

Prefer a real IAP for private OpenQuick. A bare public VPS without IAP violates the default private-host stance.

Run a local dev host without installing systemd:

```bash
quick serve --dev --port 9366 --identity sam@example.com
```

Forward local static files to a deployed site's remote APIs only when the user accepts that local actions can touch remote data:

```bash
quick serve --dev --remote-api lab
```

### Loopback host with real deploys (no edge)

When you want the full prepare/rsync/activate deploy flow on one machine without DNS, TLS, or an edge proxy, run `quickd serve` against a local root and point a profile at `localhost` over SSH:

```json
{
  "$schema": "https://openquick.dev/schemas/host.v1.json",
  "listen": "127.0.0.1:9366",
  "public_base_domain": "localhost",
  "remote_root": "/srv/quick",
  "iap": { "type": "dev" },
  "viewer": { "require_identity": true, "allow_anonymous": false }
}
```

```bash
quickd serve --config /srv/quick/config/quickd.json --identity sam@example.com
```

Profile shape for the same machine (requires `ssh localhost` to work non-interactively):

```json
{
  "default_profile": "local",
  "profiles": {
    "local": {
      "ssh": "sam@localhost",
      "remote_root": "/srv/quick",
      "base_url": "http://localhost:9366/~",
      "iap": { "type": "none" }
    }
  }
}
```

Facts that matter in this posture:

- `iap.type: "dev"` with `--identity` gives an authenticated synthetic identity on loopback, so `/_quick/db`, uploads, and realtime work.
  `iap: none` without an identity serves pages but rejects data APIs with `authentication required`.
- Deploys stop with an IAP/domain publication message because no edge exists; pass `--allow-unpublished`.
- `quick doctor` edge probes (`http_health`, `http_identity`) probe `https://<site>.<base_domain>` and fail without DNS/TLS;
  the working URL is the `base_url` path form `http://localhost:9366/~/<site>`.
  Set `base_url` (not `base_domain`) in the profile so resolved URLs use the path form.
- `quickd` resolves over the SSH session's non-interactive PATH; install it somewhere like `/usr/local/bin` or `~/.local/bin` that the default shell exposes.

## Container option

Build and run the repository container when you need a local smoke host:

```bash
docker build -t openquick:test .
docker run --rm -p 127.0.0.1:9366:9366 openquick:test
curl http://127.0.0.1:9366/_quick/health
```

Run the repeatable smoke script:

```bash
scripts/container-smoke.sh
```

The container uses a dev identity and `--allow-public-unsafe` for local test exposure only. Do not use that posture as a production IAP design.

## Host config gates

Most optional features are off until the operator enables host config keys. `directory.enabled` is the exception: the built-in authenticated directory is enabled by default and can be disabled.

| Gate | Default | Enable or configure with |
| --- | --- | --- |
| IAP | `iap.type: "none"` in the default config; `quick serve install` sets the chosen IAP. | `iap.type`, `iap.mode`, `iap.trusted_proxies`, `iap.source_ip_header`, `iap.team_domain`, `iap.audience`, `iap.jwks_url`, `iap.email_domain_allowlist`. |
| Viewer identity | Default config allows anonymous with `iap=none`; installs with `tailscale` or `cloudflare` set `viewer.require_identity: true` and `viewer.allow_anonymous: false`. | `viewer.require_identity`, `viewer.allow_anonymous`. |
| Browser site directory | On by default. | Set `directory.enabled: false` to disable; set `true` to keep it. |
| Public static sites | Off. | `public_static.enabled: true`, then use `quick public SITE on`. |
| Browser ZIP deploy portal | Off. | `http_deploy.enabled: true` plus `http_deploy.tokens` or `http_deploy.allow_identities`. |
| AI | Off. | `ai.enabled: true`, `ai.providers`, `ai.default_provider`, and `ai.limits`. |
| Warehouse | Off. | `warehouse.enabled: true` and `warehouse.queries`. |
| Local dev remote API proxy | Off. | `dev_proxy.enabled: true`. |
| Release signing | Off. | `deploy.signing.enabled: true`; use `deploy.signing.required: true` only after verification is ready. |
| SSH certificate enforcement | Off. | `deploy.require_ssh_cert: true`. |

Minimal valid fragments for optional gates:

```json
{
  "public_static": { "enabled": true },
  "http_deploy": {
    "enabled": true,
    "tokens": [],
    "allow_identities": ["alice@example.com"]
  },
  "ai": {
    "enabled": true,
    "default_provider": "openai",
    "providers": [
      {
        "name": "openai",
        "type": "openai",
        "api_key_env": "OPENAI_API_KEY",
        "models": ["gpt-4o-mini"],
        "default_model": "gpt-4o-mini"
      }
    ],
    "limits": {
      "requests_per_minute_per_identity": 20,
      "requests_per_day_per_site": 2000,
      "max_request_bytes": 1048576
    }
  },
  "warehouse": {
    "enabled": true,
    "queries": [
      {
        "name": "recent_orders",
        "sql": "SELECT id, email, total_cents FROM orders WHERE status = :status LIMIT 20",
        "params": [{ "name": "status", "type": "string" }],
        "max_rows": 100
      }
    ]
  },
  "dev_proxy": { "enabled": true },
  "deploy": {
    "signing": { "enabled": true, "required": false },
    "require_ssh_cert": false
  }
}
```

## Verification loop

Run diagnostics before deploying user work:

```bash
quick doctor --profile lab
quick doctor --profile lab --remote
quick doctor --profile lab --remote --json
```

After DNS and IAP are published, run a site-specific check:

```bash
quick doctor --profile lab --remote --site lunch-vote
```

Run the deep check only when a temporary diagnostic deploy and cleanup are acceptable:

```bash
quick doctor --profile lab --deep --json
```

Then deploy a small site and open it:

```bash
quick init lunch-vote --profile lab
cd lunch-vote
quick deploy --dry-run
quick deploy --open
```
