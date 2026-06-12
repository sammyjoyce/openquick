# OpenQuick

OpenQuick turns a folder of static HTML/assets into a private URL on a host you control. The `quick` CLI builds (when `quick.json` asks it to), transfers files with `rsync over SSH`, and asks the host daemon `quickd` to atomically activate the new release.

The default workflow is intentionally small:

```bash
quick serve install --profile lab --host quick@box --remote-root /srv/quick --domain quick.example.com --iap tailscale
quick init lunch-vote
cd lunch-vote
quick deploy
quick open
```

## What ships here

- `quick`: a C23 CLI built with Zig.
- Table-driven commands with human and JSON output.
- OpenCLI contract (`quick opencli`) checked against `opencli.json`.
- Site scaffolding (`quick init`) with `quick.json`, `.quickignore`, agent guidance, and bundled API notes.
- Deploy planning and rsync/quickd orchestration (`quick deploy`).
- URL resolution (`quick open`), catalog listing (`quick list`), local/remote diagnostics (`quick doctor`), and guided host install/dev serve commands (`quick serve`).
- The existing vendored TUI/component internals remain under `cs_` namespaces for CLI presentation; OpenQuick is a deployment tool, not a generic TUI starter.

## Requirements

Default CLI builds need:

- Zig 0.16.0.
- A system C toolchain/libc.
- `ssh` and `rsync` for real deployments.
- `quickd` on the remote host for prepare/activate/list/doctor admin commands.
- Curses development files for the optional TUI build (`-Denable-tui=false` disables it).

Optional PTY-backed TUI scenarios need `libghostty-vt` discoverable through `pkg-config`, or the Nix dev shell.

## Build

```bash
zig build                          # debug build; installs zig-out/bin/quick
zig build -Doptimize=ReleaseSafe   # optimized
zig build -Denable-tui=false       # without the ncurses/PDCurses TUI
zig build tui-menu-lib             # reusable internal TUI menu library
```

The default binary is `quick`; override with `-Dapp-name=...` when testing alternate packaging.

## Container

Build the Linux container image; the Docker build runs the Go server vet/tests and the CLI test suite in Linux stages by default:

```bash
docker build -t openquick:test .
```

Run `quickd` from the image on port 9366:

```bash
docker run --rm -p 127.0.0.1:9366:9366 openquick:test
curl http://127.0.0.1:9366/_quick/health
```

Run the repeatable end-to-end container smoke test:

```bash
scripts/container-smoke.sh
```

The smoke image uses a dev identity and an explicit `--allow-public-unsafe` listener for local container testing only.

## CLI quickstart

```bash
./zig-out/bin/quick --help
./zig-out/bin/quick init lunch-vote
./zig-out/bin/quick deploy lunch-vote --dry-run
./zig-out/bin/quick open lunch-vote --plain
./zig-out/bin/quick list --json
./zig-out/bin/quick doctor --json
./zig-out/bin/quick opencli
```

## Interactive TUI

Run `quick` with no arguments on an interactive terminal to open the dashboard; the hidden `quick menu` command opens the same TUI. Default builds include it, while `zig build -Denable-tui=false` skips the ncurses/PDCurses interface.

- Sites: browse local deployment records and remote host rows, then open, copy, deploy, or refresh a site.
- Deploy: resolve a plan, confirm it, watch progress, and inspect the result.
- New site: scaffold a blank or realtime site and optionally deploy it.
- Doctor: run local, remote, or deep diagnostics and open remediation details.
- Serve: start or stop a local dev server and generate a read-only host install guide.
- Settings: edit profile config or a selected `quick.json`, then write changes explicitly.

Full guide: [docs/user/tui.md](docs/user/tui.md).

`quick deploy` resolves target settings in this order: CLI flags, `QUICK_*` environment variables, `quick.json`, user config, then local defaults.

Useful environment variables:

- `QUICK_CONFIG_PATH`: override `~/.config/openquick/config.json`.
- `QUICK_PROFILE`: select a profile.
- `QUICK_SITE`: override the site slug.
- `QUICK_REMOTE`: override the SSH host.
- `QUICK_BASE_DOMAIN`: override deterministic `https://<site>.<domain>` URLs.
- `QUICK_QUICKD`: path to a local `quickd` for `quick serve --dev`.

## Config files

User profiles live at:

```text
$XDG_CONFIG_HOME/openquick/config.json
~/.config/openquick/config.json
```

Example:

```json
{
  "default_profile": "lab",
  "profiles": {
    "lab": {
      "ssh": "quick@quickbox",
      "remote_root": "/srv/quick",
      "base_domain": "quick.example.com",
      "iap": { "type": "tailscale", "mode": "localapi" },
      "deploy": { "delete": true, "open_after_deploy": false }
    }
  }
}
```

Per-site config lives in `quick.json` and is expected to be committed with the site.

## Test and check

```bash
zig build test
zig build terminal-test
zig build fmt-check
zig build check
```

`zig build test` runs in-process unit tests plus subprocess CLI contract tests and verifies `quick opencli` matches `opencli.json`.

## Repository layout

```text
src/cli/      command parsing, command table, and quick workflow commands
src/core/     config readers, deploy planning, slug/.quickignore/local state helpers
src/io/       text and JSON output
src/tui/      optional internal TUI/menu support
src/components/ vendored `cs_` rendering components
server/       Go quickd host daemon
test/         unit and subprocess contract tests
```

## License

MIT.
