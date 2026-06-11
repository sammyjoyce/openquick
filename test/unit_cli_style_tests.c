/*
 * Unit tests for the CLI styling layer: color math, theme compilation, layout
 * width, and the plain error fallback. These exercise pure logic without a
 * terminal (the ANSI backend is linked, and memory streams are non-TTY so
 * styling is disabled).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cli/style/cli_error_render.h"
#include "../src/cli/style/cli_layout.h"
#include "../src/cli/style/cli_theme.h"
#include "../src/style/color_math.h"
#include "unit_support.h"

static const app_rgb_t AMBER = {200, 175, 130};

static char *copy_env(const char *name) {
  const char *value = getenv(name);
  if (!value) {
    return NULL;
  }
  size_t len = strlen(value) + 1;
  char *copy = malloc(len);
  if (copy) {
    memcpy(copy, value, len);
  }
  return copy;
}

static void restore_env(const char *name, char *value) {
  if (value) {
    setenv(name, value, 1);
    free(value);
  } else {
    unsetenv(name);
  }
}

static bool test_luma_light_dark(void) {
  return app_color_is_light((app_rgb_t){255, 255, 255}) &&
         !app_color_is_light((app_rgb_t){0, 0, 0}) &&
         app_color_luma8((app_rgb_t){0, 0, 0}) == 0;
}

static bool test_channel_to_u8(void) {
  return app_color_channel_to_u8(0xffff, 0xffff) == 255 &&
         app_color_channel_to_u8(0, 0xffff) == 0 &&
         app_color_channel_to_u8(0x8000, 0xffff) == 128 &&
         app_color_channel_to_u8(5, 0) == 0;
}

static bool test_rgb_to_xterm256(void) {
  return app_color_rgb_to_xterm256((app_rgb_t){0, 0, 0}) == 16 &&
         app_color_rgb_to_xterm256((app_rgb_t){255, 255, 255}) == 231 &&
         app_color_rgb_to_xterm256(AMBER) == 180;
}

static bool test_rgb_to_ansi16(void) {
  return app_color_rgb_to_ansi16((app_rgb_t){0, 0, 0}) == 0 &&
         app_color_rgb_to_ansi16((app_rgb_t){255, 255, 255}) == 15 &&
         app_color_rgb_to_ansi16((app_rgb_t){255, 0, 0}) == 9;
}

// Compile the default scheme and inspect the resolved Title token (amber).
static bool test_theme_truecolor_keeps_rgb(void) {
  app_cli_styles_t styles;
  app_cli_styles_compile(&styles, app_cli_theme_default_scheme(),
                         APP_CLI_THEME_MODE_DARK,
                         APP_CLI_COLOR_PROFILE_TRUECOLOR, 256);
  const app_cli_style_t *title =
      app_cli_style(&styles, APP_CLI_COLOR_TOKEN_TITLE);
  return title->fg.kind == APP_CLI_RESOLVED_COLOR_RGB &&
         title->fg.rgb.r == AMBER.r && title->fg.rgb.g == AMBER.g &&
         title->fg.rgb.b == AMBER.b && (title->attrs & APP_CLI_ATTR_BOLD);
}

static bool test_theme_256_downsamples(void) {
  app_cli_styles_t styles;
  app_cli_styles_compile(&styles, app_cli_theme_default_scheme(),
                         APP_CLI_THEME_MODE_DARK, APP_CLI_COLOR_PROFILE_ANSI256,
                         256);
  const app_cli_style_t *title =
      app_cli_style(&styles, APP_CLI_COLOR_TOKEN_TITLE);
  return title->fg.kind == APP_CLI_RESOLVED_COLOR_INDEXED &&
         title->fg.index == 180;
}

static bool test_theme_16_uses_hint_and_folds(void) {
  app_cli_styles_t styles16;
  app_cli_styles_compile(&styles16, app_cli_theme_default_scheme(),
                         APP_CLI_THEME_MODE_DARK, APP_CLI_COLOR_PROFILE_ANSI16,
                         16);
  const app_cli_style_t *t16 =
      app_cli_style(&styles16, APP_CLI_COLOR_TOKEN_TITLE);
  // Title hint is bright yellow (11) on a 16-color terminal.
  bool ok =
      t16->fg.kind == APP_CLI_RESOLVED_COLOR_INDEXED && t16->fg.index == 11;

  app_cli_styles_t styles8;
  app_cli_styles_compile(&styles8, app_cli_theme_default_scheme(),
                         APP_CLI_THEME_MODE_DARK, APP_CLI_COLOR_PROFILE_ANSI16,
                         8);
  const app_cli_style_t *t8 =
      app_cli_style(&styles8, APP_CLI_COLOR_TOKEN_TITLE);
  // On a true 8-color terminal the bright index folds onto the base (11-8=3).
  ok = ok && t8->fg.index == 3;
  return ok;
}

static bool test_text_width_utf8(void) {
  // "café" = c a f + 2-byte é => 4 display columns, 5 bytes.
  return app_cli_text_width("hello") == 5 &&
         app_cli_text_width("caf\xc3\xa9") == 4 &&
         app_cli_text_width_n("hello", 3) == 3;
}

// The plain error fallback (non-TTY memory stream) title-cases the first word
// and appends the usage hint.
static bool test_error_plain_fallback(void) {
  // Capture via tmpfile() rather than open_memstream(): the latter is POSIX
  // only and absent on Windows. tmpfile() is C-standard and still a non-TTY,
  // so the plain (escape-free) fallback path is exercised the same way.
  FILE *stream = tmpfile();
  if (!stream) {
    return false;
  }
  app_cli_render_error(NULL, stream, "myapp", "invalid option '--x'",
                       APP_CLI_ERROR_KIND_USAGE);
  fflush(stream);
  rewind(stream);

  char buf[1024];
  const size_t n = fread(buf, 1, sizeof(buf) - 1, stream);
  buf[n] = '\0';
  fclose(stream);

  return strstr(buf, "Error: Invalid option '--x'") != NULL &&
         strstr(buf, "Try myapp --help for usage.") != NULL &&
         strstr(buf, "\x1b") == NULL; /* no escapes on a non-TTY */
}

