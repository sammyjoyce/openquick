# `quick deploy`

`quick deploy` is an rsync-over-SSH deploy wrapper with host-side activation by `quickd`. It never rsyncs into the live directory.

## Common commands

```bash
quick deploy                         # deploy current site using quick.json
quick deploy ./dist --site demo       # deploy a folder directly
quick deploy --profile lab            # override profile
quick deploy --site lunch-vote        # override site/subdomain
quick deploy --subdomain lunch        # publish under lunch.<base-domain>
quick deploy site.zip --site demo     # deploy a ZIP archive
quick deploy --dry-run                # show plan only
quick deploy --no-build               # skip quick.json build command
quick deploy --no-delete              # do not mirror deletions into the release
quick deploy --open                   # open the URL after activation
quick deploy --yes                    # allow non-interactive overwrite confirmation
quick delete lunch-vote               # remove a site after confirmation
quick public lunch-vote on            # make a static-only site public
quick domain add app.example.com --site lunch-vote
```

## Target resolution

Resolution order:

1. CLI flags.
2. Environment: `QUICK_PROFILE`, `QUICK_SITE`, `QUICK_REMOTE`, `QUICK_BASE_DOMAIN`.
3. Per-site `quick.json`.
4. User-global config.
5. Built-in local dev defaults.

Resolved values include `site`, `subdomain`, `profile`, SSH target, `remote_root`, `base_domain` or `base_url`, and IAP type. The host stores `subdomain` separately from the site name; it defaults to the site name, must be a DNS label, and must be unique on the host.

## Overwrite confirmation

`quickd deploy prepare --json` returns the last deploy metadata when a site already exists:

```json
{
  "last_deployer": "alice",
  "last_release": "20260611T135901Z-a1b2c3",
  "last_deployed_at": "2026-06-11T13:59:01Z"
}
```

If `last_deployer` differs from the current deployer identity (local username or the profile SSH user), the CLI requires extra friction:

- in a TTY, type the exact site name to continue;
- outside a TTY, pass `--yes` or the deploy fails with a clear message;
- the TUI uses the same typed-confirm dialog.

This confirmation protects shared hosts from accidental cross-user overwrites. It does not replace host authorization.

## Build step

If `quick.json.build` is non-null and `--no-build` is not set, `quick deploy`:

1. runs the command in `quick.json.source`;
2. requires `quick.json.output` to exist;
3. deploys only the output directory.

OpenQuick does not infer frameworks or run package managers unless `quick.json` says to.

## Three-phase deploy lifecycle

### 1. Prepare

The CLI asks the host to create a staging area:

```bash
ssh quick@quickbox quickd deploy prepare --site lunch-vote --subdomain lunch-vote --json
```

`quickd` validates the site slug and subdomain label, checks reserved names, verifies subdomain uniqueness, creates the site directory when needed, acquires `deploy.lock`, creates `.incoming/<deploy-id>/files`, and returns the staging path plus an optional `link_dest`.

### 2. Transfer

The CLI mirrors local output into staging:

```bash
rsync -az \
  --delete \
  --partial-dir=.rsync-partial \
  --safe-links \
  --chmod=Dg+s,ug+rwX,o-rwx \
  --link-dest=/srv/quick/sites/lunch-vote/current \
  ./dist/ quick@quickbox:/srv/quick/sites/lunch-vote/.incoming/<deploy-id>/files/
```

`--delete` applies to staging, not the live release. `--safe-links` prevents symlinks that escape the release. `--link-dest` reuses unchanged files from the current release.

### ZIP deploys

`quick deploy site.zip` uses the same prepare and activate lifecycle, but transfers one archive instead of running rsync:

1. `prepare` creates the staging directory;
2. the CLI copies the ZIP to a temporary path on the host;
3. the CLI runs `quickd deploy extract-zip --site <site> --deploy-id <id> --zip <path> --json` over SSH using argv, not shell interpolation;
4. `activate` publishes the extracted staging tree.

Host extraction rejects unsafe archives: absolute paths, `..` traversal, duplicate entries, symlinks, encrypted entries, more than 1000 entries, more than 50 MiB compressed, or more than 100 MiB uncompressed. Archives with one top-level directory may be stripped so `dist/index.html` deploys as `index.html`.

### 3. Activate

The CLI asks the host to publish atomically:

```bash
ssh quick@quickbox quickd deploy activate --site lunch-vote --deploy-id <id> --json
```

