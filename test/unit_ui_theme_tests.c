/*
 * Unit tests for the shared semantic UI theme.
 */

#include <stdlib.h>
#include <string.h>

#include "../src/style/design_tokens.h"
#include "../src/style/ui_theme.h"
#include "unit_support.h"

static char *ui_env_dup(const char *name) {
  const char *value = getenv(name);
  if (!value) {
    return NULL;
  }
  const size_t len = strlen(value) + 1;
  char *copy = malloc(len);
  if (copy) {
    memcpy(copy, value, len);
  }
  return copy;
}

static void ui_env_restore(const char *name, char *value) {
  if (value) {
    setenv(name, value, 1);
    free(value);
  } else {
    unsetenv(name);
  }
}

static bool rgb_eq(app_rgb_t a, app_rgb_t b) {
  return a.r == b.r && a.g == b.g && a.b == b.b;
}

static bool test_default_dark_roles_use_design_palette(void) {
  const app_ui_color_scheme_t *scheme = app_ui_theme_default_scheme();
  const app_ui_color_t title =
      app_ui_theme_pick(scheme, APP_UI_ROLE_TITLE, APP_UI_THEME_MODE_DARK);
  const app_ui_color_t border =
      app_ui_theme_pick(scheme, APP_UI_ROLE_BORDER, APP_UI_THEME_MODE_DARK);
  const app_ui_color_t error = app_ui_theme_pick(
      scheme, APP_UI_ROLE_ERROR_DETAILS, APP_UI_THEME_MODE_DARK);
  return title.kind == APP_UI_COLOR_RGB &&
         rgb_eq(title.rgb, APP_DESIGN_PALETTE.amber) &&
         border.kind == APP_UI_COLOR_RGB &&
         rgb_eq(border.rgb, APP_DESIGN_PALETTE.muted) &&
         border.has_ansi16_hint && border.ansi16_hint == 7 &&
         error.kind == APP_UI_COLOR_RGB &&
         rgb_eq(error.rgb, APP_DESIGN_PALETTE.red);
}

static bool test_color_parse_matches_cli_contract(void) {
  app_ui_color_t color;
  bool ok = app_ui_color_parse("#3aa0ff", &color) &&
            color.kind == APP_UI_COLOR_RGB && color.rgb.r == 0x3a &&
            color.rgb.g == 0xa0 && color.rgb.b == 0xff && color.has_ansi16_hint;
  ok = ok && app_ui_color_parse("ff8800", &color) &&
       color.kind == APP_UI_COLOR_RGB;
  ok = ok && app_ui_color_parse("12", &color) &&
       color.kind == APP_UI_COLOR_ANSI16 && color.ansi16 == 12;
  ok = ok && app_ui_color_parse("200", &color) &&
       color.kind == APP_UI_COLOR_ANSI256 && color.ansi256 == 200;
  ok = ok && !app_ui_color_parse("#ggg", &color) &&
       !app_ui_color_parse("300", &color) && !app_ui_color_parse("", &color);
  return ok;
}

static bool test_accent_override_updates_shared_roles(void) {
  char *previous = ui_env_dup("APP_CLI_ACCENT");
  bool ok = setenv("APP_CLI_ACCENT", "#102030", 1) == 0;
  app_ui_color_scheme_t scheme = *app_ui_theme_default_scheme();
  app_ui_theme_apply_env_overrides(&scheme);

  const app_ui_color_t title =
      app_ui_theme_pick(&scheme, APP_UI_ROLE_TITLE, APP_UI_THEME_MODE_DARK);
  const app_ui_color_t accent =
      app_ui_theme_pick(&scheme, APP_UI_ROLE_ACCENT, APP_UI_THEME_MODE_DARK);
  const app_ui_color_t selection = app_ui_theme_pick(
      &scheme, APP_UI_ROLE_SELECTION_BG, APP_UI_THEME_MODE_DARK);
  ok = ok && title.kind == APP_UI_COLOR_RGB && title.rgb.r == 0x10 &&
       title.rgb.g == 0x20 && title.rgb.b == 0x30 &&
       accent.kind == APP_UI_COLOR_RGB && accent.rgb.r == 0x10 &&
       selection.kind == APP_UI_COLOR_RGB && selection.rgb.b == 0x30;

  ui_env_restore("APP_CLI_ACCENT", previous);
  return ok;
}

static bool test_light_title_preserves_cli_literal(void) {
  const app_ui_color_t title =
      app_ui_theme_pick(app_ui_theme_default_scheme(), APP_UI_ROLE_TITLE,
                        APP_UI_THEME_MODE_LIGHT);
  return title.kind == APP_UI_COLOR_RGB && title.rgb.r == 135 &&
         title.rgb.g == 94 && title.rgb.b == 20;
}

// An explicit palette index must survive a truecolor profile as INDEXED, not
// collapse to NONE — otherwise an all-indexed theme (e.g. "mono") renders
// colorless on the common truecolor terminal. (Regression: the resolver
// previously emitted color under TRUECOLOR only for RGB inputs.)
static bool test_indexed_color_survives_truecolor(void) {
  const app_ui_color_t ansi16 = {.kind = APP_UI_COLOR_ANSI16, .ansi16 = 7};
  const app_ui_color_t ansi256 = {.kind = APP_UI_COLOR_ANSI256, .ansi256 = 200};
  const app_ui_color_t rgb = {.kind = APP_UI_COLOR_RGB,
                              .rgb = {.r = 0x10, .g = 0x20, .b = 0x30}};

  app_ui_resolved_color_t r16 =
      app_ui_color_resolve(ansi16, APP_CLI_COLOR_PROFILE_TRUECOLOR, 256);
  app_ui_resolved_color_t r256 =
      app_ui_color_resolve(ansi256, APP_CLI_COLOR_PROFILE_TRUECOLOR, 256);
  app_ui_resolved_color_t rrgb =
      app_ui_color_resolve(rgb, APP_CLI_COLOR_PROFILE_TRUECOLOR, 256);
  // Lower profiles already handled indices; confirm they still do.
  app_ui_resolved_color_t r16_at_256 =
      app_ui_color_resolve(ansi16, APP_CLI_COLOR_PROFILE_ANSI256, 256);

  return r16.kind == APP_UI_RESOLVED_INDEXED && r16.index == 7 &&
         r256.kind == APP_UI_RESOLVED_INDEXED && r256.index == 200 &&
         rrgb.kind == APP_UI_RESOLVED_RGB && rrgb.rgb.r == 0x10 &&
         r16_at_256.kind == APP_UI_RESOLVED_INDEXED && r16_at_256.index == 7;
}

void run_ui_theme_unit_tests(unit_stats_t *stats) {
  unit_record(stats, test_default_dark_roles_use_design_palette(),
              "ui theme dark roles derive from design palette");
  unit_record(stats, test_color_parse_matches_cli_contract(),
              "ui color parser supports hex and ANSI indexes");
  unit_record(stats, test_accent_override_updates_shared_roles(),
              "APP_CLI_ACCENT recolors shared accent roles");
  unit_record(stats, test_light_title_preserves_cli_literal(),
              "ui theme light title preserves existing CLI literal");
  unit_record(stats, test_indexed_color_survives_truecolor(),
              "ui resolver passes explicit palette indices through truecolor");
}