static bool test_color_parse(void) {
  app_cli_color_t c;
  bool ok = app_cli_color_parse("#3aa0ff", &c) && c.kind == APP_CLI_COLOR_RGB &&
            c.rgb.r == 0x3a && c.rgb.g == 0xa0 && c.rgb.b == 0xff &&
            c.has_ansi16_hint;
  ok = ok && app_cli_color_parse("ff8800", &c) && c.kind == APP_CLI_COLOR_RGB;
  ok = ok && app_cli_color_parse("12", &c) && c.kind == APP_CLI_COLOR_ANSI16 &&
       c.ansi16 == 12;
  ok = ok && app_cli_color_parse("200", &c) &&
       c.kind == APP_CLI_COLOR_ANSI256 && c.ansi256 == 200;
  ok = ok && !app_cli_color_parse("#ggg", &c) &&
       !app_cli_color_parse("300", &c) && !app_cli_color_parse("", &c);
  return ok;
}

static bool test_accent_override(void) {
  char *previous = copy_env("APP_CLI_ACCENT");
  bool env_ok = setenv("APP_CLI_ACCENT", "#102030", 1) == 0;
  app_cli_color_scheme_t scheme = *app_cli_theme_default_scheme();
  app_cli_theme_apply_env_overrides(&scheme);
  restore_env("APP_CLI_ACCENT", previous);

  const app_cli_color_t *title = &scheme.tokens[APP_CLI_COLOR_TOKEN_TITLE].dark;
  return env_ok && title->kind == APP_CLI_COLOR_RGB && title->rgb.r == 0x10 &&
         title->rgb.g == 0x20 && title->rgb.b == 0x30;
}

static bool test_color_env_forces_profile_and_never_wins(void) {
  char *previous_color = copy_env("APP_CLI_COLOR");
  char *previous_no_color = copy_env("NO_COLOR");
  char *previous_term = copy_env("TERM");
  unsetenv("NO_COLOR");
  setenv("TERM", "xterm-256color", 1);

  FILE *stream = tmpfile();
  if (!stream) {
    restore_env("APP_CLI_COLOR", previous_color);
    restore_env("NO_COLOR", previous_no_color);
    restore_env("TERM", previous_term);
    return false;
  }

  bool ok = setenv("APP_CLI_COLOR", "truecolor", 1) == 0;
  app_cli_term_t term;
  ok = ok && app_cli_term_init(&term, stream, NULL, NULL) &&
       term.style_enabled && term.profile == APP_CLI_COLOR_PROFILE_TRUECOLOR;
  app_cli_term_deinit(&term);

  app_cli_term_opts_t opts = {.force_profile = "truecolor"};
  ok = ok && setenv("APP_CLI_COLOR", "never", 1) == 0 &&
       !app_cli_term_init(&term, stream, NULL, &opts) && !term.style_enabled;
  app_cli_term_deinit(&term);
  fclose(stream);

  restore_env("APP_CLI_COLOR", previous_color);
  restore_env("NO_COLOR", previous_no_color);
  restore_env("TERM", previous_term);
  return ok;
}