`quickd` validates staging, writes `.quick-release.json`, renames staging files into `releases/<release-id>`, creates a new symlink, atomically renames it over `current`, records the deploy, and prunes old releases. When host signing is enabled, `.quick-release.json` also includes the release signature and public key.

Guarantee: a browser sees either the previous complete release or the next complete release, never an rsync-in-progress tree.

## Output

Human output should include profile, host, changed/reused/deleted counts, release ID, and URL.

JSON output uses a stable envelope:

```json
{
  "format_version": "1.0",
  "site": "lunch-vote",
  "profile": "lab",
  "release": "20260611T135901Z-a1b2c3",
  "url": "https://lunch-vote.quick.example.com",
  "changed": 18,
  "reused": 71,
  "deleted": 2
}
```

## First deploy bootstrap

Before the first deploy to a profile, run or let the CLI run:

```bash
ssh quick@quickbox quickd doctor --json
```

If `quickd` is missing, install with `quick serve install ...`. If `/srv/quick` is missing or unwritable, fix host permissions before transferring. If IAP or DNS is not ready, deploy only with an explicit unpublished/unsafe flag.

## Deleting a site

Use `quick delete` when a site should be removed from the host catalog and release storage:

```bash
quick delete lunch-vote
quick delete lunch-vote --yes          # non-interactive confirmation
```

Interactive deletes require typing the site name. Prefer deleting over deploying an empty directory when the URL should stop resolving.

## Public sites

By default, static assets are served behind the host identity layer. Operators can enable public static sites with host config:

```json
{
  "public_static": { "enabled": true }
}
```

Then a deployer can toggle a site:

```bash
quick public lunch-vote on
quick public lunch-vote off --yes
```

Turning public access on requires a typed confirmation and a passing static-only scan of the current release. Public sites serve GET/HEAD static assets without identity, but `/_quick/*` always requires auth. Future deploys to a public site are scanned again and fail if they start using OpenQuick dynamic APIs such as `quick.db`, `quick.uploads`, `quick.ai`, `quick.identity`, `quick.realtime`, or `/_quick/` calls.

## Custom domains

Custom domains map exact hostnames to cataloged sites:

```bash
quick domain add app.example.com --site lunch-vote
quick domain list
quick domain remove app.example.com
```

Domains are validated by the host, must not conflict with reserved names or the apex host, and are resolved before the subdomain fallback. Caddy on-demand TLS can use the host ask endpoint (`/_quick/domains/ask?domain=...`) from loopback or a trusted proxy.

## Directory listing

A normal static release should include `index.html`. To intentionally serve a directory browser, set the routing flag in `quick.json` or generated `site.json`:

```json
{
  "routing": {
    "directory_listing": true
  }
}
```

When this flag is true, activation allows directories without `index.html`. The static handler renders an escaped HTML listing for GET/HEAD only, excludes dotfiles, and sends no-cache headers.

## Browser deploy portal

Operators can enable browser ZIP deploys on the apex host:

```json
{
  "http_deploy": {
    "enabled": true,
    "allow_identities": ["alice@example.com"]
  }
}
```

When enabled and the viewer is authorized, the site directory shows a deploy panel with drag-and-drop ZIP upload, a site name field, and overwrite-confirm handling. The portal uses the same host lifecycle as the CLI (`prepare -> extract ZIP -> scan when public -> activate`). It is config-gated, accepts only same-origin browser requests on the apex host, and requires either a valid bearer token configured by the operator or an authenticated identity in `allow_identities`.

## Publishing a shared library site

A site can publish plain JavaScript for other OpenQuick sites to import. Deploy the library like any other static site:

```text
shared-ui/
  index.html          # optional landing page and examples
  mod.js              # exported browser module
```

```bash
quick deploy ./shared-ui --site shared-ui
```

Another site can import it directly from the sibling site URL:

```html
<script type="module">
  import { toast } from 'https://shared-ui.quick.example.com/mod.js';
  toast('Loaded from a shared OpenQuick site');
</script>
```

For cookie-based IAP edges such as Cloudflare Access, use credentialed module fetches so the browser sends the IAP cookie to the library origin:

```html
<script type="module" crossorigin="use-credentials" src="/app.js"></script>
```

OpenQuick reflects CORS only for GET/HEAD static assets requested from sibling site origins on the same host. The `/_quick/*` API remains same-origin.

## Browser site directory

Authenticated viewers can open the apex host, such as `https://quick.example.com/`, or the path-fallback root `https://quick.example.com/~/`, to see the built-in site directory. It lists cataloged sites with their URLs, current release, update time, and deployer. Operators can disable it with:

```json
{
  "directory": { "enabled": false }
}
```
