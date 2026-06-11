# Justfile for Curspan
# Quick reference: https://just.systems/man/en/

set positional-arguments

# --- Build ---

# Build the default CLI
build:
    zig build

# Build with TUI support
build-tui:
    zig build -Denable-tui=true

# Build with release optimization
release:
    zig build -Doptimize=ReleaseSafe

# Build with release optimization and TUI support
release-tui:
    zig build -Doptimize=ReleaseSafe -Denable-tui=true

# Build a stripped, minimal-footprint release (no symbol table; ~4x smaller).
# Use for shipping when you don't need in-process backtraces. The plain
# `release`/`release-tui` recipes keep symbols for debugging.
release-min:
    zig build -Doptimize=ReleaseSafe -Denable-tui=true -Dstrip=true

# Build the reusable TUI menu static library
tui-menu-lib:
    zig build tui-menu-lib

# --- Run ---

# Run the application. Delegate menu to the TUI-enabled recipe.
run *args:
    if [ "${1:-}" = "menu" ]; then \
        just run-tui "$@"; \
    else \
        zig build run -- "$@"; \
    fi

# Run the application with TUI support
run-tui *args:
    zig build -Denable-tui=true run -- "$@"

# --- Test ---

# Run tests
@test:
    zig build test

# Run end-to-end terminal scenario tests (uses current build default)
terminal-test:
    zig build terminal-test

# Run terminal scenarios with TUI explicitly enabled
tui-test:
    zig build -Denable-tui=true terminal-test

# Require Ghostty VT-backed TUI scenarios
tui-test-ghostty:
    zig build -Denable-tui=true -Dterminal-backend=ghostty terminal-test

# Require Ghostty VT-backed TUI scenarios inside the Nix shell
nix-test-ghostty:
    nix develop --command zig build -Denable-tui=true -Dterminal-backend=ghostty terminal-test

# Run tests with different optimization levels
test-debug:
    zig build test -Doptimize=Debug

test-release:
    zig build test -Doptimize=ReleaseSafe

test-fast:
    zig build test -Doptimize=ReleaseFast

# --- Quality ---

# Check code formatting
fmt:
    zig build fmt

# Check code formatting without fixing
fmt-check:
    zig build fmt-check

# Run all checks (format + tests)
check:
    zig build check

# Guard the minimal footprint: the no-TUI, no-CLI-style build must stay
# libc-only (no curses) and small. Linux/ELF only (uses readelf).
footprint-check:
    #!/usr/bin/env bash
    set -euo pipefail
    zig build -Doptimize=ReleaseSafe -Denable-tui=false -Denable-cli-style=false -Dstrip=true
    bin=$(ls zig-out/bin/* | head -1)
    if readelf -d "$bin" 2>/dev/null | grep -qiE 'ncurses|curses|tinfo'; then
        echo "FAIL: minimal build links a curses library; a front-end TU leaked into the libc-only build"
        readelf -d "$bin" | grep -i needed
        exit 1
    fi
    size=$(stat -c%s "$bin")
    limit=98304  # 96 KiB: generous headroom over the ~49-69 KiB libc-only build; trips on a curses regression
    if [ "$size" -gt "$limit" ]; then
        echo "FAIL: minimal binary ${size} B exceeds ${limit} B"
        exit 1
    fi
    echo "OK: minimal libc-only build is ${size} B (limit ${limit} B), no curses NEEDED"

# --- Maintenance ---

# Clean build artifacts
clean:
    zig build clean

# --- Help ---

# Display available commands
help:
    @just --list --unsorted
