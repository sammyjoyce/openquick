/*
 * CLI color theme: Fang-style semantic tokens compiled into ready-to-emit
 * styles for a concrete color profile and light/dark mode.
 *
 * Two stages, mirroring Fang's ColorScheme -> Styles:
 *   1. app_cli_color_scheme_t  — semantic tokens, each an adaptive {dark,light}
 *      pair of RGB colors with optional ANSI fallback hints.
 *   2. app_cli_styles_t        — those tokens resolved to indexed/RGB colors +
 *      attributes for the detected terminal.
 *
 * The default scheme's tokens are projected from the shared semantic UI theme
 * (style/ui_theme.h) so the CLI and TUI share role meaning instead of
 * hand-maintained duplicate palettes. The scheme degrades to ANSI-256/ANSI-16
 * per the detected profile (each token carries a semantic ANSI-16 fallback
 * hint), and callers may apply per-token overrides.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../../style/color_math.h"
#include "cli_term.h"

// How a token color is specified before resolution.
typedef enum app_cli_color_kind_id {
  APP_CLI_COLOR_UNSET = 0,  // token absent: emit nothing
  APP_CLI_COLOR_RGB,        // RGB authoritative, degrade per profile
  APP_CLI_COLOR_ANSI256,    // explicit 256-index
  APP_CLI_COLOR_ANSI16,     // explicit 16-index
  APP_CLI_COLOR_DEFAULT,    // terminal default fg/bg
} app_cli_color_kind_id;

typedef struct app_cli_color {
  app_cli_color_kind_id kind;
  app_rgb_t rgb;
  uint8_t ansi256;
  uint8_t ansi16;
  // Semantic fallback hint so e.g. amber degrades to yellow, not nearest gray.
  bool has_ansi16_hint;
  uint8_t ansi16_hint;
} app_cli_color_t;

typedef struct app_cli_adaptive_color {
  app_cli_color_t dark;
  app_cli_color_t light;
} app_cli_adaptive_color_t;

// Semantic tokens, mirroring Fang's ColorScheme. (upper, field, label)
#define APP_CLI_COLOR_TOKEN_LIST(X)                     \
  X(BASE, base, "Base")                                 \
  X(TITLE, title, "Title")                              \
  X(DESCRIPTION, description, "Description")            \
  X(CODEBLOCK, codeblock, "Codeblock")                  \
  X(PROGRAM, program, "Program")                        \
  X(DIMMED_ARGUMENT, dimmed_argument, "DimmedArgument") \
  X(COMMENT, comment, "Comment")                        \
  X(FLAG, flag, "Flag")                                 \
  X(FLAG_DEFAULT, flag_default, "FlagDefault")          \
  X(COMMAND, command, "Command")                        \
  X(QUOTED_STRING, quoted_string, "QuotedString")       \
  X(ARGUMENT, argument, "Argument")                     \
  X(HELP, help, "Help")                                 \
  X(DASH, dash, "Dash")                                 \
  X(ERROR_HEADER_FG, error_header_fg, "ErrorHeaderFg")  \
  X(ERROR_HEADER_BG, error_header_bg, "ErrorHeaderBg")  \
  X(ERROR_DETAILS, error_details, "ErrorDetails")

typedef enum app_cli_color_token_id {
#define APP_CLI_TOKEN_ENUM(upper, field, label) APP_CLI_COLOR_TOKEN_##upper,
  APP_CLI_COLOR_TOKEN_LIST(APP_CLI_TOKEN_ENUM)
#undef APP_CLI_TOKEN_ENUM
      APP_CLI_COLOR_TOKEN_COUNT,
} app_cli_color_token_id;

typedef struct app_cli_color_scheme {
  app_cli_adaptive_color_t tokens[APP_CLI_COLOR_TOKEN_COUNT];
} app_cli_color_scheme_t;

typedef enum app_cli_theme_mode_id {
  APP_CLI_THEME_MODE_DARK = 0,
  APP_CLI_THEME_MODE_LIGHT,
} app_cli_theme_mode_id;

// A resolved color, ready to emit.
typedef enum app_cli_resolved_color_kind_id {
  APP_CLI_RESOLVED_COLOR_NONE = 0,
  APP_CLI_RESOLVED_COLOR_INDEXED,
  APP_CLI_RESOLVED_COLOR_RGB,
  APP_CLI_RESOLVED_COLOR_DEFAULT,
} app_cli_resolved_color_kind_id;

typedef struct app_cli_resolved_color {
  app_cli_resolved_color_kind_id kind;
  uint16_t index;
  app_rgb_t rgb;
} app_cli_resolved_color_t;

typedef struct app_cli_style {
  app_cli_resolved_color_t fg;
  app_cli_resolved_color_t bg;
  app_cli_attr_mask_t attrs;
} app_cli_style_t;

typedef struct app_cli_styles {
  app_cli_style_t tokens[APP_CLI_COLOR_TOKEN_COUNT];
  app_cli_style_t error_header;  // ERROR_HEADER_FG on ERROR_HEADER_BG, bold
} app_cli_styles_t;

// Built-in default scheme.
const app_cli_color_scheme_t *app_cli_theme_default_scheme(void);

// Parse a color spec into an app_cli_color_t. Accepts "#rrggbb"/"rrggbb" (RGB,
// with an auto-derived ANSI-16 fallback hint) or a decimal 0-255 ANSI index.
// Returns false on malformed input.
bool app_cli_color_parse(const char *spec, app_cli_color_t *out);

// Apply environment-based theme overrides to a mutable scheme copy. Currently
// honors APP_CLI_ACCENT, which recolors the accent tokens (title, program,
// command, flag, help, codeblock). Safe to call on a copy of a built-in scheme.
void app_cli_theme_apply_env_overrides(app_cli_color_scheme_t *scheme);

// Compile a scheme into emit-ready styles for the given mode/profile.
void app_cli_styles_compile(app_cli_styles_t *out,
                            const app_cli_color_scheme_t *scheme,
                            app_cli_theme_mode_id mode,
                            app_cli_color_profile_id profile, int color_count);

// Accessor for a token style.
const app_cli_style_t *app_cli_style(const app_cli_styles_t *styles,
                                     app_cli_color_token_id token);
