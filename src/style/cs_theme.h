/*
 * cs_theme — Curspan's public theming API.
 *
 * Curspan's design system is three layers (see docs/THEMING.md):
 *   1. design tokens   — raw sRGB palette values (design_tokens.h)
 *   2. semantic roles  — named, adaptive {dark,light} colors (ui_theme.h)
 *   3. themes          — a named, overridable bundle of roles + a mode
 *
 * A cs_theme_t is the value components and surfaces consume. Built-in themes
 * are resolvable by name; any role can be overridden; the active light/dark
 * mode is resolved from the environment. This is the terminal-UI analogue of a
 * ShadCN CSS-variable theme: every component styles itself through these roles,
 * never through hard-coded colors, so re-theming an app is a one-line change.
 *
 * This is an additive public surface over the existing app_ui_* internals; the
 * default theme is the same amber-on-near-black identity the CLI and TUI
 * already share, and the APP_CLI_THEME / APP_CLI_ACCENT environment contract is
 * honored.
 */

#pragma once

#include <stdbool.h>

#include "ui_theme.h"

// A role is a semantic slot in the theme. Curspan exposes the shared UI roles
// under render-neutral, component-centric aliases so component code reads as
// cs_surface_set_role(s, CS_ROLE_PRIMARY) rather than naming help-text tokens.
typedef app_ui_role_id cs_role_t;
typedef app_ui_theme_mode_id cs_mode_t;

#define CS_ROLE_TEXT APP_UI_ROLE_TEXT
#define CS_ROLE_MUTED APP_UI_ROLE_MUTED
#define CS_ROLE_PRIMARY APP_UI_ROLE_ACCENT
#define CS_ROLE_ACCENT APP_UI_ROLE_ACCENT
#define CS_ROLE_TITLE APP_UI_ROLE_TITLE
#define CS_ROLE_HEADING APP_UI_ROLE_TITLE
#define CS_ROLE_CODE APP_UI_ROLE_CODE
#define CS_ROLE_SUCCESS APP_UI_ROLE_SUCCESS
#define CS_ROLE_WARNING APP_UI_ROLE_WARNING
#define CS_ROLE_ERROR APP_UI_ROLE_ERROR_DETAILS
#define CS_ROLE_INFO APP_UI_ROLE_INFO
#define CS_ROLE_BORDER APP_UI_ROLE_BORDER
#define CS_ROLE_SELECTION_FG APP_UI_ROLE_SELECTION_FG
#define CS_ROLE_SELECTION_BG APP_UI_ROLE_SELECTION_BG
#define CS_ROLE_PANEL APP_UI_ROLE_PANEL

#define CS_MODE_DARK APP_UI_THEME_MODE_DARK
#define CS_MODE_LIGHT APP_UI_THEME_MODE_LIGHT

// Optional role fields in component props are zero-initialized for their
// component-specific defaults, but CS_ROLE_TEXT is also enum value zero. Pair a
// role field with a `*_role_set` flag when CS_ROLE_TEXT must be selected
// explicitly where the default is another role.
static inline bool cs_role_valid(cs_role_t role) {
  return role >= 0 && role < APP_UI_ROLE_COUNT;
}

static inline cs_role_t cs_role_or_default(cs_role_t role, bool role_set,
                                           cs_role_t default_role) {
  if (role_set || role != CS_ROLE_TEXT) {
    return cs_role_valid(role) ? role : default_role;
  }
  return default_role;
}

typedef struct cs_theme {
  app_ui_color_scheme_t scheme;  // the role table (by value, ~copyable)
  cs_mode_t mode;                // active light/dark mode
  const char *name;              // built-in name, or "custom"
} cs_theme_t;

// The default theme ("amber"): the shared amber-on-near-black identity, with
// APP_CLI_ACCENT applied and the mode resolved from APP_CLI_THEME. This is what
// an app gets by default and what every built-in component is tuned against.
cs_theme_t cs_theme_default(void);

// Resolve a built-in theme by name. Returns false (leaving *out untouched) when
// the name is unknown. The result has env overrides applied and mode resolved,
// exactly like cs_theme_default().
bool cs_theme_by_name(const char *name, cs_theme_t *out);

// NULL-terminated list of built-in theme names, for `theme` listings and docs.
const char *const *cs_theme_names(void);

// Resolve the active mode from APP_CLI_TEST_THEME / APP_CLI_THEME
// (auto|dark|light). "auto" and anything unknown use the same OSC 11
// background detection as the styled CLI when available, then fall back to
// dark.
cs_mode_t cs_theme_mode_resolve(void);

// Override one role for both modes (the general form of the accent override).
void cs_theme_set_role(cs_theme_t *theme, cs_role_t role, app_ui_color_t color);

// Parse "#rrggbb" / "rrggbb" / decimal ANSI index and override a role. Returns
// false on a malformed spec (leaving the theme unchanged).
bool cs_theme_set_role_spec(cs_theme_t *theme, cs_role_t role,
                            const char *spec);

// Resolve a role in this theme to a concrete color for a terminal profile.
app_ui_resolved_color_t cs_theme_resolve(const cs_theme_t *theme,
                                         cs_role_t role,
                                         app_cli_color_profile_id profile,
                                         int color_count);
