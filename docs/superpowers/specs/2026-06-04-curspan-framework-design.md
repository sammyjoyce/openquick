# Curspan Framework Design — a ShadCN-style framework for CLIs & TUIs

**Status:** Approved direction (set by repo goal: "refactor to be a proper high level
framework, think ShadCN, for CLIs and TUIs"). Implemented additively on branch
`framework-refactor`.
**Date:** 2026-06-04

## 1. Problem & intent

Curspan today is a high-quality **GitHub template**: you click "Use this template",
run a cleanup script, and own a generated C23 CLI/TUI app. Its `docs/CONTRACTS.md`
explicitly says *"Do not add … a broad TUI framework"*. The goal inverts that: make
Curspan a **framework** in the ShadCN sense — a curated catalog of themeable,
composable terminal UI components that downstream projects **own** (copy in), driven
by a shared design-token/semantic-role theme, distributed through a **registry + an
`add` CLI**.

ShadCN's defining properties, translated to a C23 terminal-UI framework:

| ShadCN pillar | Curspan realization |
| --- | --- |
| **Open code** — you own component source | Components are self-contained `.c/.h` pairs you copy into `src/components/`. |
| **Composition** — one consistent component API | A neutral **render surface** (`cs_surface`) + uniform `cs_<comp>` props/render contract, identical across CLI and TUI. |
| **Distribution** — `registry.json` + `npx shadcn add` | `registry/registry.json` + a `curspan` CLI: `curspan add table` copies a component and its dependency closure into your project. |
| **Beautiful defaults** | The existing amber-on-near-black ayu/gruvbox identity, kept as the default theme. |
| **Theming via tokens** | The existing **design tokens → semantic roles → renderer adapters** pipeline, promoted to the public `cs_theme` API with named themes and per-role overrides. |
| **AI-ready** | Open, consistent, documented component source with a machine-readable registry. |

## 2. What already exists (and why this is a refactor, not a rewrite)

The substrate is remarkably close already:

- **Theme pipeline** (`src/style/`): raw RGB `APP_DESIGN_PALETTE` → 26 adaptive
  (dark/light) semantic roles (`APP_UI_ROLE_LIST`) → renderer adapters. This *is*
  ShadCN's tokens→components model. It is the crown jewel and stays the core.
- **A stream render surface** (`src/cli/style/cli_term.c`): `app_cli_term_t` owns a
  `FILE*`+fd, resolves a 4-level color profile (none/16/256/truecolor), emits styled
  spans, measures width — with a swappable terminfo/ANSI backend. `app_cli_render_ctx_t`
  is already a "component context" (surface + compiled styles + clamped width).
  `cli_help_render` / `cli_error_render` / `cli_version_render` are already components.
- **A curses surface** (`src/tui/tui.c`): windows, borders, wrapped/centered text,
  color pairs, dialogs — but with **no component-context abstraction**; it threads raw
  ncurses color pairs and drops to `mvwprintw`/`waddch`.
- **Shared curses-free primitives** (`src/ui/`): `text_layout` (UTF-8 width/truncate/
  wrap) and `action_item` (command-table → selectable descriptors), already consumed
  by both front-ends.
- **One distributed component**: `tui-menu-lib` (versioned static lib + pkg-config +
  headers under `curspan/`). This is the *proof of concept* for component distribution.

**Gaps to close:** (a) no neutral surface unifying CLI+TUI, so components can't target
both; (b) degradation logic duplicated between CLI and TUI; (c) three divergent role
vocabularies; (d) no component catalog or consistent component API; (e) no public
theming API (named themes, per-role override); (f) no registry/`add` distribution; (g)
"template" identity throughout docs.

## 3. Architecture

```
                         ┌──────────────────────────────┐
                         │  curspan.h  (umbrella header) │
                         └──────────────────────────────┘
   theme (tokens→roles)        surface (neutral)            components
 ┌───────────────────┐   ┌─────────────────────────┐   ┌────────────────────┐
 │ design_tokens      │   │ cs_surface (interface)  │   │ cs_rule  cs_heading │
 │ ui_theme (roles)   │──▶│  ├ stream backend ──────┼──▶│ cs_badge cs_keyval  │
 │ cs_theme (public)  │   │  │   (app_cli_term_t)    │   │ cs_table cs_note    │
 │  named themes,     │   │  └ curses backend ───────┼──▶│ cs_list  cs_spinner │
 │  per-role override │   │      (WINDOW*)           │   │ cs_progress …       │
 └───────────────────┘   └─────────────────────────┘   │ interactive: menu,  │
          │                          │                   │ select, confirm,    │
          └───── shared resolver ────┘                   │ input, progress     │
            app_ui_resolve(role,mode,profile,colors)     └────────────────────┘
                                                                   │
                         registry/registry.json  +  `curspan add`  ◀┘
```

### 3.1 The neutral render surface — `cs_surface` (`src/surface/`)

The keystone new abstraction. A `cs_surface_t` is a drawing target that components
write to without knowing whether they render to a byte stream (CLI) or an ncurses
window (TUI). Two backends:

- **stream backend** — wraps the existing `app_cli_term_t` + compiled styles; emits
  SGR; degrades by color profile; `move` is a no-op (stream is top-to-bottom).
- **curses backend** — wraps a `WINDOW*`; draws cells with color pairs resolved via the
  shared resolver; supports cursor `move`. Only compiled when `ENABLE_TUI`.

Surface API (small, role-driven — components never name raw colors):

```c
size_t       cs_surface_width(const cs_surface_t *s);
cs_caps_t    cs_surface_caps(const cs_surface_t *s);   // profile, tty, unicode, interactive
void         cs_surface_set_role(cs_surface_t *s, cs_role_t role);     // fg
void         cs_surface_set_role_bg(cs_surface_t *s, cs_role_t role);  // bg (where supported)
void         cs_surface_set_attr(cs_surface_t *s, cs_attr_t attrs);    // bold/dim/underline/italic
void         cs_surface_reset(cs_surface_t *s);
void         cs_surface_write(cs_surface_t *s, const char *utf8);
void         cs_surface_write_n(cs_surface_t *s, const char *utf8, size_t bytes);
void         cs_surface_repeat(cs_surface_t *s, const char *glyph, size_t count);
void         cs_surface_newline(cs_surface_t *s);
void         cs_surface_move(cs_surface_t *s, int x, int y);  // curses only; no-op on stream
```

The surface owns degradation + capability; components stay pure layout + semantics.

### 3.2 One shared color resolver

Extract the duplicated `role → concrete color` degradation into one function in
`src/style/`:

```c
app_ui_resolved_t app_ui_resolve(app_ui_role_id role,
                                 app_ui_theme_mode_id mode,
                                 app_cli_color_profile_id profile,
                                 int color_count);
```

Both surface backends call it. **Existing `cli_theme.c` and `tui.c` are left untouched
in this pass** (their output is byte-pinned by tests); the new resolver is built from
the same math (`color_math.c`) so it produces identical decisions, and a follow-up can
migrate the old adapters onto it once snapshot-verified.

### 3.3 Public theming — `cs_theme` (`src/style/cs_theme.h`)

Additive public surface over `app_ui_*`:

- **Named theme registry**: `cs_theme_by_name("amber"|"mono"|…)`, `cs_theme_names()`.
  Default stays the amber identity. One or two extra schemes prove the catalog.
- **Per-role override** (not just accent): `cs_theme_set_role(theme, role, color)`.
- **Mode resolution owned here**: `cs_theme_mode_resolve(env)` — dedupes the
  `APP_CLI_THEME` parsing currently duplicated in `cli_layout.c` and `tui.c`.
- **Component-centric role aliases** layered over the help-centric roles
  (`PRIMARY`, `SECONDARY`, `BORDER`, `MUTED`, `DESTRUCTIVE`, `SUCCESS`, `WARNING`,
  `INFO`, `SELECTION`) — no existing role removed.

Existing `APP_CLI_ACCENT`/`APP_CLI_THEME` env contract preserved verbatim.

### 3.4 Component catalog (`src/components/`)

Each component is a self-contained `cs_<name>.h/.c` with a **props struct** (caller-owned
pointers, like the menu) and a render entry point. Two flavors:

- **Render-once** (work on *both* surfaces): `void cs_<name>_render(const cs_<name>_t *, cs_surface_t *)`.
- **Interactive** (TUI event loop): `cs_<name>_result_t cs_<name>_run(const cs_<name>_t *)`.

Initial catalog (chosen for coverage of the ShadCN "primitives" feel and real CLI/TUI
utility):

| Component | Surfaces | Notes |
| --- | --- | --- |
| `cs_rule` | both | horizontal divider, optional centered label |
| `cs_heading` | both | section title / banner, role-styled |
| `cs_badge` | both | inline status pill (success/warn/error/info/muted variants) |
| `cs_keyvalue` | both | aligned key→value list (used by `info`/`doctor`-style output) |
| `cs_table` | both | columns with alignment + width budgeting via `text_layout` |
| `cs_note` | both | callout block (info/success/warning/error) with gutter |
| `cs_list` | both | bulleted / numbered list with wrap + hanging indent |
| `cs_spinner` | both | non-interactive frame set (renders one frame; caller drives) |
| `cs_progress` | both | one-shot progress bar (distinct from interactive `tui_progress`) |
| `cs_menu` | tui | re-expose the existing reference menu under the umbrella |
| `cs_select` / `cs_confirm` / `cs_input` | tui | thin, themed wrappers over tui dialogs/menu |

Each ships a unit test asserting plain-surface (escape-free, non-TTY) output and, where
relevant, degraded-profile output.

### 3.5 Distribution — registry + `curspan` CLI (`registry/`, `tools/curspan/`)

The defining ShadCN feature.

- **`registry/registry.json`** — single source of truth. Each entry:

  ```json
  {
    "name": "table",
    "title": "Table",
    "category": "component",
    "surfaces": ["cli", "tui"],
    "description": "Columnar table with alignment and width budgeting.",
    "files": ["src/components/cs_table.c", "src/components/cs_table.h"],
    "dependencies": ["surface", "theme", "text-layout"],
    "since": "1.0.0"
  }
  ```

  Foundations (`surface`, `theme`, `text-layout`, `color-math`, `design-tokens`) are
  registry entries too, so dependency closures resolve.
- **`curspan` CLI** (`tools/curspan/main.zig`, built with `zig build curspan` →
  `zig-out/bin/curspan`; convenience steps `zig build registry`, `zig build add -- <name>`):
  - `curspan list` — print the catalog (grouped by category).
  - `curspan info <name>` — show a component's files + dependency closure.
  - `curspan add <name> [--dest DIR] [--dry-run]` — copy the component **and its
    transitive dependency closure** into `DIR` (default `./src`), then print the exact
    `build.zig` source lines to add. This is `npx shadcn add` for terminal UI.
  - Reads `registry/registry.json` via `std.json`; Zig is guaranteed present (it is the
    build toolchain), so no extra runtime dependency.
- **Registry integrity test** (`test/unit_registry_tests` or a Zig check): every file
  referenced exists; every dependency name resolves; no cycles. Keeps the manifest
  honest as components are added.

The registry tooling lives **outside** the app binary, so `opencli.json` and the 25
byte-pinned contract cases are untouched.

### 3.6 Public umbrella — `curspan.h`

A single header that includes the public framework surface (`cs_theme`, `cs_surface`,
the component headers, and the existing `tui`/command-runner seams). Installed as
`include/curspan/curspan.h` alongside the existing `tui-menu-lib` headers. The `cs_`
prefix is the public framework namespace; `app_`/`tui_` remain as the implementation
and stay back-compatible for the existing tests and the `tui-menu` contract.

## 4. Invariants the refactor must NOT break (the safety contract)

Pulled from the codebase map; every one is covered by a test or `opencli.json`:

1. **`opencli.json` byte-match**: `myapp opencli` equals the checked-in file (after the
   `myapp`→binary rename). Exit codes (0,1,2,3,4,5,6,7,10–17,20–25,130,143), command/
   flag/arg/example metadata, and the env-var table (`NO_COLOR`, `FORCE_COLOR`,
   `CLICOLOR(_FORCE)`, `APP_CLI_THEME`, `APP_CLI_COLOR`, `APP_CLI_OSC11`,
   `APP_CLI_ACCENT`, `APP_LOG_LEVEL`, `APP_CONFIG_PATH`) are frozen. → **No new CLI
   commands or flags in this pass.**
2. **25 CLI contract cases**: help content (USAGE/COMMANDS/doctor/debug line/env vars),
   hidden `menu`, arity errors, `--` semantics, unknown-command suggestions, headless
   JSON, single-JSON-envelope errors, requires-terminal gating.
3. **Theme unit invariants**: dark TITLE==palette amber, BORDER==muted (ansi16_hint 7),
   ERROR_DETAILS==red; light TITLE==RGB(135,94,20); accent reaches SELECTION_BG; color
   parse semantics; degradation indices (amber→256:180, →16 hint 11, →8:3).
4. **CLI style invariants**: non-TTY error = `Error: …` + `Try myapp --help for usage.`
   with **zero escape bytes**; `APP_CLI_COLOR=never` hard-disables; profile forcing.
5. **Config invariants**: flag-table/enum parallelism, exclusivity (last-wins), atomic
   load, parse caps, errno mapping, headless `\uXXXX` handling.
6. **Shared-primitive invariants**: `action_item` skips hidden / contiguous ids 1..N /
   zero-copy borrows; `text_layout` width/truncate/wrap column accounting + indent
   preservation + forward progress on bad UTF-8.
7. **`tui-menu` seam**: `TUI_MENU_VERSION`, `tui_show_menu` API, pointer-lifetime,
   installed headers under `curspan/`.
8. **All build flag combinations link**: `-Denable-tui`, `-Denable-cli-style`, both off
   (libc-only). New sources are gated so a minimal build still links.

**Strategy: additive.** Build the new public layer, surface, resolver, components,
theming, registry, and umbrella *alongside* the working code. Do not rewrite the
byte-pinned renderers in this pass. Keep `zig build check` green after every step.

## 5. Build & packaging changes (`build.zig`)

- New `src/surface/*.c`, `src/components/*.c`, `src/style/cs_theme.c`, and the shared
  resolver compile with the shared UI substrate (gated like `shared_ui_sources`; curses
  backend gated on `enable_tui`).
- New unit suites added to the `unit-tests` file list.
- New `curspan` CLI build step (`tools/curspan/main.zig`) + `registry`/`add` convenience
  steps; registry-integrity check wired into `test`.
- Umbrella + component headers installed alongside `tui-menu-lib` (extend the install
  list); bump nothing that the `tui-menu` pkg-config version pins.

## 6. Documentation reframe (`docs/`, `examples/`, `README.md`)

- **README / ARCHITECTURE**: lead with the framework identity (theme→surface→component,
  the catalog, `curspan add`). Keep the "use as a starter" path as one workflow, not the
  whole story.
- **CONTRACTS.md**: replace "opinionated reference app" framing; rewrite the "Not yet …
  broad TUI framework" section to describe the now-supported component/theming/registry
  surfaces and their stability tiers.
- **New `docs/COMPONENTS.md`** (catalog reference) and **`docs/THEMING.md`** (tokens,
  roles, named themes, overrides).
- **examples/custom-tui.md**: replace the raw-ncurses example with component composition
  through `cs_surface`.

## 7. Build sequence (phases, each ends green)

1. **Foundation**: shared resolver + `cs_theme` (named themes, mode resolve, per-role
   override, role aliases) + `cs_surface` (stream + curses backends) + `curspan.h`.
2. **Catalog**: author components (parallelizable — independent files) + unit tests.
3. **Distribution**: `registry.json` + `curspan` CLI + integrity test + build steps.
4. **Reframe docs/examples**.
5. **Wire build across all flag combos**; full `zig build check` green; adversarial
   review pass; fix; commit.

## 8. Non-goals (YAGNI)

- No plugin/ABI promise beyond the existing `tui-menu` seam and the new headers.
- No new runtime CLI commands/flags (protects the byte-pinned contract).
- No network registry / remote component fetching — the registry is local-first.
- No rewrite of the byte-pinned help/error/version/opencli renderers in this pass.
- No nested-subcommand engine (noted as a future possibility, not built now).
