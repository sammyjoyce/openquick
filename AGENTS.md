# OpenQuick contributor instructions

OpenQuick is a C23/Zig `quick` CLI + TUI, a Go `quickd` host daemon, and a Bun-built same-origin JS SDK for static sites with shared host APIs.

## Component map

- `src/`: C23 `quick` CLI, shared ops layer, optional TUI, Zig build integration.
- `server/`: Go `quickd` for static routing, deploy activation, identity, and `/_quick/*` APIs.
- `sdk/js/`: dependency-free SDK built by Bun and embedded into `quickd`.
- `skills/`: agent skill source and `.skill` packaging.
- `docs/`: user docs and design records.
- `test/`: unit tests, CLI contract subprocess tests, and PTY/ghostty-vt terminal scenarios.
- `install/`: systemd, Caddy, Cloudflare, and Tailscale install assets.
- `examples/`: example OpenQuick sites.

## Build and test

Run focused checks while iterating. Run `just test-all` before committing.

```bash
zig build
zig build check
zig build terminal-test
zig build -Denable-tui=false
```

```bash
cd server && go build ./... && go vet ./... && go test -count=1 ./...
```

```bash
cd sdk/js && bun run build && bun test
```

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
