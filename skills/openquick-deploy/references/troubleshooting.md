# Troubleshooting

Start with `quick doctor`. Use the smallest scope that can reproduce the failure, then widen to remote and deep checks.

```bash
quick doctor
quick doctor --profile lab --remote
quick doctor --profile lab --remote --site lunch-vote --json
quick doctor --profile lab --deep --json
```

## Doctor check groups

| Group | Checks | What failure means |
| --- | --- | --- |
| `local` | `quick_version`, `rsync_present`, `ssh_present`, `quick_json`, `site_dns_label`, `build_output`, `quickignore` | The local machine cannot resolve, build, or transfer the site safely. |
| `remote` | `ssh_profile`, `quickd_doctor`, `host_stats` | The selected profile cannot reach a healthy `quickd` host over SSH. |
| `edge/iap` | `http_probe`, `http_health`, `http_identity`, `deep_temp_deploy` | DNS, TLS, IAP, routing, or identity does not work from the resolved URL. |

## `ssh` or `rsync` is missing

Symptoms:

- `rsync_present` fails.
- `ssh_present` fails.
- Deploy transfer fails before activation.

Verify tools:

```bash
command -v ssh
command -v rsync
```

Install them on Debian or Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y openssh-client rsync
```

Install or upgrade rsync on macOS with Homebrew when the system tool is missing or too old:

```bash
brew install rsync
```

Retry:

```bash
quick doctor --profile lab
quick deploy --profile lab --dry-run
```

## `quickd` is missing or unhealthy on the host

Symptoms:

- First deploy bootstrap says `quickd` is missing.
- `quickd_doctor` fails.
- SSH works, but prepare or list cannot run remote `quickd` commands.

Install or repair the host:

```bash
quick serve install --profile lab --host quick@box --remote-root /srv/quick --domain quick.example.com --iap tailscale --execute
```

For Cloudflare Access, use the Cloudflare profile instead:

```bash
quick serve install --profile cf --host quick@cf-box --remote-root /srv/quick --domain quick.example.com --iap cloudflare --execute
```

Verify:

```bash
quick doctor --profile lab --remote --json
quick list --profile lab --remote
```

If `--execute` fails before copying `quickd`, set `QUICK_QUICKD` to the local `quickd` path or put `quickd` on `PATH`, then retry.

## IAP or domain is not published yet

Symptoms:

- Deploy stops before transfer with an IAP/domain publication message.
- `http_health` or `http_identity` fails.
- DNS, TLS, or the IAP app is not ready.

Check the profile and site:

```bash
quick doctor --profile lab --remote --site lunch-vote
```

If the site must be deployed before DNS or IAP is reachable, make the unsafe/unpublished decision explicit:

```bash
quick deploy --profile lab --site lunch-vote --allow-unpublished
```

Otherwise fix the edge first, then deploy without `--allow-unpublished`:

```bash
quick doctor --profile lab --remote --site lunch-vote --json
quick deploy --profile lab --site lunch-vote
```

Use `--allow-unpublished` only when the user accepts that the URL may not be reachable or protected yet.

## Identity probe fails on Tailscale

Symptoms:

- `http_identity` fails or returns identity JSON without `authenticated`, `provider`, or `subject`.
- `quick.identity.current()` fails in the browser.

For Tailscale Serve, verify the local route:

```bash
tailscale serve --bg https / http://127.0.0.1:9366
quick doctor --profile lab --remote --site lunch-vote
```

For Tailscale LocalAPI with custom wildcard domains, verify all of these facts:

- wildcard DNS for `*.quick.example.com` resolves to the host's Tailscale address for viewers;
- the proxy forwards to `127.0.0.1:9366`;
- `quickd` trusts only the local proxy source;
- the host config uses `iap.type: "tailscale"`, `iap.mode: "localapi"`, `trusted_proxies`, and `source_ip_header`.

Use this IAP fragment:

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

Retry:

```bash
quick doctor --profile lab --remote --site lunch-vote --json
```

Do not use Tailscale Funnel as the identity boundary. Treat Funnel as public unless another IAP protects it.

## Identity probe fails on Cloudflare Access

Symptoms:

- `http_identity` fails behind Cloudflare.
- Requests reach quickd without a valid Access JWT.
- The Access app protects the apex but not wildcard site hosts.

Verify Cloudflare routing:

```yaml
ingress:
  - hostname: "*.quick.example.com"
    service: http://127.0.0.1:9366
  - hostname: quick.example.com
    service: http://127.0.0.1:9366
  - service: http_status:404
```

Verify host IAP config:

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

Then run:

```bash
quick doctor --profile cf --remote --site lunch-vote --json
```

Fix the Access application if it does not cover both `quick.example.com` and `*.quick.example.com`. Block direct origin access so scanners cannot bypass Access.

## Deploy lock or concurrent deploy conflict

Symptoms:

- Prepare or activate waits unexpectedly.
- Remote output mentions `deploy.lock`, an existing release, or a concurrent deploy.
- A previous deploy was interrupted during transfer or activation.

Check current host state first:

```bash
quick list --profile lab --remote
quick doctor --profile lab --remote --site lunch-vote
```

If another deploy is active, wait for it or coordinate with that deployer. Do not delete `deploy.lock` while a deploy can still be running.

If the host operator confirms no deploy process is active, inspect the site directory on the host before removing stale incoming data:

```bash
ssh quick@box quickd doctor --host --json
```

Then retry a dry run and deploy:

```bash
quick deploy --profile lab --site lunch-vote --dry-run
quick deploy --profile lab --site lunch-vote
```

If the last deployer differs, the CLI requires typed site-name confirmation or an approved `--yes`:

```bash
quick deploy --profile lab --site lunch-vote --yes
```

## Public static scan rejects a site

Symptoms:

- `quick public SITE on` fails.
- Deploy to an already-public site fails before activation.
- The failure lists a file and one of the static-only patterns.

The scan rejects text files containing:

```text
quick.db
quick.uploads
quick.ai
quick.identity
quick.realtime
/_quick/
```

Find matches locally:

```bash
grep -RIn "quick\.db\|quick\.uploads\|quick\.ai\|quick\.identity\|quick\.realtime\|/_quick/" .
```

Choose one remediation:

1. Keep the site private:

   ```bash
   quick public lunch-vote off --profile lab
   quick deploy --profile lab --site lunch-vote
   ```

2. Remove SDK/API usage from the public site, rebuild if needed, and redeploy:

   ```bash
   quick deploy --profile lab --site lunch-vote --dry-run
   quick deploy --profile lab --site lunch-vote
   quick public lunch-vote on --profile lab
   ```

3. Split public static assets into a separate site that does not import `/_quick/sdk.js`.

## ZIP deploy is rejected

Symptoms:

- `quick deploy site.zip --site demo` fails during extraction.
- Browser ZIP deploy portal reports an unsafe archive.

Host extraction rejects absolute paths, `..` traversal, duplicate entries, symlinks, encrypted entries, more than 1000 entries, more than 50 MiB compressed, or more than 100 MiB uncompressed.

Rebuild the archive from the directory contents, not from a parent path with unsafe entries:

```bash
cd dist
zip -r ../site.zip .
cd ..
quick deploy site.zip --site demo --profile lab
```

## Build output is missing

Symptoms:

- `build_output` warns.
- Deploy says the output directory does not exist.

Read `quick.json`, run the configured build, then dry run:

```bash
cat quick.json
quick deploy --dry-run
```

If the build command is wrong, update `quick.json.build` and `quick.json.output`. Do not make OpenQuick infer frameworks or package managers.
