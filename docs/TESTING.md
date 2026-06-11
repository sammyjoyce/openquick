# Testing CLI and TUI Behavior

The template ships three test layers so a generated project can cover ordinary command behavior and interactive terminal UI flows from day one.

| Layer | How it runs | What it asserts | Lives in |
| --- | --- | --- | --- |
| Unit tests | In-process, linked against the real sources | Logic inside `config`, `error`, `tui_menu_model`, and other modules | `test/unit_*.c` |
| CLI contract tests | The built binary as a subprocess | Exit codes, JSON fields, durable output, `myapp opencli` matching `opencli.json` | `test/cli_contract_*.c` |
| PTY/TUI scenarios | The binary in a real PTY via libghostty-vt | Rendered screen snapshots, input and resize handling | `test/terminal_vt_*.c` |

The first two run everywhere with no extra dependencies. The third needs libghostty-vt and is optional.

## Running the tests

```bash
zig build unit-test       # just the in-process unit tests
zig build test            # unit tests + CLI contract tests
zig build terminal-test   # unit + CLI tests; PTY/TUI skipped unless TUI + backend are available
zig build check           # fmt-check + tests
zig build -Dstrict=true check  # stricter local/CI gate: extra warnings as errors
```

PTY/TUI scenarios only run when the TUI is built and a terminal backend is present:

```bash
zig build terminal-test                                           # auto-run PTY scenarios when libghostty-vt is found
zig build -Dterminal-backend=ghostty terminal-test                # require Ghostty VT
zig build -Dterminal-backend=none terminal-test                   # never run PTY/TUI scenarios
```

## The Ghostty VT backend

`zig build terminal-test` defaults to `-Dterminal-backend=auto`. In auto mode it runs
unit and CLI contract tests everywhere, then runs Ghostty VT-backed PTY/TUI scenarios
when the default TUI build is enabled and `pkg-config` finds libghostty-vt with the
Terminal and Formatter APIs. The backend is a C runner (`test/terminal_vt_*.c`) that
runs the app in a real PTY, feeds output through
[`libghostty-vt`](https://libghostty.tip.ghostty.org/index.html), snapshots the virtual
terminal with Ghostty's formatter API, and drives deterministic input and resize
sequences.

Use `-Dterminal-backend=ghostty` to *require* it on POSIX hosts (the build fails if
TUI support or libghostty-vt is missing). Use `-Dterminal-backend=none` to skip PTY/TUI
coverage explicitly. Without the backend, auto mode still runs the unit and CLI contract
suites and prints a skip reason.

The Nix dev shell is a convenience path that wires Zig, the C toolchain, curses, and
`libghostty-vt` into `PATH` and `pkg-config`:

```bash
nix develop
zig build -Dterminal-backend=ghostty terminal-test
```

Outside Nix, install a libghostty-vt build that exposes the development
[Terminal](https://libghostty.tip.ghostty.org/group__terminal.html) and
[Formatter](https://libghostty.tip.ghostty.org/group__formatter.html) APIs so
`pkg-config` can find `libghostty-vt.pc`. If it lives outside a standard path, pass its
prefix:

```bash
zig build terminal-test \
  -Dterminal-backend=ghostty \
  -Dghostty-vt-prefix=/path/to/ghostty
```

## Writing unit tests

Unit tests link a subset of the production sources directly, so they exercise modules
like `core/config.c`, `core/error.c`, and `tui/tui_menu_model.c` without spawning a
process. Shared CLI/TUI style roles are covered in `test/unit_ui_theme_tests.c`, while
renderer-specific CLI behavior lives in `test/unit_cli_style_tests.c`. Add cases to the
closest existing file (or a new file registered in the `unit-test` sources in
`build.zig`), then run `zig build unit-test`.

Reach for a unit test when you can call a function and check its result directly. Reach for a CLI contract test when the behavior is only observable from the outside (exit code, stdout, JSON).

## Writing CLI scenario tests

Add cases to `test/cli_contract_cases.c`; the runner in `test/cli_contract_runner.c`
executes them against the built binary. Keep the CLI suite the single source of truth
for non-interactive behavior; the Ghostty runner is scoped to PTY/TUI only.

Prefer stable contracts over incidental prose:

- Assert exit status.
- Assert JSON fields for automation-facing commands.
- Keep `myapp opencli` byte-for-byte aligned with `opencli.json`.
- Match durable words, not whole paragraphs.
- Use `NO_COLOR=1` or `--plain` when color is not under test.

## Writing TUI scenario tests

The Ghostty VT backend has fixed C scenarios for the demo menu, including a
deterministic input/resize smoke test. Add project-specific scenarios in
`test/terminal_vt_scenarios.c` when you need cell-accurate screen snapshots or resize
coverage.

Prefer small step tables (`expect`, `send`, `resize`, `wait`) over long branch ladders, so a new screen does not require a second harness.

## What CI runs

CI runs `zig build -Dstrict=true check` on Linux, macOS, and Windows without Nix, so the unit and CLI
contract suites are enforced on every platform. CI also builds the TUI with platform
package managers and runs a `--json info` smoke check.

Maintainers who want required Ghostty VT coverage can set `CI_ENABLE_NIX_GHOSTTY=true`
to enable the separate Nix-backed job. That job uses `nix develop`, so its Ghostty VT
coverage comes from the nixpkgs `libghostty-vt` package pinned by this repository's
`flake.lock`, not the older direct Ghostty source flake build. It is intentionally not
part of the default release gate.

## Choosing a richer tool

The built-in Ghostty VT runner covers most project needs: a real PTY, modern VT parsing, formatter-backed snapshots, scrollback, and resize coverage, with no daemon or Rust wrapper. Keep it as the default.

Consider an adapter around an external tool when you need something it does not provide:

| You need… | Look at |
| --- | --- |
| PNG/SVG screenshots as review artifacts | recording tools like `evp`, VHS |
| Cell-level color/style assertions | `headless-terminal`, Phantom, Termscope |
| Cross-emulator compatibility checks | the same headless terminal CLIs |
| Detached sessions a human or agent can watch live | Phantom, Termscope |
| Property-based exploration of many input sequences | an external fuzzer over the PTY |

These are heavier than most template users need on day one, which is why they are not wired in by default.
