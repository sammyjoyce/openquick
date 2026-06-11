/*
 * Shared semantic terminal UI theme.
 *
 * design_tokens.* owns raw RGB palette values. This module gives those values
 * names that both renderers can agree on: CLI SGR tokens and ncurses color
 * pairs map to these roles, then degrade through their own terminal backends.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "color_math.h"

typedef enum app_ui_color_kind_id {
  APP_UI_COLOR_UNSET = 0,
  APP_UI_COLOR_RGB,
  APP_UI_COLOR_ANSI256,
  APP_UI_COLOR_ANSI16,
  APP_UI_COLOR_DEFAULT,
} app_ui_color_kind_id;

typedef struct app_ui_color {
  app_ui_color_kind_id kind;
  app_rgb_t rgb;
  uint8_t ansi256;
  uint8_t ansi16;
  /* Semantic ANSI-16 fallback hint so amber degrades to yellow, not gray. */
  bool has_ansi16_hint;
  uint8_t ansi16_hint;
} app_ui_color_t;

typedef struct app_ui_adaptive_color {
  app_ui_color_t dark;
  app_ui_color_t light;
} app_ui_adaptive_color_t;

typedef enum app_ui_theme_mode_id {
  APP_UI_THEME_MODE_DARK = 0,
  APP_UI_THEME_MODE_LIGHT,
} app_ui_theme_mode_id;

#define APP_UI_ROLE_LIST(X)                            \
  X(TEXT, text, "Text")                                \
  X(TITLE, title, "Title")                             \
  X(DESCRIPTION, description, "Description")           \
  X(CODE, code, "Code")                                \
  X(PROGRAM, program, "Program")                       \
  X(MUTED, muted, "Muted")                             \
  X(ACCENT, accent, "Accent")                          \
  X(COMMENT, comment, "Comment")                       \
  X(FLAG, flag, "Flag")                                \
  X(FLAG_DEFAULT, flag_default, "FlagDefault")         \
  X(COMMAND, command, "Command")                       \
  X(QUOTED_STRING, quoted_string, "QuotedString")      \
  X(ARGUMENT, argument, "Argument")                    \
  X(HELP, help, "Help")                                \
  X(DASH, dash, "Dash")                                \
  X(ERROR_HEADER_FG, error_header_fg, "ErrorHeaderFg") \
  X(ERROR_HEADER_BG, error_header_bg, "ErrorHeaderBg") \
  X(ERROR_DETAILS, error_details, "ErrorDetails")      \
  X(SUCCESS, success, "Success")                       \
  X(WARNING, warning, "Warning")                       \
  X(INFO, info, "Info")                                \
  X(BORDER, border, "Border")                          \
  X(SELECTION_FG, selection_fg, "SelectionFg")         \
  X(SELECTION_BG, selection_bg, "SelectionBg")         \
  X(PANEL, panel, "Panel")                             \
  X(PANEL_ALT, panel_alt, "PanelAlt")

typedef enum app_ui_role_id {
#define APP_UI_ROLE_ENUM(upper, field, label) APP_UI_ROLE_##upper,
  APP_UI_ROLE_LIST(APP_UI_ROLE_ENUM)
#undef APP_UI_ROLE_ENUM
      APP_UI_ROLE_COUNT,
} app_ui_role_id;

typedef struct app_ui_color_scheme {
  app_ui_adaptive_color_t roles[APP_UI_ROLE_COUNT];
} app_ui_color_scheme_t;

const app_ui_color_scheme_t *app_ui_theme_default_scheme(void);

app_ui_color_t app_ui_theme_pick(const app_ui_color_scheme_t *scheme,
                                 app_ui_role_id role,
                                 app_ui_theme_mode_id mode);

/* A semantic color resolved against a concrete terminal color profile, ready to
 * realize as an SGR sequence (stream surface) or a curses color. This is the
 * single shared degradation result that both render surfaces and the CLI
 * styling layer agree on. */
typedef enum app_ui_resolved_kind_id {
  APP_UI_RESOLVED_NONE = 0, /* emit nothing (token absent) */
  APP_UI_RESOLVED_INDEXED,  /* ANSI index 0..255 */
  APP_UI_RESOLVED_RGB,      /* truecolor */
  APP_UI_RESOLVED_DEFAULT,  /* terminal default fg/bg */
} app_ui_resolved_kind_id;

typedef struct app_ui_resolved_color {
  app_ui_resolved_kind_id kind;
  uint16_t index;
  app_rgb_t rgb;
} app_ui_resolved_color_t;

/* Degrade one semantic color to a concrete profile. This is THE shared
 * resolver: truecolor keeps RGB; ANSI256 downsamples through the xterm-256
 * cube; ANSI16 prefers each color's semantic ansi16 hint and folds bright
 * indices onto the base 8 when the terminal only has 8 colors. The CLI styling
 * layer (app_cli_styles_compile) delegates here so the stream surface, the
 * curses surface, and the help/error renderers all degrade identically. */
app_ui_resolved_color_t app_ui_color_resolve(app_ui_color_t color,
                                             app_cli_color_profile_id profile,
                                             int color_count);

/* Resolve a role from a scheme for a mode + profile in one call. */
app_ui_resolved_color_t app_ui_theme_resolve(
    const app_ui_color_scheme_t *scheme, app_ui_role_id role,
    app_ui_theme_mode_id mode, app_cli_color_profile_id profile,
    int color_count);

/* Parse "#rrggbb"/"rrggbb" RGB or decimal ANSI palette index 0..255. */
bool app_ui_color_parse(const char *spec, app_ui_color_t *out);

/* Apply a concrete accent to every role that represents the product accent. */
void app_ui_theme_apply_accent(app_ui_color_scheme_t *scheme,
                               app_ui_color_t accent);

/* Apply environment overrides. Currently honors APP_CLI_ACCENT for both the
 * styled CLI and generated TUI; the name is kept for compatibility. */
void app_ui_theme_apply_env_overrides(app_ui_color_scheme_t *scheme);
