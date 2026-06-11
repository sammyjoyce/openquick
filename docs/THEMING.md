# Theming

Curspan's design system is the framework's core. Every component styles itself
through **semantic roles**, never hard-coded colors, so re-theming an app — or
adapting it to a 16-color terminal — is a one-line change, not a sweep through
render code. This is the terminal-UI analogue of a ShadCN CSS-variable theme.

- [Three layers](#three-layers)
- [Roles](#roles)
- [Using a theme](#using-a-theme)
- [Overriding roles](#overriding-roles)
- [Light and dark mode](#light-and-dark-mode)
- [Color degradation](#color-degradation)
- [Environment contract](#environment-contract)

## Three layers

```
design tokens          semantic roles              themes
(raw sRGB values)  ->  (named, adaptive colors) ->  (a named, overridable
 design_tokens.h        ui_theme.h                   bundle of roles + a mode)
                                                     cs_theme.h
```

1. **Design tokens** (`src/style/design_tokens.c`) — the raw sRGB palette. The
   single source of truth for color *values*. Curspan's default identity is
   amber-on-near-black (an ayu/gruvbox feel).
2. **Semantic roles** (`src/style/ui_theme.c`) — each role (e.g. `TITLE`,
   `BORDER`, `SUCCESS`) is an adaptive color with a `{dark, light}` pair, derived
   from the tokens. Roles carry an optional 16-color hint so degradation stays
   faithful.
3. **Themes** (`src/style/cs_theme.h`) — a `cs_theme_t` is the value components
   and surfaces consume: a role table (`scheme`), an active light/dark `mode`,
   and a `name`.

```c
typedef struct cs_theme {
  app_ui_color_scheme_t scheme;  // the role table (by value, copyable)
  cs_mode_t mode;                // active light/dark mode
  const char *name;              // built-in name, or "custom"
} cs_theme_t;
```

## Roles

Components reference roles through render-neutral aliases (`cs_theme.h`), so
component code reads `cs_surface_set_role(s, CS_ROLE_PRIMARY)` rather than naming
help-text tokens. The aliases layer over the shared UI roles — no role is
removed, so the styled CLI and the TUI keep sharing one vocabulary.

| Role | Default meaning |
| --- | --- |
| `CS_ROLE_TEXT` | body text |
| `CS_ROLE_MUTED` | de-emphasized / secondary text |
| `CS_ROLE_PRIMARY`, `CS_ROLE_ACCENT` | the brand accent (amber) |
| `CS_ROLE_TITLE`, `CS_ROLE_HEADING` | headings and titles |
| `CS_ROLE_CODE` | inline code / literals |
| `CS_ROLE_SUCCESS` | success / ok |
| `CS_ROLE_WARNING` | warnings |
| `CS_ROLE_ERROR` | errors |
| `CS_ROLE_INFO` | informational |
| `CS_ROLE_BORDER` | rules, separators, frames |
| `CS_ROLE_SELECTION_FG`, `CS_ROLE_SELECTION_BG` | selected / highlighted row |
| `CS_ROLE_PANEL` | panel background |

`CS_ROLE_PRIMARY`/`CS_ROLE_ACCENT` and `CS_ROLE_TITLE`/`CS_ROLE_HEADING` are
aliases for the same underlying role — pick whichever reads best in your code.

## Using a theme

Most apps never touch a theme directly — `cs_surface_stream_new(stream, config,
NULL)` and `cs_surface_curses_new(window, NULL)` use the default. To pick or
inspect one:

```c
#include "curspan.h"

cs_theme_t theme = cs_theme_default();          // "amber", env overrides applied
cs_theme_by_name("mono", &theme);               // false if the name is unknown
const char *const *names = cs_theme_names();     // NULL-terminated: "amber", "mono"

cs_surface_t *s = cs_surface_stream_new(stdout, config, &theme); // theme is copied
```

- `cs_theme_default()` returns the amber identity with `APP_CLI_ACCENT` applied
  and the mode resolved from the environment. This is what an app gets by default
  and what every built-in component is tuned against.
- `cs_theme_by_name(name, &out)` resolves a built-in theme; it returns `false`
  and leaves `*out` untouched when the name is unknown.
- The surface **copies** the theme you pass, so a stack `cs_theme_t` is fine.

Built-in themes:

| Name | Identity |
| --- | --- |
| `amber` | the default amber-on-near-black palette (full-color) |
| `mono` | grayscale — every role mapped to a 16-color index, for a no-accent look |

## Overriding roles

Re-theme one slot without forking a theme. Both overrides apply to dark and light
mode.

```c
cs_theme_t theme = cs_theme_default();

// Programmatic: supply a parsed color.
cs_theme_set_role(&theme, CS_ROLE_ACCENT, /* app_ui_color_t */ my_color);

// From a string: "#rrggbb", "rrggbb", or a decimal ANSI index. Returns false
// (leaving the theme unchanged) on a malformed spec.
cs_theme_set_role_spec(&theme, CS_ROLE_ACCENT, "#7dd3fc");
cs_theme_set_role_spec(&theme, CS_ROLE_BORDER, "8");
```

`cs_theme_set_role` is the general form of the accent override — any role, not
just the accent.

## Light and dark mode

Each role holds a `{dark, light}` pair; the theme's `mode` selects which is used.
Mode parsing has a single owner:

```c
cs_mode_t mode = cs_theme_mode_resolve();  // reads APP_CLI_TEST_THEME / APP_CLI_THEME
// CS_MODE_DARK or CS_MODE_LIGHT; "auto" and anything unknown resolve to dark.
```

`cs_theme_default()` / `cs_theme_by_name()` already call this for you. Set
`theme.mode` directly if you want to force a mode regardless of the environment.

## Color degradation

A theme stores ideal colors; the **surface** resolves each role to what the
terminal can actually show. Resolution is one shared function
(`app_ui_color_resolve`) that both the stream and curses backends call, so a
role degrades identically everywhere:

```c
app_ui_resolved_color_t c =
    cs_theme_resolve(&theme, CS_ROLE_TITLE, profile, color_count);
// c.kind is NONE | DEFAULT | INDEXED (c.index) | RGB (c.rgb)
```

By profile:

| Profile | An RGB token becomes | An explicit palette index becomes |
| --- | --- | --- |
| truecolor | RGB (24-bit) | INDEXED, passed through unchanged |
| 256-color | nearest xterm-256 index | INDEXED |
| 16-color | the role's 16-color hint, else nearest ANSI-16 | INDEXED (bright folded to base on true 8-color terminals) |
| none | no color | no color |

An *explicit* palette index (from `cs_theme_set_role_spec(&t, role, "200")` or an
all-indexed theme like `mono`) resolves to that index on every profile that can
render one — including truecolor, where a 24-bit terminal renders palette indices
fine. (Without this, an all-indexed theme would be colorless on the common
truecolor profile.)

## Environment contract

Curspan honors the same color/theme environment variables as the styled CLI;
they are part of the supported [contract](CONTRACTS.md).

| Variable | Effect |
| --- | --- |
| `APP_CLI_THEME` | `auto` \| `dark` \| `light` — the light/dark mode (`auto`/unknown => dark) |
| `APP_CLI_ACCENT` | `#rrggbb` or a decimal ANSI index — overrides the accent role |
| `APP_CLI_TEST_THEME` | takes precedence over `APP_CLI_THEME` (used by tests) |

Standard color-policy variables (`NO_COLOR`, `FORCE_COLOR`, `CLICOLOR(_FORCE)`,
`APP_CLI_COLOR`) flow through the `app_config_t` you hand to
`cs_surface_stream_new`, so a surface respects them without per-component work.

## See also

- [COMPONENTS.md](COMPONENTS.md) — the components that consume these roles
- [ARCHITECTURE.md](ARCHITECTURE.md#the-framework-layer) — where theming sits
- [CONTRACTS.md](CONTRACTS.md) — the stability tier of the theming surface
