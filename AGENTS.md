# Agent Instructions

## External References

| Need | File |
| --- | --- |
| Project overview and component usage | `README.md` |
| Contribution workflow and commit style | `CONTRIBUTING.md` |
| Architecture/module ownership | `docs/ARCHITECTURE.md` |
| Testing layers | `docs/TESTING.md` |
| Component catalog API | `docs/COMPONENTS.md` |
| Theming API | `docs/THEMING.md` |

## Commands

| Task | Command |
| --- | --- |
| Build debug binary | `zig build` |
| Run CI gate locally | `zig build check` |
| Run tests | `zig build test` |
| Run unit tests only | `zig build unit-test` |
| Validate component registry | `zig build registry` |
| Check formatting | `zig build fmt-check` |
| Apply formatting | `zig build fmt` |
| Build component CLI | `zig build curspan` |
| List catalog via CLI | `zig build curspan-run -- list` |
| Copy a component into a project | `zig build curspan-run -- add table --dest <dir>` |
| TUI/PTY scenarios when touched | `zig build -Denable-tui=true terminal-test` |

## Component Catalog

- Components live in `src/components/cs_*.{c,h}` and render only through `src/surface/`.
- Component styling uses semantic roles from `src/style/cs_theme.*` and `src/style/ui_theme.*`.
- Keep `registry/registry.json` in sync with component source files and dependency closures.
- `zig build test` also runs `curspan check` against `registry/registry.json`.

## Build Boundaries

- Keep `tools/curspan/` pure Zig; it is host-built and should not depend on project C sources.
- Keep `src/surface/surface_curses.c` and other curses-only code behind TUI-enabled builds.
- CLI-only or unit-test builds must not require ncurses/PDCurses.
- Zig formatting covers `build.zig`, `src/`, `test/`, and `tools/` via `zig build fmt-check`.
