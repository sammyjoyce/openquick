# OpenQuick

OpenQuick turns a folder of HTML and assets into a private site URL on a host you control.
The `quick` CLI builds only when `quick.json` says to, sends the release with `rsync over SSH`, and asks `quickd` to activate it atomically.
Put an identity-aware edge in front of the host, then use the same-origin SDK for identity, document DB, realtime, uploads, and config-gated AI and warehouse APIs.

OpenQuick is inspired by [Shopify Quick](https://shopify.engineering/quick); it keeps the folder-to-private-site workflow and uses SSH, rsync, and a self-hosted `quickd` host.

> [!NOTE]
> OpenQuick was built almost entirely by coding agents (design docs,
> implementation, tests, and documentation), with human direction and review.
> Read the code and run the test suites before trusting it in production; treat it
> as an agent-built experiment, not a hardened release.

## Quickstart

### Bootstrap a host, then deploy a site

Run the host install command once per host.
It writes or updates the local `lab` profile and prints the host install plan.
Add `--execute` when you are ready to copy a local `quickd` over SSH and install the systemd service on the host.

```bash
quick serve install --profile lab --host quick@box --remote-root /srv/quick --domain quick.example.com --iap tailscale
quick init lunch-vote
cd lunch-vote
quick deploy
quick open
```

Use Tailscale or Cloudflare Access for a private host. Do not use `--iap none` on a non-loopback listener unless you also pass the explicit unsafe override and intend public anonymous access.

### Try it locally in two minutes

Run a local `quickd` in the site directory:

```bash
quick init local-demo
cd local-demo
quick serve --dev --port 9366 --identity dev@example.test
```

Open this URL in a browser:

```text
http://localhost:9366/~/local-demo/
```

Local dev mode binds to loopback, uses no IAP, and returns the synthetic identity only when `--identity` is set.

## What you get

### CLI

`quick` is the site lifecycle tool. It scaffolds sites, resolves profiles, runs optional builds, transfers folders or ZIP archives, calls host-side `quickd` commands, and emits human or JSON output.

Global flags: `--help` (`-h`), `--version`, `--debug` (`-d`), `--quiet` (`-q`), `--verbose` (`-v`), `--json`, `--plain`, `--no-color`, and `--config` (`-c`) `<path>`.

| Command | Purpose | Command flags |
| --- | --- | --- |
| `quick init [path]` | Scaffold a static site with `index.html`, `quick.json`, `AGENTS.md`, API notes, and `.quickignore`. | `--template <blank\|realtime>`, `--name <name>`, `--profile <profile>` |
| `quick deploy [path]` | Build if configured, transfer a folder or ZIP, and publish a new release through `quickd`. | `--site <site>`, `--subdomain <subdomain>`, `--profile <profile>`, `--dry-run`, `--no-build`, `--no-delete`, `--open`, `--bootstrap`, `--allow-unpublished`, `--checksum`, `--yes` |
| `quick serve --dev` or `quick serve install` | Run local dev `quickd`, proxy remote APIs for dev, or print/install host setup steps. | `--dev`, `--port <port>`, `--identity <email>`, `--remote-api <profile>`, `--profile <profile>`, `--host <host>`, `--remote-root <path>`, `--domain <domain>`, `--iap <tailscale\|cloudflare\|none>`, `--execute`, `--allow-public-unsafe` |
| `quick open [site]` | Open or print the resolved site URL. | `--profile <profile>`, `--copy`, `--plain` |
| `quick list` | List local deployment records and remote host rows. | `--profile <profile>`, `--remote`, `--json` |
| `quick info` | Display application metadata. | none |
| `quick doctor [site]` | Run local, remote, edge, and optional deep diagnostics. | `--profile <profile>`, `--remote`, `--site <site>`, `--deep`, `--json` |
| `quick delete SITE` | Delete a remote site after typed confirmation or `--yes`. | `--profile <profile>`, `--yes`, `--json` |
| `quick public SITE [on\|off]` | Show or change a site's public-static flag when the host enables public static sites. | `--profile <profile>`, `--yes`, `--json` |
| `quick domain add\|remove\|list [domain]` | Manage custom domains in the host catalog. | `--site <site>`, `--profile <profile>`, `--json` |
| `quick menu` | Launch the same interactive TUI as bare `quick`. | none |
| `quick opencli` | Print the OpenCLI command contract as JSON. | none |

Target resolution is deterministic: CLI flags, `QUICK_*` environment variables, `quick.json`, user profile config, then local defaults.
Useful environment variables include `QUICK_CONFIG_PATH`, `QUICK_PROFILE`, `QUICK_SITE`, `QUICK_REMOTE`, `QUICK_BASE_DOMAIN`, and `QUICK_QUICKD`.

### Interactive TUI

Run `quick` with no arguments on an interactive terminal. It opens the keyboard-driven dashboard unless JSON output is active. The hidden `quick menu` command opens the same TUI.

Use the TUI to browse sites, deploy a site, create a new site, run doctor checks, start a local dev server, generate a host install guide, and edit profile or site settings.
Default builds include the TUI.
Build with `zig build -Denable-tui=false` for a headless CLI.

### `quickd` host daemon

`quickd` runs on one host. It serves static files, routes wildcard subdomains, exposes `/_quick/*`, stores catalog data in SQLite, and performs host-side deploy activation.

A deploy is staged before it is served:

1. `quickd deploy prepare` creates `.incoming/<deploy-id>/files` and returns a staging path.
2. `quick deploy` mirrors files with `rsync` or uploads a ZIP for host extraction.
3. `quickd deploy activate` validates the staging tree, writes `.quick-release.json`, moves it into `releases/<release-id>`, swaps `current` atomically, records the deploy, and prunes old releases.

A browser sees either the previous complete release or the next complete release. It never sees an in-progress transfer.

Sites normally resolve as `https://<site>.<base-domain>`.
Pure Tailscale Serve and local dev can use the path fallback `https://host/~/site/` or `http://localhost:9366/~/site/`.
The authenticated site directory is served from the apex host and `/~/`; disable it with `"directory": { "enabled": false }`.

### Browser SDK

Hosted sites import the SDK from the same origin:

```html
<input id="file" type="file">
<script type="module">
  import { quick } from '/_quick/sdk.js';

  const me = await quick.identity.current();
  const votes = quick.db.collection('votes');
  await votes.create({ choice: 'ramen', by: me.email || me.login || me.subject });

  const room = quick.realtime.channel('lunch-room');
  const stop = room.on('cursor', (msg) => console.log('cursor', msg));
  room.send('cursor', { x: 120, y: 90 });

  const fileInput = document.querySelector('#file');
  fileInput.addEventListener('change', async () => {
    const file = fileInput.files[0];
    if (file) console.log(await quick.uploads.put(file, { name: file.name }));
  });

  const caps = await quick.capabilities();
  if (caps.ai) {
    const answer = await quick.ai.chat([{ role: 'user', content: 'Summarize this page.' }]);
    console.log(answer.message.content);
  }
  if (caps.warehouse) {
    console.log(await quick.warehouse.query('recent_orders', { limit: 10 }));
  }

  window.addEventListener('pagehide', stop);
</script>
```

The SDK calls only relative `/_quick/*` URLs with same-origin credentials.
The stable surface is `quick.identity.current()`, `quick.identity.onChange()`, `quick.db.collection()`, `quick.realtime.channel()`, `quick.uploads.put/get/remove()`,
`quick.ai.chat/image()`, `quick.warehouse.query()`, and `quick.capabilities()`.

Identity, DB, realtime, and uploads are core host APIs. AI and warehouse stay unavailable until the host advertises `capabilities().ai === true` or `capabilities().warehouse === true`.

### IAP options

OpenQuick gets privacy from the configured IAP and the network boundary around the host.
The host `viewer` gate controls anonymous access with `viewer.require_identity` and `viewer.allow_anonymous`.
Set one of those for private production hosts instead of relying on local `iap=none` defaults.

| IAP mode | Use it for | Notes |
| --- | --- | --- |
| Tailscale Serve | The simplest private tailnet URL. | Use path fallback for pure `*.ts.net` names, or add custom DNS for wildcard site subdomains. |
| Tailscale localapi | Private wildcard subdomains on a custom domain. | Put Caddy on the Tailscale address, trust `X-Forwarded-For` only from configured local proxies, and let `quickd` call Tailscale WhoIs. |
| Tailscale tsnet | A Go-native `quickd` listener inside the tailnet. | Manage tsnet state and auth keys. Pure `*.ts.net` wildcard caveats still apply. |
| Cloudflare Access | Public DNS protected by organization login and Cloudflare Tunnel. | `quickd` must validate `Cf-Access-Jwt-Assertion` with issuer, audience, expiry, and JWKS checks. Do not trust email headers alone. |
| dev or none | Loopback local development. | `iap=none` or `dev` on a non-loopback listener is rejected unless `--allow-public-unsafe` is passed. |

### Config-gated extras

These surfaces are off by default or empty by default. Enable them deliberately in host config or the host catalog.

- **Browser deploy portal:** set `http_deploy.enabled` and authorize `http_deploy.allow_identities` or bearer tokens.
  It accepts same-origin ZIP deploys at the apex host and reuses prepare, extract, scan, and activate.
- **Public static sites:** set `public_static.enabled`, then run `quick public SITE on`. Static GET/HEAD assets can be public only after the static-only scan passes. `/_quick/*` remains authenticated.
- **ZIP deploy surfaces:** browser ZIP deploys are off with the portal. CLI ZIP deploys use `quick deploy site.zip --site demo` and still pass through host extraction limits before activation.
- **Signed release manifests and SSH certificate enforcement:** set `deploy.signing.enabled`, `deploy.signing.required`, or `deploy.require_ssh_cert`. Defaults are false.
- **Custom domains and on-demand TLS:** add exact hostnames with `quick domain add DOMAIN --site SITE` and wire Caddy to `/_quick/domains/ask`.
  The ask endpoint allows only cataloged domains from loopback or trusted proxies.
- **Dev remote-API proxy:** set `dev_proxy.enabled`, then use `quick serve --dev --remote-api <profile>` to serve local static files while forwarding `/_quick/*` to a deployed site.
- **AI and warehouse:** set `ai.enabled` with providers and model allowlists, or `warehouse.enabled` with named read-only queries. Browser code never receives provider keys or database URLs.

## Philosophy

OpenQuick has no site owners in the default model. Every authenticated viewer inside the IAP can view every site. Every deployer with SSH access to the host can overwrite every site.

The safety valve is friction, not ownership.
If a site was last deployed by someone else, `quick deploy` shows the last deployer and requires the exact site name in a TTY.
Non-interactive runs must pass `--yes`.

Keep the capability set small and fixed: static releases, identity, document DB, realtime, uploads, and host-gated AI and warehouse.
Do not add custom backends, cron jobs, arbitrary server code, or per-site ACLs to a normal site.

Run one host first. Use audit records, release manifests, SSH identity, and rollback-friendly release directories instead of per-site filesystem ownership.

## Container

Build the container image.
The Docker build creates the SDK artifact, runs Go vet/tests and Zig tests unless `SKIP_TESTS=1`, and builds a release CLI without the TUI for deterministic Linux container builds.

```bash
docker build -t openquick:test .
```

Run `quickd` from the image on port 9366:

```bash
docker rm -f openquick-test >/dev/null 2>&1 || true
docker run --rm -d --name openquick-test -p 127.0.0.1:9366:9366 openquick:test
for i in $(seq 1 30); do
  curl -fsS http://127.0.0.1:9366/_quick/health && break
  sleep 1
done
docker rm -f openquick-test
```

Run the committed smoke test:

```bash
scripts/container-smoke.sh
```

The runtime image uses `iap.type=dev`, a synthetic identity, and `--allow-public-unsafe` because the smoke test publishes the container port locally.
Do not treat that container config as a production IAP setup.

## Requirements, build, and test

Install these tools for full local development:

- Zig 0.16.0 and a system C toolchain/libc for the C CLI.
- Go 1.25 for `quickd`.
- Bun for the browser SDK build and tests.
- `ssh`, `scp`, and `rsync` for real host installs and deploys.
- Curses development files for TUI builds. Pass `-Denable-tui=false` for headless builds.
- Optional `libghostty-vt` for Ghostty-backed terminal tests, or use the Nix dev shell.

Build everything:

```bash
just build-all
```

Build individual layers:

```bash
zig build
(cd server && go build -o ../zig-out/bin/quickd ./cmd/quickd)
(cd sdk/js && bun run build)
```

Run the main test layers:

```bash
just test-all
zig build check
zig build terminal-test
(cd server && go test ./...)
(cd sdk/js && bun test)
```

Use `zig build -Denable-tui=true terminal-test` when TUI behavior changes.

## Docs

| Topic | Read |
| --- | --- |
| Initialize a site | [docs/user/quick-init.md](docs/user/quick-init.md) |
| Deploy, delete, public sites, domains, ZIPs, directory listing, shared libraries | [docs/user/quick-deploy.md](docs/user/quick-deploy.md) |
| Interactive TUI | [docs/user/tui.md](docs/user/tui.md) |
| Browser SDK | [docs/user/sdk.md](docs/user/sdk.md) |
| AI runtime | [docs/user/ai.md](docs/user/ai.md) |
| Warehouse named queries | [docs/user/warehouse.md](docs/user/warehouse.md) |
| Tailscale IAP modes | [docs/user/iap-tailscale.md](docs/user/iap-tailscale.md) |
| Cloudflare Access IAP | [docs/user/iap-cloudflare.md](docs/user/iap-cloudflare.md) |
| Product workflow and trust model | [docs/design/WORKFLOW.md](docs/design/WORKFLOW.md) |
| Host architecture and roadmap | [docs/design/ARCHITECTURE.md](docs/design/ARCHITECTURE.md) |
| Shopify Quick parity audit | [docs/design/QUICK_PARITY.md](docs/design/QUICK_PARITY.md) |
| Accepted and rejected backlog decisions | [docs/design/DEFERRED_ASSESSMENT.md](docs/design/DEFERRED_ASSESSMENT.md) |
| SDK dependency assessment | [docs/design/UNJS_ASSESSMENT.md](docs/design/UNJS_ASSESSMENT.md) |
| Agent deploy skill | [skills/openquick-deploy/SKILL.md](skills/openquick-deploy/SKILL.md) and [skills/build.sh](skills/build.sh) |

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md). Keep changes small, preserve the OpenCLI contract, and run the relevant build and test layer before opening a PR.

## License

MIT. See [LICENSE](LICENSE).
