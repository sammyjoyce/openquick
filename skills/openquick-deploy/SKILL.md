---
name: openquick-deploy
description: Use when creating, updating, or deploying an OpenQuick static site with the `quick` CLI and same-origin SDK.
---

# OpenQuick deploy skill

## When to use

Use this skill when the task is to:

- initialize an OpenQuick site;
- update a site's static files, `quick.json`, or SDK usage;
- deploy a folder or ZIP to a configured OpenQuick host;
- open, list, inspect, publish, or diagnose sites through the `quick` CLI.

Do not use this skill to change OpenQuick host/server internals unless the user explicitly asks for repository development work and the applicable repo instructions allow it.

## Core workflow

1. Inspect the project instructions first.
   - Read local `AGENTS.md` files that apply to the workspace.
   - Do not widen scope beyond the user's requested files or commands.
   - Do not put secrets in `quick.json`, source files, or deployed assets.
2. Initialize when needed:

   ```bash
   quick init my-site
   cd my-site
   ```

3. Confirm `quick.json`:

   ```json
   {
     "name": "my-site",
     "source": ".",
     "output": ".",
     "build": null,
     "profile": "default",
     "subdomain": "my-site",
     "sdk": { "enabled": true, "import": "/_quick/sdk.js" }
   }
   ```

4. Build locally only when `quick.json.build` says to:

   ```bash
   quick deploy --dry-run
   quick deploy
   ```

   Useful variants:

   ```bash
   quick deploy ./dist --site my-site
   quick deploy site.zip --site my-site
   quick deploy --profile lab --open
   quick deploy --no-build
   quick deploy --yes          # required for some non-TTY overwrite confirmations
   ```

5. Open, list, and diagnose:

   ```bash
   quick open my-site
   quick list
   quick doctor
   quick doctor --remote
   ```

6. Optional operations:

   ```bash
   quick public my-site on
   quick public my-site off --yes
   quick domain add app.example.com --site my-site
   quick domain list
   quick domain remove app.example.com
   quick delete my-site
   ```

## Deploy constraints

- Site names and subdomains must be DNS labels: lowercase ASCII letters, digits, hyphens, no leading/trailing hyphen, and no reserved labels.
- `quick deploy` is a three-phase host operation: prepare, transfer/extract, activate. Do not rsync directly into the live release.
- If another deployer last published the site, expect a typed site-name confirmation in a TTY or `--yes` in non-interactive runs.
- ZIP deploys are extracted host-side; archives must avoid traversal, absolute paths, symlinks, encrypted entries, duplicate entries, and size/count limits.
- Public sites require host enablement and a passing static-only scan. `/_quick/*` remains authenticated even when static assets are public.
- Browser deploy portal support is host-config gated and accepts ZIPs only from authorized identities or bearer tokens.
- Directory listings require `routing.directory_listing: true`; otherwise a release should include `index.html`.

## Repository constraints from `AGENTS.md`

When the workspace is the OpenQuick repository itself:

- Use the documented commands: `zig build`, `zig build check`, `zig build test`, `zig build unit-test`, `zig build registry`, `zig build fmt-check`, and `zig build fmt` as appropriate.
- Keep component sources under `src/components/cs_*.{c,h}` rendering through `src/surface/`.
- Keep `registry/registry.json` synchronized with component source files and dependency closures.
- Keep the component registry tool pure Zig.
- Keep curses-only code behind TUI-enabled builds so CLI-only and unit-test builds do not require curses.
- Run TUI/PTY scenarios with `zig build -Denable-tui=true terminal-test` when TUI behavior is touched.

## SDK reference

See `references/quick-sdk.md` in this skill for the same-origin OpenQuick JavaScript SDK API, including identity, document DB, uploads, realtime, AI, and warehouse queries.

## Building this skill archive

From the repository root, run:

```bash
sh skills/build.sh
unzip -l skills/openquick-deploy.skill
```

The archive root contains `SKILL.md` and `references/quick-sdk.md`.
