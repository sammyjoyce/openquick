# OpenQuick contributor instructions

OpenQuick is a C23/Zig `quick` CLI + TUI, a Go `quickd` host daemon, and a Bun-built same-origin JS SDK for static sites with shared host APIs.

## Component map

- `src/`: C23 `quick` CLI, shared ops layer, optional TUI, Zig build integration.
- `server/`: Go `quickd` for static routing, deploy activation, identity, and `/_quick/*` APIs.
- `sdk/js/`: dependency-free SDK built by Bun and embedded into `quickd`.
- `skills/openquick-deploy/`: end-user agent skill source. `skills/openquick-deploy.skill` is generated from it by `sh skills/build.sh`; never hand-edit the archive.
- `.agents/skills/`: repo-local reference skills. `.claude/skills/*` are symlinks into it; edit the `.agents/skills/` source.
- `docs/`: user docs and design records.
- `test/`: unit tests, CLI contract subprocess tests, and PTY/ghostty-vt terminal scenarios.
- `install/`: systemd, Caddy, Cloudflare, and Tailscale install assets.
- `examples/`: example OpenQuick sites.

## Build and test

Run the checks for the component you changed while iterating. Run `just test-all` before committing.

CLI/TUI (`src/`, `test/`):

```bash
zig build
zig build check
zig build terminal-test
zig build -Denable-tui=false
```

Daemon (`server/`):

```bash
cd server && go build ./... && go vet ./... && go test -count=1 ./...
```

SDK (`sdk/js/`):

```bash
cd sdk/js && bun run build && bun test
```

Everything:

```bash
just build-all
just test-all
```

After any CLI surface change, refresh and commit the OpenCLI contract:

```bash
zig build run -- opencli > opencli.json
```

`zig build test` enforces that `quick opencli` matches `opencli.json`.

After SDK changes, rebuild the SDK and embedded quickd copy:

```bash
just build-sdk
```

`just build-sdk` runs `bun run build` and copies `sdk/js/dist/quick.js` to `server/internal/api/sdk/quick.js`.

After editing `skills/openquick-deploy/`, rebuild the archive so it matches the source:

```bash
sh skills/build.sh
```

`skills/build.sh` requires `zip` and fails if the SKILL.md deploy safety checklist drops its dry-run or targeted doctor steps.

## Host and deploy authority

Routine build, unit, and local verification uses local paths: `quick serve --dev`, `quick deploy --dry-run`, and the test suites.
Real-host integration work is separate and needs an explicitly authorized target.
Do not run host-mutating commands (`quick deploy`, `quick serve install --execute`, `quick delete`, `quick restore`, `quick rollback`, `quick public SITE on`, `quick domain add|remove`)
against a real profile, or change `install/` targets on a live host, unless the user explicitly asks for that target in the current task.

## Architecture rules

- Put workflow logic in `src/core/ops.{h,c}`. Keep CLI handlers and TUI screens thin consumers. Do not duplicate business logic.
- Spawn subprocesses with argv arrays. Never build shell command strings for user-controlled values.
- Preserve server security invariants:
  - Strip inbound `X-Quick-*` headers.
  - Trust identity or source headers only from loopback or configured trusted proxies.
  - Keep static path and symlink hardening in `server/internal/static/static.go`.
  - Keep `/_quick/*` APIs same-origin.
  - Gate new risky features in host config and default them OFF.
- Keep SQLite migrations idempotent with `IF NOT EXISTS` or explicit column checks.
- Keep the vendored `cs_` component namespace unless a scoped migration changes all callers.
- Preserve the no-owner model. Use audit, signed manifests, and overwrite friction instead of per-site owners or ACLs.
- Keep the SDK fixed to the documented capability families: identity, DB, realtime, uploads, AI, warehouse, and capabilities.

## Never do

- Do not commit without a green `just test-all` or a documented reason it could not run.
- Do not regenerate `opencli.json` and ignore mismatches.
- Do not add SDK runtime dependencies; read `docs/design/UNJS_ASSESSMENT.md` first.
- Do not weaken fail-closed auth or origin checks.
- Do not bypass the ops layer from CLI or TUI code.

## Authoritative references

- Start with `docs/design/ARCHITECTURE.md` and `docs/design/WORKFLOW.md`.
- Use `docs/design/QUICK_PARITY.md` for parity decisions.
- Use `docs/design/DEFERRED_ASSESSMENT.md` for rejected ideas and why they stay out.
