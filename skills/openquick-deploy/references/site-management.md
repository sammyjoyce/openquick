# Site management

Use these commands after a host profile exists. OpenQuick has no per-site owners. Treat delete, public, domain, and overwrite operations as shared-host changes.

## List sites

List local state and default-profile rows:

```bash
quick list
```

Query the remote host and print machine-readable output:

```bash
quick list --profile lab --remote --json
```

Rows can include local cached deployments and remote catalog entries. If SSH is unavailable, local rows may be stale.

## Run doctor

Run local checks for the current site:

```bash
quick doctor
```

Include host checks for a profile and site:

```bash
quick doctor --profile lab --remote --site lunch-vote
```

Print JSON for an agent or CI log:

```bash
quick doctor --profile lab --remote --json
```

Run a deeper end-to-end check only when a temporary deploy and cleanup are acceptable:

```bash
quick doctor --profile lab --deep --json
```

Doctor check groups are `local`, `remote`, and `edge/iap`. Details and remediations are part of the command output.

## Delete a site

Delete removes a remote site from the host catalog and release storage. Prefer `quick delete` over deploying an empty directory when the URL should stop resolving.

```bash
quick delete lunch-vote --profile lab
```

Interactive deletion requires typing the exact site name. In non-interactive runs, use `--yes` only after explicit approval:

```bash
quick delete lunch-vote --profile lab --yes --json
```

## Toggle public static access

Show current public state:

```bash
quick public lunch-vote --profile lab
```

Turn public static assets on or off:

```bash
quick public lunch-vote on --profile lab
quick public lunch-vote off --profile lab
```

Use `--yes` only for an approved non-interactive public-on confirmation:

```bash
quick public lunch-vote on --profile lab --yes
```

Public mode requires host config `public_static.enabled: true`. It serves static GET/HEAD assets without identity, but `/_quick/*` remains authenticated.

Before public mode turns on, and before future deploys to a public site activate, the host runs a static-only scan. The scan rejects text files that contain any of these patterns:

```text
quick.db
quick.uploads
quick.ai
quick.identity
quick.realtime
/_quick/
```

The default scan examines up to 2000 files and up to 1 MiB per text file. It skips binary files and symlinks.
Remove SDK/API usage from the public site, keep the site private, or split public static assets into a separate site.

## Manage custom domains

List mapped custom domains:

```bash
quick domain list --profile lab --json
```

Add a domain to a site:

```bash
quick domain add app.example.com --site lunch-vote --profile lab
```

Remove a domain mapping:

```bash
quick domain remove app.example.com --profile lab
```

Domains are host-validated. They must not conflict with reserved names, the apex host, or existing catalog entries. Custom domains resolve before the subdomain fallback.

## Overwrite behavior

OpenQuick intentionally has no owners. Anyone with deploy access can overwrite any site, but the CLI adds friction when the last deployer differs from the current deployer.

Run a normal deploy:

```bash
quick deploy --site lunch-vote --profile lab
```

If `quickd deploy prepare --json` reports a different `last_deployer`, the CLI shows the last deployer and release.
In a TTY, type the exact site name to continue. Outside a TTY, the deploy fails unless `--yes` is present.

Use `--yes` only when the user explicitly approves the overwrite:

```bash
quick deploy --site lunch-vote --profile lab --yes
```

## Releases and rollback facts

A deploy never rsyncs into the live directory. The host stages files under `.incoming`, writes an immutable release under `releases/<release-id>`, and atomically swaps the `current` symlink.

Hosts retain old releases according to `retained_releases`, default 10. The host also maintains a `previous` symlink when there is a prior release.
Use the audited rollback command after explicit confirmation:

```bash
quick rollback lunch-vote --profile lab
quick rollback lunch-vote --profile lab --to 20260623T120000Z-abcd12 --yes
```

A failed activation leaves the previous complete release live. Browsers see either the old release or the new release, never a partial transfer.

## Local `.quick` state

A successful deploy writes local cache under the site root:

```text
.quick/deployments/<profile>.json
```

The record includes `profile`, `site`, `url`, `release`, and `deployed_at`. `quick open` and `quick list` can use this local state, but the host catalog is the source of truth.

Generated `.quickignore` excludes `.quick/` from deploy transfers:

```gitignore
.quick/
```

Do not commit `.quick/` or use it as authorization evidence.

## Interactive dashboard

On an interactive terminal, a bare `quick` opens the dashboard. The hidden command opens the same UI:

```bash
quick menu
```

Use the dashboard for Sites, Deploy, New site, Doctor, Serve, and Settings when the user wants an interactive workflow. Use CLI commands for non-interactive agent runs.
