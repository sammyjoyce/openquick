# `quick deploy`

`quick deploy` is an rsync-over-SSH deploy wrapper with host-side activation by `quickd`. It never rsyncs into the live directory.

## Common commands

```bash
quick deploy                         # deploy current site using quick.json
quick deploy ./dist --site demo       # deploy a folder directly
quick deploy --profile lab            # override profile
quick deploy --site lunch-vote        # override site/subdomain
quick deploy --dry-run                # show plan only
quick deploy --no-build               # skip quick.json build command
quick deploy --no-delete              # do not mirror deletions into the release
quick deploy --open                   # open the URL after activation
```

## Target resolution

Resolution order:

1. CLI flags.
2. Environment: `QUICK_PROFILE`, `QUICK_SITE`, `QUICK_REMOTE`, `QUICK_BASE_DOMAIN`.
3. Per-site `quick.json`.
4. User-global config.
5. Built-in local dev defaults.

Resolved values include `site`, `subdomain`, `profile`, SSH target, `remote_root`, `base_domain` or `base_url`, and IAP type.

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
ssh quick@quickbox quickd deploy prepare --site lunch-vote --json
```

`quickd` validates the site slug, checks reserved names, creates the site directory when needed, acquires `deploy.lock`, creates `.incoming/<deploy-id>/files`, and returns the staging path plus an optional `link_dest`.

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

### 3. Activate

The CLI asks the host to publish atomically:

```bash
ssh quick@quickbox quickd deploy activate --site lunch-vote --deploy-id <id> --json
```

`quickd` validates staging, writes `.quick-release.json`, renames staging files into `releases/<release-id>`, creates a new symlink, atomically renames it over `current`, records the deploy, and prunes old releases.

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