static bool test_theme_env_light_and_accent_integration(void) {
  char *previous_theme = copy_env("APP_CLI_THEME");
  char *previous_test_theme = copy_env("APP_CLI_TEST_THEME");
  char *previous_accent = copy_env("APP_CLI_ACCENT");
  char *previous_color = copy_env("APP_CLI_COLOR");
  char *previous_no_color = copy_env("NO_COLOR");
  char *previous_term = copy_env("TERM");
  unsetenv("APP_CLI_TEST_THEME");
  unsetenv("APP_CLI_COLOR");
  unsetenv("NO_COLOR");
  setenv("TERM", "xterm-256color", 1);

  FILE *stream = tmpfile();
  if (!stream) {
    restore_env("APP_CLI_THEME", previous_theme);
    restore_env("APP_CLI_TEST_THEME", previous_test_theme);
    restore_env("APP_CLI_ACCENT", previous_accent);
    restore_env("APP_CLI_COLOR", previous_color);
    restore_env("NO_COLOR", previous_no_color);
    restore_env("TERM", previous_term);
    return false;
  }

  app_cli_term_opts_t opts = {.force_profile = "truecolor"};
  app_cli_render_ctx_t ctx;
  bool ok = setenv("APP_CLI_THEME", "light", 1) == 0 &&
            unsetenv("APP_CLI_ACCENT") == 0 &&
            app_cli_render_ctx_init(&ctx, NULL, stream, "myapp", &opts);
  const app_cli_style_t *title =
      app_cli_style(&ctx.styles, APP_CLI_COLOR_TOKEN_TITLE);
  ok = ok && title->fg.kind == APP_CLI_RESOLVED_COLOR_RGB &&
       title->fg.rgb.r == 135 && title->fg.rgb.g == 94 && title->fg.rgb.b == 20;
  app_cli_render_ctx_deinit(&ctx);

  rewind(stream);
  ok = ok && setenv("APP_CLI_ACCENT", "#102030", 1) == 0 &&
       app_cli_render_ctx_init(&ctx, NULL, stream, "myapp", &opts);
  title = app_cli_style(&ctx.styles, APP_CLI_COLOR_TOKEN_TITLE);
  ok = ok && title->fg.kind == APP_CLI_RESOLVED_COLOR_RGB &&
       title->fg.rgb.r == 0x10 && title->fg.rgb.g == 0x20 &&
       title->fg.rgb.b == 0x30;
  app_cli_render_ctx_deinit(&ctx);
  fclose(stream);

  restore_env("APP_CLI_THEME", previous_theme);
  restore_env("APP_CLI_TEST_THEME", previous_test_theme);
  restore_env("APP_CLI_ACCENT", previous_accent);
  restore_env("APP_CLI_COLOR", previous_color);
  restore_env("NO_COLOR", previous_no_color);
  restore_env("TERM", previous_term);
  return ok;
}

void run_cli_style_unit_tests(unit_stats_t *stats) {
  unit_record(stats, test_luma_light_dark(), "color luma light/dark threshold");
  unit_record(stats, test_channel_to_u8(), "color channel 16->8 bit");
  unit_record(stats, test_rgb_to_xterm256(), "rgb -> xterm256 cube/gray");
  unit_record(stats, test_rgb_to_ansi16(), "rgb -> ansi16 nearest");
  unit_record(stats, test_theme_truecolor_keeps_rgb(),
              "theme truecolor keeps RGB + bold");
  unit_record(stats, test_theme_256_downsamples(),
              "theme 256 downsamples amber to 180");
  unit_record(stats, test_theme_16_uses_hint_and_folds(),
              "theme 16 uses semantic hint and folds bright->base");
  unit_record(stats, test_text_width_utf8(), "layout utf8 display width");
  unit_record(stats, test_error_plain_fallback(),
              "error plain fallback title-cases + usage hint");
  unit_record(stats, test_color_parse(), "color spec parse (hex/index)");
  unit_record(stats, test_accent_override(),
              "APP_CLI_ACCENT recolors accent tokens");
  unit_record(stats, test_color_env_forces_profile_and_never_wins(),
              "APP_CLI_COLOR forces profile and never wins");
  unit_record(stats, test_theme_env_light_and_accent_integration(),
              "APP_CLI_THEME light mode and accent override integrate");
}
