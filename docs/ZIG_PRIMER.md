# Zig Primer for C Developers

You do not need to know Zig to work on this project. Zig is only the build system: it
replaces Make or CMake, and it bundles the C compiler. This guide covers the handful of
commands and the `build.zig` structure you will actually touch.

- [Why Zig here?](#why-zig-here)
- [The commands you actually need](#the-commands-you-actually-need)
- [How this build.zig is organized](#how-this-buildzig-is-organized)
- [Build options](#build-options)
- [Build steps](#build-steps)
- [Adding a C file](#adding-a-c-file)
- [Cross-compiling](#cross-compiling)
- [Zig vs Make and CMake](#zig-vs-make-and-cmake)
- [Troubleshooting](#troubleshooting)
- [Resources](#resources)

## Why Zig here?

- **One toolchain, every target.** `zig cc` bundles Clang/LLVM plus the headers and libc for every platform, so cross-compilation is a flag, not a second toolchain.
- **The build script is just code.** `build.zig` is Zig, not a bespoke macro language, so logic like "only compile the TUI when a flag is set" is an ordinary `if`.
- **Caching and parallelism are automatic.** Incremental rebuilds and parallel compilation come for free.
- **It is still your C.** The sources are C23. Zig compiles and links them; it does not change how you write them.

## The commands you actually need

```bash
zig build                         # build + install the binary to zig-out/bin/
zig build run -- hello Alice      # build, then run with arguments after --
zig build test                    # CLI contract tests + in-process unit tests
zig build -Doptimize=ReleaseSafe  # optimized build
zig build run                     # on a TTY, open the default TUI menu
```

A `justfile` wraps the common ones if you prefer: `just build`, `just check`, `just test-fast`, `just clean`. Run `just help` to list them.

## How this build.zig is organized

The script builds **one executable from a list of C files.** There is no Zig source in
the binary. This excerpt is simplified from the real `build.zig`; open that file for
the full source list and flag handling.

```zig
const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const app_name = b.option([]const u8, "app-name", "Application and binary name") orelse "myapp";
    const enable_tui = b.option(bool, "enable-tui", "Enable the ncurses/PDCurses TUI") orelse true;

    const exe = b.addExecutable(.{
        .name = app_name,
        .root_module = b.createModule(.{
            .root_source_file = null, // a C program: no Zig root file
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        }),
    });
    exe.root_module.addIncludePath(b.path("src"));

    const base_sources = [_][]const u8{
        "src/main.c",
        "src/core/config.c",
        // ... full list lives in build.zig
    };
    // The real script also appends -DAPP_VERSION, -DAPP_NAME, -DAPP_GIT_COMMIT,
    // and -DENABLE_TUI=1 (when enabled) to these flags.
    const base_flags = [_][]const u8{ "-Wall", "-Wextra", "-std=c23", "-D_GNU_SOURCE" };
    exe.root_module.addCSourceFiles(.{ .files = &base_sources, .flags = &base_flags });

    if (enable_tui) {
        exe.root_module.addCSourceFiles(.{
            .files = &.{
                "src/tui/tui.c",
                "src/tui/tui_app.c",
                "src/tui/tui_menu.c",
                "src/tui/tui_menu_model.c",
                "src/tui/tui_progress.c",
            },
            .flags = &base_flags,
        });
        if (target.result.os.tag == .windows) {
            exe.root_module.linkSystemLibrary("pdcurses", .{});
        } else {
            exe.root_module.linkSystemLibrary("ncursesw", .{});
        }
    }

    b.installArtifact(exe);
}
```

The pieces to recognize:

- **`b`** is the build graph. You add artifacts (executables, libraries) and steps to it.
- **`b.createModule`** with `root_source_file = null` and `link_libc = true` is how a C program is declared in modern Zig. Source files attach to `exe.root_module`, not to `exe` directly.
- **`base_sources` / `base_flags`** are plain arrays. Adding a file means editing an array (see [Adding a C file](#adding-a-c-file)).
- **`b.option(...)`** declares the `-D...` flags listed below.

## Build options

Pass these as `-D<name>=<value>` on any `zig build` command.

| Option | Values (default) | Effect |
| --- | --- | --- |
| `-Doptimize=` | `Debug` (default), `ReleaseSafe`, `ReleaseFast`, `ReleaseSmall` | C optimization level (no Zig runtime is linked into this C-only binary) |
| `-Dtarget=` | e.g. `x86_64-windows`, `aarch64-macos` | Cross-compile target |
| `-Denable-tui=` | `true` / `false` (default `true`) | Compile the ncurses TUI and link curses |
| `-Denable-cli-style=` | `true` / `false` (default `true`) | Compile the styled CLI help/error/version layer (uses terminfo when available, ANSI otherwise) |
| `-Dcli-terminfo=` | `auto` (default) / `required` / `disabled` | Terminfo backend policy for the CLI styling layer |
| `-Dapp-name=` | string (default `myapp`) | Application and binary name |
| `-Dversion=` | string (default `0.1.0`) | Version baked into the binary |
| `-Dstrict=` | `true` / `false` (default `false`) | Add extra warnings and treat warnings as errors |
| `-Dharden=` | `true` / `false` (default `false`) | Add supported compiler hardening flags such as stack protector and fortify |
| `-Dstrip=` | `true` / `false` (default `false`) | Strip symbols from the installed binary (~4x smaller; drops in-process backtraces). The `just release-min` recipe sets this |
| `-Dcurses-prefix=` | path | Override the ncurses/PDCurses install prefix |
| `-Dterminal-backend=` | `auto` (default) / `none` / `ghostty` | PTY/TUI terminal-test backend selection; see [TESTING.md](TESTING.md) |
| `-Dghostty-vt-prefix=` | path | Override the libghostty-vt install prefix |

### Binary footprint

The default `zig build` is a `Debug` binary (~4.6 MB) that keeps full symbols.
For shipping, pick a release optimization and decide whether you need symbols.
Measured `ReleaseSafe` sizes (x86-64 Linux):

| Configuration | Size | Links |
| --- | --- | --- |
| default (TUI + CLI styling), unstripped | ~549 KB | libc, ncursesw |
| default, `-Dstrip=true` (`just release-min`) | ~139 KB | libc, ncursesw |
| `-Denable-tui=false -Dstrip=true` | ~93 KB | libc, ncursesw |
| `-Denable-tui=false -Denable-cli-style=false -Dstrip=true` | ~68 KB | libc only |

Disabling both front-ends drops the curses dependency entirely, leaving a
libc-only binary for headless/JSON deployments. `just footprint-check` (and a
CI step) guards that the libc-only build never regains a curses dependency.

For the absolute minimum, `-Doptimize=ReleaseSmall` trims further at the cost of
some speed: stripped default ~102 KB / `ReleaseFast` ~115 KB, and the libc-only
build reaches ~49 KB under `ReleaseSmall`.

## Build steps

| Command | What it does |
| --- | --- |
| `zig build` | Build and install the binary (the default step) |
| `zig build run -- ARGS` | Build, then run with `ARGS` |
| `zig build test` | CLI contract tests plus in-process unit tests |
| `zig build unit-test` | Only the in-process unit tests |
| `zig build terminal-test` | Unit and CLI tests plus PTY/TUI scenarios when TUI + backend are available |
| `zig build tui-menu-lib` | Build the reusable TUI menu static library and install its headers + `pkgconfig/tui-menu.pc` |
| `zig build fmt` / `fmt-check` | Format, or check formatting of, `build.zig`, `src`, and `test` |
| `zig build check` | Baseline gate: `fmt-check` + tests (what CI runs) |
| `zig build clean` | Remove `zig-out` and `.zig-cache` |

## Adding a C file

1. Drop the file under `src/`.
2. Add its path to the source array in `build.zig` that matches when it should
   compile. `build()` keeps a few named lists so each file lands in exactly one
   place:

   | Array | Compiled when | For |
   | --- | --- | --- |
   | `base_sources` | always | core CLI/app code |
   | `shared_ui_sources` | TUI **or** CLI styling enabled | text layout + color math + design tokens + semantic UI theme roles shared by both front-ends |
   | `cli_style_sources` | `-Denable-cli-style` (default on) | CLI help/error/version renderers |
   | `tui_sources` | `-Denable-tui` (default on) | ncurses screens |

   Most new code is `base_sources`:

   ```zig
   const base_sources = [_][]const u8{
       // ... existing files ...
       "src/features/deploy.c",
   };
   ```

3. Rebuild with `zig build`. Headers are found automatically because `src/` is
   on the include path. If a TUI-only file references a shared primitive (text
   layout or the design palette), that primitive is already in
   `shared_ui_sources`, so it links in every front-end combination.

## Adding a package dependency

`build.zig.zon` starts with `.dependencies = .{}`. To vendor a C library (or
another Zig package) reproducibly:

1. Fetch and pin it. `zig fetch` records the URL and a content hash in
   `build.zig.zon` so builds are hermetic:

   ```bash
   zig fetch --save=cjson https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz
   ```

   This adds a `.cjson = .{ .url = ..., .hash = ... }` entry under
   `.dependencies`.

2. Wire it into a target in `build.zig`. Resolve the dependency, then either add
   its C sources to a module or link a library it builds:

   ```zig
   const cjson = b.dependency("cjson", .{ .target = target, .optimize = optimize });
   exe.root_module.addIncludePath(cjson.path("."));
   exe.root_module.addCSourceFiles(.{
       .files = &.{"cJSON.c"},
       .flags = c_flags.items,
       .root = cjson.path("."),
   });
   ```

3. `zig build` fetches the pinned archive into the global cache on first use; the
   hash in `build.zig.zon` makes the dependency tamper-evident. Commit the
   updated `build.zig.zon` so collaborators and CI resolve the same revision.

Keep dependencies pinned by hash (never a moving branch) and prefer adding their
sources to a dedicated module so your `c_flags` and warning policy still apply.

## Cross-compiling

```bash
# Windows binary from any host
zig build -Dtarget=x86_64-windows -Doptimize=ReleaseSafe

# Apple Silicon macOS binary
zig build -Dtarget=aarch64-macos -Doptimize=ReleaseSafe
```

The default build includes the TUI, so cross builds need the curses library for the
target; point at it with `-Dcurses-prefix=`. Use `-Denable-tui=false` for a
CLI/headless-only cross build with no curses dependency.

## Zig vs Make and CMake

| Concern | Zig | Make | CMake |
| --- | --- | --- | --- |
| Build language | Zig | Make syntax | CMake script |
| Cross-compilation | Built in | Manual toolchains | Toolchain files |
| Dependencies | `build.zig.zon` | None | FetchContent / ExternalProject |
| Platform detection | Automatic | Manual | Automatic |
| Caching and parallelism | Automatic | `make -j`, manual cache | Automatic |
| Learning curve | Moderate | Low | High |

A Makefile rule like this:

```makefile
myapp: main.c args.c
	gcc -std=c23 -Wall -O2 -o $@ $^ -lncursesw
```

becomes, in `build.zig`:

```zig
const exe = b.addExecutable(.{
    .name = "myapp",
    .root_module = b.createModule(.{
        .root_source_file = null,
        .target = target,
        .optimize = .ReleaseSafe,
        .link_libc = true,
    }),
});
exe.root_module.addCSourceFiles(.{
    .files = &.{ "main.c", "args.c" },
    .flags = &.{ "-std=c23", "-Wall" },
});
exe.root_module.linkSystemLibrary("ncursesw", .{});
b.installArtifact(exe);
```

## Troubleshooting

**`zig: command not found`**. Install Zig 0.16.0. The simplest route is [zvm](https://github.com/tristanisham/zvm): `zvm install 0.16.0 && zvm use 0.16.0`.

**ncurses/PDCurses headers or library not found.** The TUI is enabled by default.
Install the dev package (`apt install libncurses-dev`, `brew install ncurses`,
`dnf install ncurses-devel`), point at a custom install with `-Dcurses-prefix=/path`,
or pass `-Denable-tui=false` for a CLI/headless-only build.

**libghostty-vt not found for terminal tests.** The PTY/TUI backend is optional. See
[TESTING.md](TESTING.md), or pass `-Dghostty-vt-prefix=/path`. Without it, auto mode
still runs `zig build test` and prints a PTY/TUI skip reason. Use
`-Dterminal-backend=ghostty` to require the backend, or `-Dterminal-backend=none` to
skip it explicitly.

**Stale build.** `zig build clean` removes `zig-out` and `.zig-cache`. (Equivalent: `rm -rf zig-out .zig-cache`.)

**Need to see what the compiler is doing.** `zig build --verbose` shows commands; `zig build --verbose-cc` shows the C compiler invocations.

## A real custom build step

Build logic is ordinary Zig, so `build.zig` can compute inputs. This project injects the current git hash as a `#define`, falling back to `"unknown"` outside a checkout:

```zig
const git_commit = blk: {
    const res = std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "git", "rev-parse", "--short", "HEAD" },
    }) catch break :blk "unknown";
    // ... verify exit status ...
    break :blk b.dupe(std.mem.trim(u8, res.stdout, "\r\n"));
};
// added to the compile flags as: -DAPP_GIT_COMMIT="<hash>"
```

`myapp info` then reports that hash. Use the same pattern for any value you want baked into the binary at build time.

## Resources

- [Zig 0.16.0 Language Reference](https://ziglang.org/documentation/0.16.0/)
- [Zig Build System Guide](https://ziglang.org/learn/build-system/)
- [This project's build.zig](../build.zig) - the real script, the best reference
