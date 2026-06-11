# Contributing

Thanks for contributing. This page covers how to report an issue, set up locally, and send a PR that passes CI.

By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Quick start

```bash
# Fork, clone, branch
git clone https://github.com/yourusername/yourproject
cd yourproject
git checkout -b your-change

# Build and test
zig build check                                           # fmt-check + tests (the CI gate)
zig build -Denable-tui=true terminal-test                 # if you touched the TUI
zig build -Denable-tui=true -Dterminal-backend=ghostty terminal-test  # require PTY/TUI coverage

# Format, commit, push
zig build fmt
git commit -m "feat(scope): one-line summary"
git push origin your-change
```

Open a PR against `main`. CI runs `zig build check` on Linux, macOS, and Windows, plus `clang-tidy` and `cppcheck`, without requiring Nix by default.

## Reporting issues

Before opening one, search [existing issues](https://github.com/yourusername/yourproject/issues). Use the templates when they apply, and include:

- your operating system and version,
- `zig version`,
- the steps to reproduce,
- the expected versus actual behavior, and
- any error output or logs.

## Suggesting features

Open an issue describing the use case and how it fits the template's scope. Keep an eye on [docs/CONTRACTS.md](docs/CONTRACTS.md); the supported seams document what's intentionally in or out.

## Setting up

### Install Zig

Use [zvm](https://github.com/tristanisham/zvm) to install the pinned 0.16.0:

```bash
zvm install 0.16.0 && zvm use 0.16.0
```

### Platform dependencies

Default builds link curses/PDCurses for the TUI. Pass `-Denable-tui=false` for a
CLI/headless-only build that links only libc.

| Platform | Install |
| --- | --- |
| macOS | `brew install pkg-config ncurses` |
| Ubuntu / Debian | `sudo apt-get install pkg-config libncurses-dev clang-format clang-tidy` |
| Fedora / RHEL | `sudo dnf install pkg-config ncurses-devel clang-tools-extra` |
| Windows | `vcpkg install pdcurses:x64-windows` |

### Optional: Nix or devcontainer

```bash
nix develop                # convenience shell with Zig, C tooling, curses, Ghostty VT, markdownlint
```

The repository also ships a default devcontainer (open in VS Code, choose **Reopen in Container**) that uses Zig and normal Linux packages, not Nix.

## Build and test

For every `-D` option and step (and why Zig is the build system), see [docs/ZIG_PRIMER.md](docs/ZIG_PRIMER.md). The day-to-day commands:

```bash
zig build                                # debug build
zig build -Doptimize=ReleaseSafe         # optimized
zig build run -- hello Alice             # build + run with arguments
zig build test                           # unit tests + CLI contract tests
zig build terminal-test                  # unit + CLI tests; PTY/TUI skipped unless TUI + backend are available
zig build -Denable-tui=true terminal-test # TUI build; PTY scenarios run if libghostty-vt is found
zig build check                          # fmt-check + tests (the CI gate)
zig build fmt                            # apply formatting
zig build clean                          # remove zig-out + .zig-cache
```

Cross-compile with `-Dtarget=x86_64-windows` (or similar). The test layers and how to add to each are in [docs/TESTING.md](docs/TESTING.md).

### Adding a source file

Append the path to `base_sources` in `build.zig` (TUI-only files belong in `tui_sources`). The walkthrough is in [docs/ZIG_PRIMER.md#adding-a-c-file](docs/ZIG_PRIMER.md#adding-a-c-file).

### Adding a command, or a TUI screen

[examples/adding-a-command.md](examples/adding-a-command.md) is the full five-step flow (handler, command table, `opencli.json`, contract test, build). For interactive UIs, [examples/custom-tui.md](examples/custom-tui.md).

## Pull requests

1. Branch from `main`.
2. Run `zig build check`.
3. Use [Conventional Commits](https://www.conventionalcommits.org/); a pre-commit hook validates the format:

   ```
   <type>(<scope>): <subject>
   ```

   Common types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `perf`, `style`.

   ```
   feat(cli): add greet command
   fix(config): handle a missing config file gracefully
   docs(testing): document the unit-test step
   ```

4. Keep PRs focused. Update documentation when behavior changes (commands, flags, exit codes, the OpenCLI contract).

Local pre-commit hooks (install with `pre-commit install`) run clang-format, markdownlint, and the conventional-commit validator.
CI runs `zig build check` on three platforms plus `clang-tidy` and `cppcheck`; optional Ghostty VT coverage can be enabled with `CI_ENABLE_NIX_GHOSTTY=true`.
That optional job uses the nixpkgs `libghostty-vt` package pinned by `flake.lock`, so review lockfile bumps if you make it branch-protected.

## Coding standards

For C and C++, `.clang-format` is the source of truth — clang-format applies it
through the pre-commit hook and the CI **Lint and Format** job. For `build.zig`
and any Zig sources, `zig build fmt` runs the Zig formatter.

Conventions in this codebase:

- Public APIs use the `app_` / `tui_` prefixes (see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)).
- Functions return `app_error` for typed failures; check return values and free on every path.
- Buffers that held sensitive data go through `app_secret_zero()` before being freed.
- Early-return on errors; keep the happy path flat.

```c
app_error process_input(const char *input, size_t len) {
  if (!input || len == 0) {
    return APP_ERROR_INVALID_ARG;
  }

  char *scratch = malloc(len);
  if (!scratch) {
    return APP_ERROR_MEMORY;
  }

  app_error err = APP_SUCCESS;
  // ... work ...

  app_secret_zero(scratch, len);
  free(scratch);
  return err;
}
```

## Where things live

```text
.
├── src/
│   ├── main.c
│   ├── cli/        command parsing, help, command table
│   ├── core/       configuration, typed errors
│   ├── io/         text and JSON output
│   ├── tui/        ncurses windows, menus, progress (default unless disabled)
│   └── utils/      logging, colors, secret zeroing
├── test/           unit tests + CLI contract + PTY/TUI scenarios
├── build.zig
├── opencli.json    the CLI contract
└── docs/
```

## Getting help

- [docs/](docs/): architecture, contracts, testing, Zig primer
- [Existing issues](https://github.com/yourusername/yourproject/issues)

## License

By contributing you agree your contributions are licensed under the MIT License.
