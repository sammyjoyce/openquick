/*
 * CLI theme: the default scheme and scheme->styles compilation. See
 * cli_theme.h.
 */

#include "cli_theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../../style/ui_theme.h"

// ANSI-16 indices used as hints: 1 red, 2 green, 3 yellow, 7 white,
// 8 bright-black (gray), 11 bright-yellow, 15 bright-white.

static app_cli_color_kind_id app_cli_color_kind_from_ui(
    app_ui_color_kind_id kind) {
  switch (kind) {
  case APP_UI_COLOR_RGB:
    return APP_CLI_COLOR_RGB;
  case APP_UI_COLOR_ANSI256:
    return APP_CLI_COLOR_ANSI256;
  case APP_UI_COLOR_ANSI16:
    return APP_CLI_COLOR_ANSI16;
  case APP_UI_COLOR_DEFAULT:
    return APP_CLI_COLOR_DEFAULT;
  case APP_UI_COLOR_UNSET:
  default:
    return APP_CLI_COLOR_UNSET;
  }
}

static app_cli_color_t app_cli_color_from_ui(app_ui_color_t color) {
  return (app_cli_color_t){.kind = app_cli_color_kind_from_ui(color.kind),
                           .rgb = color.rgb,
                           .ansi256 = color.ansi256,
                           .ansi16 = color.ansi16,
                           .has_ansi16_hint = color.has_ansi16_hint,
                           .ansi16_hint = color.ansi16_hint};
}

static app_cli_adaptive_color_t app_cli_token_from_role(
    const app_ui_color_scheme_t *scheme, app_ui_role_id role) {
  return (app_cli_adaptive_color_t){
      .dark = app_cli_color_from_ui(scheme->roles[role].dark),
      .light = app_cli_color_from_ui(scheme->roles[role].light),
  };
}

// Default scheme: CLI tokens are an adapter over the shared semantic UI roles
// in src/style/ui_theme.c. The CLI keeps Fang-style token names for renderer
// readability, while the actual color meaning is shared with the ncurses TUI.
static app_cli_color_scheme_t APP_CLI_DEFAULT_SCHEME;
static bool APP_CLI_DEFAULT_SCHEME_READY;

static void app_cli_theme_init_default_scheme(void) {
  if (APP_CLI_DEFAULT_SCHEME_READY) {
    return;
  }

  const app_ui_color_scheme_t *ui = app_ui_theme_default_scheme();
  APP_CLI_DEFAULT_SCHEME = (app_cli_color_scheme_t){
      .tokens =
          {
              [APP_CLI_COLOR_TOKEN_BASE] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_TEXT),
              [APP_CLI_COLOR_TOKEN_TITLE] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_TITLE),
              [APP_CLI_COLOR_TOKEN_DESCRIPTION] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_DESCRIPTION),
              [APP_CLI_COLOR_TOKEN_CODEBLOCK] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_CODE),
              [APP_CLI_COLOR_TOKEN_PROGRAM] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_PROGRAM),
              [APP_CLI_COLOR_TOKEN_DIMMED_ARGUMENT] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_MUTED),
              [APP_CLI_COLOR_TOKEN_COMMENT] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_COMMENT),
              [APP_CLI_COLOR_TOKEN_FLAG] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_FLAG),
              [APP_CLI_COLOR_TOKEN_FLAG_DEFAULT] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_FLAG_DEFAULT),
              [APP_CLI_COLOR_TOKEN_COMMAND] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_COMMAND),
              [APP_CLI_COLOR_TOKEN_QUOTED_STRING] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_QUOTED_STRING),
              [APP_CLI_COLOR_TOKEN_ARGUMENT] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_ARGUMENT),
              [APP_CLI_COLOR_TOKEN_HELP] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_HELP),
              [APP_CLI_COLOR_TOKEN_DASH] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_DASH),
              [APP_CLI_COLOR_TOKEN_ERROR_HEADER_FG] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_ERROR_HEADER_FG),
              [APP_CLI_COLOR_TOKEN_ERROR_HEADER_BG] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_ERROR_HEADER_BG),
              [APP_CLI_COLOR_TOKEN_ERROR_DETAILS] =
                  app_cli_token_from_role(ui, APP_UI_ROLE_ERROR_DETAILS),
          },
  };
  APP_CLI_DEFAULT_SCHEME_READY = true;
}

const app_cli_color_scheme_t *app_cli_theme_default_scheme(void) {
  app_cli_theme_init_default_scheme();
  return &APP_CLI_DEFAULT_SCHEME;
}

// Default attributes per token.
static app_cli_attr_mask_t app_cli_token_attrs(app_cli_color_token_id token) {
  switch (token) {
  case APP_CLI_COLOR_TOKEN_TITLE:
  case APP_CLI_COLOR_TOKEN_PROGRAM:
  case APP_CLI_COLOR_TOKEN_HELP:
    return APP_CLI_ATTR_BOLD;
  case APP_CLI_COLOR_TOKEN_DIMMED_ARGUMENT:
  case APP_CLI_COLOR_TOKEN_COMMENT:
  case APP_CLI_COLOR_TOKEN_FLAG_DEFAULT:
  case APP_CLI_COLOR_TOKEN_DASH:
    return APP_CLI_ATTR_DIM;
  default:
    return APP_CLI_ATTR_NONE;
  }
}

