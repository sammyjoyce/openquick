# Architecture Overview

This page is a developer map for the current repository. The authoritative product
architecture and workflow are in [`docs/design/ARCHITECTURE.md`](design/ARCHITECTURE.md)
and [`docs/design/WORKFLOW.md`](design/WORKFLOW.md).

OpenQuick has three first-class runtime components:

- `quick`: the C23 CLI built by `zig build`.
- `quickd`: the Go host daemon under `server/`.
- `/_quick/sdk.js`: the browser SDK built from `sdk/js/` and embedded into
  `quickd`.

```mermaid
graph TD
    User[developer] --> CLI[quick CLI]
    CLI --> SSH[ssh + rsync]
    SSH --> Daemon[quickd]
    Daemon --> Sites[/srv/quick/sites]
    Browser[browser] --> Edge[IAP / TLS edge]
    Edge --> Daemon
    Daemon --> SDK[/_quick/sdk.js]
```

## Repo map

| Path | Responsibility |
| --- | --- |
| `src/cli/` | CLI argument parsing, command table, OpenCLI contract, and OpenQuick workflow commands |
| `src/core/` | App metadata, config readers, site/profile config, deploy planning, JSON helpers, typed errors |
| `src/io/` | Bounded input, human output, JSON output, and terminal capability checks |
| `src/style/`, `src/surface/`, `src/components/` | Vendored `cs_` rendering and theming internals used by CLI/TUI output |
| `src/tui/` | Optional ncurses/PDCurses menu and diagnostics UI |
| `server/` | Go `quickd` module: static serving, APIs, identity adapters, deploy activation, store |
| `sdk/js/` | Browser SDK source, tests, and bundled `dist/quick.js` |
| `install/` | systemd, Caddy, cloudflared, and Tailscale examples |
| `examples/sites/` | OpenQuick static site examples |
| `docs/user/` | User-facing workflow and IAP documentation |
| `test/` | C CLI unit and contract tests |

## CLI request lifecycle

1. `src/main.c` initializes logging and creates an `app_config_t`.
2. `src/cli/args.c` handles immediate options and applies global flags.
3. `src/cli/commands.c` resolves the command and dispatches to the handler.
4. OpenQuick handlers in `src/cli/commands_*.c` call helpers in `src/core/` to
   read `quick.json`, profiles, `.quickignore`, and deploy-plan inputs.
5. Handlers write either human output or versioned JSON through `src/io/`.
6. Failures return an `app_error` value that maps to the process exit status and
   the checked-in `opencli.json` contract.

## Server lifecycle

`quickd` is a normal Go module rooted at `server/`.

- `server/cmd/quickd` owns the CLI entry point (`serve`, `deploy`, `list`,
  `sites`, `doctor`).
- `server/internal/static` routes site hostnames and serves immutable release
  trees.
- `server/internal/deploy` prepares staging directories and atomically activates
  releases.
- `server/internal/identity` normalizes dev, Tailscale, and Cloudflare identity
  sources.
- `server/internal/api` exposes `/_quick/health`, `/_quick/identity`,
  `/_quick/capabilities`, and the embedded SDK.
- `server/internal/store` owns the SQLite catalog.

## Build system

Day-to-day component builds are wired through `just`:

```bash
just build-sdk     # sdk/js -> sdk/js/dist/quick.js -> server/internal/api/sdk/quick.js
just build-server  # Go quickd -> zig-out/bin/quickd
just build         # Zig/C quick CLI -> zig-out/bin/quick
just build-all     # SDK, then quickd, then quick
just test-all      # zig build test, go test ./..., bun test
```

`zig build test` still validates the C CLI contracts and the vendored component
registry used by the CLI presentation layer. `cd server && go test ./...` covers
`quickd`. `cd sdk/js && bun test` covers the browser SDK surface.

## Stable contracts

- `opencli.json` and `quick opencli` are the CLI automation contract.
- JSON command output uses versioned envelopes.
- Site config is `quick.json`; user config lives under `openquick/config.json`.
- Browser apps import the same-origin SDK from `/_quick/sdk.js`.
- Host admin operations go through `quickd` commands over SSH.

See [CONTRACTS.md](CONTRACTS.md) for the lower-level C CLI and internal UI seams.
