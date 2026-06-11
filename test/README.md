# Test Suite

Three test layers cover unit logic, CLI contracts, and interactive terminal flows.

| Layer | How it runs | What it asserts | Lives in |
| --- | --- | --- | --- |
| Unit tests | In-process, linked against the real sources | Logic inside `config`, `error`, `tui_menu_model`, and other modules | `unit_*.c` |
| CLI contract tests | The built binary as a subprocess | Exit codes, JSON fields, durable output, `myapp opencli` matching `opencli.json` | `cli_contract_*.c` |
| PTY/TUI scenarios | The binary in a real PTY via libghostty-vt | Rendered screen snapshots, input and resize handling | `terminal_vt_*.c` |

## Running them

```bash
zig build unit-test       # just the in-process unit tests
zig build test            # unit tests + CLI contract tests
zig build terminal-test   # unit + CLI tests; PTY/TUI skipped unless TUI + backend are available
```

`zig build terminal-test` defaults to `-Dterminal-backend=auto`. It always runs the unit
and CLI contract suites, then runs PTY/TUI scenarios only when `-Denable-tui=true` is set
and libghostty-vt is available through `pkg-config` on POSIX hosts. Without libghostty-vt
auto mode prints a skip reason; pass `-Dterminal-backend=ghostty` to require the PTY
backend, `-Dterminal-backend=none` to disable it explicitly, or `-Dghostty-vt-prefix=/path`
to override the install prefix. The Nix dev shell is an optional convenience path for
Ghostty VT coverage.

See [docs/TESTING.md](../docs/TESTING.md) for the full guide, including how to write each layer.