// CLI tokens and the shared UI roles use structurally identical color types;
// resolution is the one shared degradation policy in style/ui_theme.c. Convert
// to the UI color, delegate, and map the result back so the stream surface, the
// curses surface, and these help/error styles all degrade identically.
static app_ui_color_kind_id app_ui_color_kind_from_cli(
    app_cli_color_kind_id kind) {
  switch (kind) {
  case APP_CLI_COLOR_RGB:
    return APP_UI_COLOR_RGB;
  case APP_CLI_COLOR_ANSI256:
    return APP_UI_COLOR_ANSI256;
  case APP_CLI_COLOR_ANSI16:
    return APP_UI_COLOR_ANSI16;
  case APP_CLI_COLOR_DEFAULT:
    return APP_UI_COLOR_DEFAULT;
  case APP_CLI_COLOR_UNSET:
  default:
    return APP_UI_COLOR_UNSET;
  }
}

static app_cli_resolved_color_t app_cli_color_resolve(
    app_cli_color_t color, app_cli_color_profile_id profile, int color_count) {
  app_ui_color_t ui = {.kind = app_ui_color_kind_from_cli(color.kind),
                       .rgb = color.rgb,
                       .ansi256 = color.ansi256,
                       .ansi16 = color.ansi16,
                       .has_ansi16_hint = color.has_ansi16_hint,
                       .ansi16_hint = color.ansi16_hint};
  app_ui_resolved_color_t r = app_ui_color_resolve(ui, profile, color_count);
  switch (r.kind) {
  case APP_UI_RESOLVED_INDEXED:
    return (app_cli_resolved_color_t){.kind = APP_CLI_RESOLVED_COLOR_INDEXED,
                                      .index = r.index};
  case APP_UI_RESOLVED_RGB:
    return (app_cli_resolved_color_t){.kind = APP_CLI_RESOLVED_COLOR_RGB,
                                      .rgb = r.rgb};
  case APP_UI_RESOLVED_DEFAULT:
    return (app_cli_resolved_color_t){.kind = APP_CLI_RESOLVED_COLOR_DEFAULT};
  case APP_UI_RESOLVED_NONE:
  default:
    return (app_cli_resolved_color_t){.kind = APP_CLI_RESOLVED_COLOR_NONE};
  }
}

static app_cli_color_t app_cli_pick(const app_cli_adaptive_color_t *adaptive,
                                    app_cli_theme_mode_id mode) {
  return mode == APP_CLI_THEME_MODE_LIGHT ? adaptive->light : adaptive->dark;
}

void app_cli_styles_compile(app_cli_styles_t *out,
                            const app_cli_color_scheme_t *scheme,
                            app_cli_theme_mode_id mode,
                            app_cli_color_profile_id profile, int color_count) {
  if (!out) {
    return;
  }
  *out = (app_cli_styles_t){0};
  if (!scheme) {
    return;
  }

  for (int i = 0; i < APP_CLI_COLOR_TOKEN_COUNT; i++) {
    app_cli_color_t color = app_cli_pick(&scheme->tokens[i], mode);
    out->tokens[i].fg = app_cli_color_resolve(color, profile, color_count);
    out->tokens[i].attrs = app_cli_token_attrs((app_cli_color_token_id)i);
  }

  // Compose the error header: header fg on header bg, bold.
  out->error_header.fg = app_cli_color_resolve(
      app_cli_pick(&scheme->tokens[APP_CLI_COLOR_TOKEN_ERROR_HEADER_FG], mode),
      profile, color_count);
  out->error_header.bg = app_cli_color_resolve(
      app_cli_pick(&scheme->tokens[APP_CLI_COLOR_TOKEN_ERROR_HEADER_BG], mode),
      profile, color_count);
  out->error_header.attrs = APP_CLI_ATTR_BOLD;
}

const app_cli_style_t *app_cli_style(const app_cli_styles_t *styles,
                                     app_cli_color_token_id token) {
  static const app_cli_style_t empty = {0};
  if (!styles || token < 0 || token >= APP_CLI_COLOR_TOKEN_COUNT) {
    return &empty;
  }
  return &styles->tokens[token];
}

bool app_cli_color_parse(const char *spec, app_cli_color_t *out) {
  if (!out) {
    return false;
  }
  app_ui_color_t parsed;
  if (!app_ui_color_parse(spec, &parsed)) {
    return false;
  }
  *out = app_cli_color_from_ui(parsed);
  return true;
}

void app_cli_theme_apply_env_overrides(app_cli_color_scheme_t *scheme) {
  if (!scheme) {
    return;
  }
  const char *accent = getenv("APP_CLI_ACCENT");
  if (!accent || !accent[0]) {
    return;
  }
  app_cli_color_t color;
  if (!app_cli_color_parse(accent, &color)) {
    return;
  }
  static const app_cli_color_token_id accent_tokens[] = {
      APP_CLI_COLOR_TOKEN_TITLE,   APP_CLI_COLOR_TOKEN_PROGRAM,
      APP_CLI_COLOR_TOKEN_COMMAND, APP_CLI_COLOR_TOKEN_FLAG,
      APP_CLI_COLOR_TOKEN_HELP,    APP_CLI_COLOR_TOKEN_CODEBLOCK,
  };
  for (size_t i = 0; i < sizeof(accent_tokens) / sizeof(accent_tokens[0]);
       i++) {
    scheme->tokens[accent_tokens[i]].dark = color;
    scheme->tokens[accent_tokens[i]].light = color;
  }
}
