---
name: openquick
description: "Deploy static sites to a private OpenQuick host, use same-origin zero-config backend APIs, and set up or operate the host."
version: "0.1.0"
author: "OpenQuick"
homepage: "https://github.com/sammyjoyce/openquick"
---

# OpenQuick skill

Use this skill to create, deploy, manage, and operate OpenQuick sites. OpenQuick serves static files from a host you control.
It exposes a fixed same-origin SDK for identity, DB, realtime, uploads, AI, warehouse, and capabilities.

## When to use

Use this skill when the user says or implies:

- "deploy a site" or "publish this folder";
- "share a prototype internally";
- "set up an internal hosting box";
- "add a database/realtime/uploads/AI to a static site";
- "open/list/delete a site";
- "make a site public" or "add a custom domain";
- "debug `quick deploy`", "run doctor", or "fix OpenQuick host setup".

Do not use this skill to add custom servers, cron jobs, serverless functions, per-site owners, or new backend capability families. OpenQuick sites are static assets plus the fixed SDK surface.

## Core workflow

Run the happy path after a host profile exists:

```bash
quick init lunch-vote --profile lab
cd lunch-vote
quick deploy --dry-run
quick deploy --open
quick open --plain
```

Edit deployable files between `quick init` and `quick deploy`. Keep generated SDK imports same-origin:

```html
<script type="module">
  import { quick } from '/_quick/sdk.js';

  const me = await quick.identity.current();
  console.log(me.email || me.login || me.subject);
</script>
```

Deploy a built folder or ZIP when the site already has static output:

```bash
quick deploy ./dist --site lunch-vote --profile lab
quick deploy site.zip --site lunch-vote --profile lab
```

## Decision guide

| Situation | Use |
| --- | --- |
| No host, no profile, or no `quickd` yet | Read `references/host-setup.md`, run `quick serve install`, then verify with `quick doctor --remote`. |
| Site needs identity, DB, realtime, uploads, AI, or warehouse | Read `references/quick-sdk.md`, check `quick.capabilities()`, and degrade when a host gate is off. |
| Need to list, delete, toggle public access, add domains, inspect state, or understand rollback facts | Read `references/site-management.md`. |
| Deploy, DNS, IAP, identity, lock, or public-scan failures | Read `references/troubleshooting.md` and run the exact `quick doctor` scope for the profile and site. |

## Command quick reference

Global flags work with the command surface. Prefer command-specific `--json` when a command has it; use global `quick --json info` for `info`.

| Command | Use | Flags and examples |
| --- | --- | --- |
| `quick --help` | Show help. | `-h` is the short form. |
| `quick --version` | Show version. | No command required. |
| `quick [global flags] COMMAND` | Control output, config, and logging. | Global flags: `--debug`, `--quiet`, `--verbose`, `--json`, `--plain`, `--no-color`, `--config PATH`. |
| `quick init [path]` | Scaffold a static site. | Flags: `--template TEMPLATE`, `--name NAME`, `--profile PROFILE`. Examples: `quick init lunch-vote`; `quick init --template realtime --name lunch-vote --profile lab`. |
| `quick deploy [path]` | Build if configured, transfer a folder or ZIP, and activate a release. | Flags: `--site SITE`, `--subdomain SUBDOMAIN`, `--profile PROFILE`, `--dry-run`, `--no-build`, `--no-delete`, `--open`, `--bootstrap`, `--allow-unpublished`, `--checksum`, `--yes`. Examples: `quick deploy`; `quick deploy --dry-run`; `quick deploy ./dist --site demo --profile lab`; `quick deploy site.zip --site demo`. |
| `quick serve --dev` | Run local dev `quickd`. | Flags: `--port PORT`, `--identity EMAIL`, `--remote-api PROFILE`, `--profile PROFILE`. Example: `quick serve --dev --port 9366 --identity sam@example.com`. |
| `quick serve install` | Print or execute host setup steps. | Flags: `--profile PROFILE`, `--host HOST`, `--remote-root PATH`, `--domain DOMAIN`, `--iap IAP`, `--execute`, `--allow-public-unsafe`. Example: `quick serve install --profile lab --host quick@box --remote-root /srv/quick --domain quick.example.com --iap tailscale`. |
| `quick open [site]` | Open or print a site URL. | Flags: `--profile PROFILE`, `--copy`, `--plain`. Example: `quick open lunch-vote --profile lab --plain`. |
| `quick list` | List local and remote deployments. | Flags: `--profile PROFILE`, `--remote`, `--json`. Example: `quick list --profile lab --remote --json`. |
| `quick info` | Display application metadata. | Example: `quick --json info`. |
| `quick doctor [site]` | Run local, remote, and edge diagnostics. | Flags: `--profile PROFILE`, `--remote`, `--site SITE`, `--deep`, `--json`. Example: `quick doctor --profile lab --remote --site lunch-vote --json`. |
| `quick delete SITE` | Delete a remote site after confirmation. | Flags: `--profile PROFILE`, `--yes`, `--json`. Example: `quick delete lunch-vote --profile lab --yes --json`. |
| `quick public SITE [on\|off]` | Show or change a site's public-static flag. | Flags: `--profile PROFILE`, `--yes`, `--json`. Examples: `quick public lunch-vote`; `quick public lunch-vote on --yes`; `quick public lunch-vote off`. |
| `quick domain ACTION [domain]` | Manage custom domains. | Actions: `add`, `remove`, `list`. Flags: `--site SITE`, `--profile PROFILE`, `--json`. Examples: `quick domain add app.example.com --site lunch-vote --profile lab`; `quick domain list --profile lab --json`; `quick domain remove app.example.com --profile lab`. |
| `quick` or `quick menu` | Open the interactive dashboard on a TTY. | Use it for Sites, Deploy, New site, Doctor, Serve, and Settings. |
| `quick opencli` | Print the OpenCLI contract as JSON. | Use it to verify the installed CLI surface. |

## Constraints

- Deploy static files only. Build locally only when `quick.json.build` says to. Do not create a custom web server for the site.
- Keep secrets out of `quick.json`, source files, built assets, and browser code. AI keys, warehouse URLs, and provider credentials stay on the host.
- Use the fixed SDK capability set: identity, DB, realtime, uploads, AI, warehouse, and capabilities. Do not invent extra `/_quick/*` APIs from site code.
- Config-gated features may be unavailable. Public static access, browser ZIP deploys, AI, warehouse,
  remote API proxying, release signing, and SSH certificate enforcement are off unless the host enables them.
- There are no per-site owners or ACLs. Anyone with deploy access to the host can overwrite any site.
  Confirm with the user before overwriting a site last deployed by someone else; use `--yes` only for an explicit non-interactive approval.
- Public static mode serves only static GET/HEAD assets without identity. `/_quick/*` remains authenticated, and public sites must pass the static-only scan.
- ZIP deploys are untrusted input. The host rejects unsafe archives before activation.

## References

- `references/host-setup.md`: install a host, choose IAP, enable host gates, and verify with doctor.
- `references/quick-sdk.md`: use the same-origin JavaScript SDK.
- `references/site-management.md`: list, doctor, delete, public, domain, overwrite, rollback, `.quick`, and TUI facts.
- `references/troubleshooting.md`: map doctor failures to exact remediation commands.
