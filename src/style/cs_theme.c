/*
 * cs_theme — public theming API. See cs_theme.h.
 */

#include "cs_theme.h"

#include <stdlib.h>
#include <string.h>

#ifdef APP_ENABLE_CLI_STYLE
#include "../cli/style/cli_term.h"
#endif

cs_mode_t cs_theme_mode_resolve(void) {
  const char *theme = getenv("APP_CLI_TEST_THEME");
  if (!theme || !theme[0]) {
    theme = getenv("APP_CLI_THEME");
  }
  if (theme && strcmp(theme, "light") == 0) {
    return CS_MODE_LIGHT;
  }
  if (theme && strcmp(theme, "dark") == 0) {
    return CS_MODE_DARK;
  }
#ifdef APP_ENABLE_CLI_STYLE
  // Match the styled CLI resolver: unset, "auto", and unknown values query OSC
  // 11 when policy allows it, then fall back to dark when detection cannot run.
  if (app_cli_term_detect_background(NULL, NULL) == APP_CLI_BG_LIGHT) {
    return CS_MODE_LIGHT;
  }
#endif
  return CS_MODE_DARK;
}

void cs_theme_set_role(cs_theme_t *theme, cs_role_t role,
                       app_ui_color_t color) {
  if (!theme || role < 0 || role >= APP_UI_ROLE_COUNT) {
    return;
  }
  theme->scheme.roles[role].dark = color;
  theme->scheme.roles[role].light = color;
}

bool cs_theme_set_role_spec(cs_theme_t *theme, cs_role_t role,
                            const char *spec) {
  app_ui_color_t color;
  if (!theme || !app_ui_color_parse(spec, &color)) {
    return false;
  }
  cs_theme_set_role(theme, role, color);
  return true;
}

app_ui_resolved_color_t cs_theme_resolve(const cs_theme_t *theme,
                                         cs_role_t role,
                                         app_cli_color_profile_id profile,
                                         int color_count) {
  if (!theme) {
    return (app_ui_resolved_color_t){.kind = APP_UI_RESOLVED_NONE};
  }
  return app_ui_theme_resolve(&theme->scheme, role, theme->mode, profile,
                              color_count);
}

// ---- Built-in themes ------------------------------------------------------

// "mono": a hueless theme. Every role degrades to white / gray / black so the
// same components render cleanly on monochrome or e-ink-style terminals. Built
// by reclassifying the default role table rather than hand-coding 26 entries,
// so it tracks any future role additions.
static app_ui_color_t mono_ansi16(uint8_t index) {
  return (app_ui_color_t){.kind = APP_UI_COLOR_ANSI16,
                          .ansi16 = index,
                          .has_ansi16_hint = true,
                          .ansi16_hint = index};
}

static void cs_theme_make_mono(app_ui_color_scheme_t *scheme) {
  for (int role = 0; role < APP_UI_ROLE_COUNT; role++) {
    uint8_t fg = 7;  // white by default
    switch (role) {
    case APP_UI_ROLE_MUTED:
    case APP_UI_ROLE_COMMENT:
    case APP_UI_ROLE_FLAG_DEFAULT:
    case APP_UI_ROLE_DASH:
    case APP_UI_ROLE_BORDER:
    case APP_UI_ROLE_PANEL:
    case APP_UI_ROLE_PANEL_ALT:
      fg = 8;  // bright black (gray)
      break;
    case APP_UI_ROLE_ERROR_HEADER_FG:
    case APP_UI_ROLE_SELECTION_FG:
      fg = 0;  // black text for the inverted bars below
      break;
    case APP_UI_ROLE_ERROR_HEADER_BG:
    case APP_UI_ROLE_SELECTION_BG:
      fg = 7;  // white bar
      break;
    case APP_UI_ROLE_TITLE:
    case APP_UI_ROLE_PROGRAM:
    case APP_UI_ROLE_ACCENT:
    case APP_UI_ROLE_HELP:
      fg = 15;  // bright white for emphasis
      break;
    default:
      fg = 7;
      break;
    }
    app_ui_color_t c = mono_ansi16(fg);
    scheme->roles[role].dark = c;
    scheme->roles[role].light = c;
  }
}

// Build a named scheme. Returns false for unknown names.
static bool cs_theme_build_scheme(const char *name,
                                  app_ui_color_scheme_t *out) {
  if (strcmp(name, "amber") == 0) {
    *out = *app_ui_theme_default_scheme();
    return true;
  }
  if (strcmp(name, "mono") == 0) {
    *out = *app_ui_theme_default_scheme();
    cs_theme_make_mono(out);
    return true;
  }
  return false;
}

// Apply the shared env overrides (APP_CLI_ACCENT) to a built scheme.
static void cs_theme_apply_env(app_ui_color_scheme_t *scheme) {
  app_ui_theme_apply_env_overrides(scheme);
}

bool cs_theme_by_name(const char *name, cs_theme_t *out) {
  if (!name || !out) {
    return false;
  }
  app_ui_color_scheme_t scheme;
  if (!cs_theme_build_scheme(name, &scheme)) {
    return false;
  }
  cs_theme_apply_env(&scheme);
  // Match the built-in name to its stable string literal for theme->name.
  const char *const *names = cs_theme_names();
  const char *canonical = "custom";
  for (size_t i = 0; names[i]; i++) {
    if (strcmp(names[i], name) == 0) {
      canonical = names[i];
      break;
    }
  }
  *out = (cs_theme_t){
      .scheme = scheme, .mode = cs_theme_mode_resolve(), .name = canonical};
  return true;
}

cs_theme_t cs_theme_default(void) {
  cs_theme_t theme;
  // "amber" always exists; fall back to a zeroed theme defensively.
  if (!cs_theme_by_name("amber", &theme)) {
    theme = (cs_theme_t){.scheme = *app_ui_theme_default_scheme(),
                         .mode = cs_theme_mode_resolve(),
                         .name = "amber"};
  }
  return theme;
}

const char *const *cs_theme_names(void) {
  static const char *const names[] = {"amber", "mono", NULL};
  return names;
}
