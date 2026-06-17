# Changelog

All notable OpenQuick changes are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- TUI support is compiled in by default; pass `-Denable-tui=false` to build a
  headless-only binary.
- Bare `quick` on a TTY now opens the interactive TUI menu, while command
  invocations continue to use the CLI command table.
- Command output defaults to versioned JSON when stdout is not a terminal;
  pass `--plain` to preserve human text under pipes or redirection.
- Failing to open an explicit `--config` file now reports the specific public
  exit code — `NOT_FOUND` (17) for a missing path or `PERMISSION` (12) for an
  unreadable one — instead of a blanket I/O error (11).
- The interactive TUI now responds to `Esc` promptly (ESCDELAY tuned to 25 ms)
  rather than pausing up to a second on every Esc affordance.
- The interactive TUI honours the same color policy as the CLI: `NO_COLOR`,
  `FORCE_COLOR`, `CLICOLOR`/`CLICOLOR_FORCE`, and the `--no-color`/`--plain`
  flags now disable or force TUI color, where it previously consulted only the
  terminal's capability. Both surfaces resolve color through one `app_use_colors`
  helper.
- `quick menu` and the bare-TTY launch now reject conflicting JSON output with a
  single shared message and exit code, instead of two slightly different
  wordings on the two entry points.

### Added

- `packages.{quick,quickd}` flake outputs build release binaries directly
  (`nix build .#quick`, `nix build .#quickd`), so packagers no longer have to
  write their own recipes and `quickd` is no longer easy to miss.
- Experimental bare non-TTY headless protocol: read a JSON request from stdin
  and dispatch it through the existing command table.
- Unknown-command errors suggest the closest command ("Did you mean ...?"),
  computed over the visible command table.
- Color output honours the de-facto `FORCE_COLOR`, `CLICOLOR_FORCE`, and
  `CLICOLOR` environment variables, documented in `--help` and the OpenCLI
  contract.
- The TUI Commands browser now lists each command's example invocations
  alongside its summary.
- The TUI Commands browser now also shows each command's arguments and options,
  carried through the shared action seam so the detail view mirrors the usage
  `--help` renders.

### Fixed

- `quickd` no longer rejects releases that ship the CLI's `quick.json` site
  config. Release-config parsing now tolerates CLI-only fields (`source`,
  `output`, `build`, `profile`, `sdk`) instead of failing activation with
  `json: unknown field "source"`. Host-managed `site.json` is still parsed
  strictly.
- The JS SDK now returns documents in the documented `DocumentRecord` shape.
  `db.list()` unwraps the host's `{"documents": [...]}` envelope, and
  `create`/`get`/`update` results and DB subscription events flatten the
  host's `data` envelope so user fields sit next to `id` as documented.
- Headless JSON requests accept RFC 8259 `\uXXXX` escapes (including UTF-16
  surrogate pairs), matching what standard JSON serializers emit.
- A config file with an unknown key whose value is a nested object or array no
  longer fails the whole load; the value is skipped and known sibling keys
  still apply.
- `FORCE_COLOR=0` now disables color instead of (as before) enabling it.
- The TUI Commands browser hides commands marked hidden from `--help`, so it no
  longer lists the internal `menu` command.
- The plain-text `--help` ENVIRONMENT section now lists the same variables as
  the styled help and the OpenCLI contract (previously only three), all rendered
  from one shared table so the documentation cannot drift between build modes.

## [0.1.0]

### Added

- Initial C23 CLI foundation under `src/`, Zig 0.16 build system, opt-in ncurses/PDCurses TUI behind `-Denable-tui=true`.
- Live OpenCLI contract. `quick opencli` prints the spec, and `zig build test` fails if it drifts from `opencli.json`.
- Reusable `tui-menu-lib` static library target with installed headers.
- Three test layers: in-process unit tests (`zig build unit-test`), CLI contract tests, and Ghostty-VT-backed PTY/TUI scenarios.
- GitHub Actions CI on Linux, macOS, and Windows, plus `clang-tidy`, `cppcheck`, Gitleaks, OpenSSF Scorecard, SBOM generation, and pinned action versions.
- Nix dev shell, devcontainer, pre-commit configuration (clang-format, markdownlint, conventional-commit), and Dependabot.

[Unreleased]: https://github.com/sammyjoyce/openquick/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/sammyjoyce/openquick/releases/tag/v0.1.0
