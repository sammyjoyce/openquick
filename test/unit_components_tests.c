/*
 * Unit tests for the Curspan component catalog.
 *
 * Most components are rendered to a non-TTY tmpfile() stream surface, which
 * means styling is disabled and the output is plain (escape-free). The
 * invariants we pin are the ones a downstream app depends on: the right text
 * appears, layout is stable, and a component NEVER emits an escape sequence
 * onto a pipe unless styling is explicitly forced for a color regression test.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/components/components.h"
#include "../src/style/cs_theme.h"
#include "../src/surface/surface.h"
#include "../src/ui/text_layout.h"
#include "unit_support.h"

// Render through `s`, then return the captured bytes in `buf`. The surface is
// read before being freed so the test never depends on teardown order.
static size_t capture(cs_surface_t *s, FILE *stream, char *buf, size_t cap) {
  fflush(stream);
  rewind(stream);
  size_t n = fread(buf, 1, cap - 1, stream);
  buf[n] = '\0';
  cs_surface_free(s);
  fclose(stream);
  return n;
}

static cs_surface_t *open_capture(FILE **stream_out) {
  FILE *stream = tmpfile();
  if (!stream) {
    return NULL;
  }
  *stream_out = stream;
  return cs_surface_stream_new(stream, NULL, NULL);
}

static bool no_escapes(const char *buf) {
  return strstr(buf, "\x1b") == NULL;
}

typedef struct test_env_save {
  const char *name;
  char *value;
} test_env_save_t;

static bool rows_within_width(const char *buf, int width) {
  const char *line = buf;
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    if (app_text_width_utf8_n(line, len) > width) {
      return false;
    }
    if (!end) {
      break;
    }
    line = end + 1;
  }
  return true;
}

#ifdef APP_ENABLE_CLI_STYLE
static cs_surface_t *open_capture_theme(FILE **stream_out,
                                        const cs_theme_t *theme) {
  FILE *stream = tmpfile();
  if (!stream) {
    return NULL;
  }
  *stream_out = stream;
  return cs_surface_stream_new(stream, NULL, theme);
}
#endif

static bool test_surface_caps_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_caps_t caps = cs_surface_caps(s);
  bool ok = !caps.color && caps.width > 0 && !caps.tty;
  char buf[64];
  capture(s, stream, buf, sizeof(buf));
  return ok;
}

static bool test_surface_caps_ascii_locale(void) {
  test_env_save_t env[] = {
      {"LC_ALL", NULL}, {"LC_CTYPE", NULL}, {"LANG", NULL}};
  bool ok = true;
  for (size_t i = 0; i < sizeof(env) / sizeof(env[0]); i++) {
    const char *value = getenv(env[i].name);
    if (value) {
      size_t len = strlen(value) + 1;
      env[i].value = malloc(len);
      if (!env[i].value) {
        ok = false;
      } else {
        memcpy(env[i].value, value, len);
      }
    }
  }

  setenv("LC_ALL", "C", 1);
  unsetenv("LC_CTYPE");
  unsetenv("LANG");

  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  ok = ok && s != NULL;
  if (s) {
    cs_caps_t caps = cs_surface_caps(s);
    ok = ok && !caps.unicode;
    cs_rule_render(&(cs_rule_t){.width = 3}, s);
    char buf[64];
    capture(s, stream, buf, sizeof(buf));
    ok = ok && strcmp(buf, "---\n") == 0;
  }

  for (size_t i = 0; i < sizeof(env) / sizeof(env[0]); i++) {
    if (env[i].value) {
      setenv(env[i].name, env[i].value, 1);
      free(env[i].value);
    } else {
      unsetenv(env[i].name);
    }
  }
  return ok;
}

static bool test_rule_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_rule_render(&(cs_rule_t){.label = "OPTIONS", .width = 20, .glyph = "-"},
                 s);
  char buf[256];
  capture(s, stream, buf, sizeof(buf));
  return strstr(buf, "OPTIONS") && strstr(buf, "-") &&
         buf[strlen(buf) - 1] == '\n' && no_escapes(buf);
}

static bool test_heading_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_heading_render(
      &(cs_heading_t){.text = "Usage", .uppercase = true, .underline = true},
      s);
  char buf[256];
  capture(s, stream, buf, sizeof(buf));
  return strstr(buf, "USAGE") && no_escapes(buf);
}

static bool test_badge_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_badge_render(&(cs_badge_t){.text = "OK", .variant = CS_BADGE_SUCCESS}, s);
  char buf[128];
  capture(s, stream, buf, sizeof(buf));
  return strstr(buf, "[") && strstr(buf, "OK") && strstr(buf, "]") &&
         no_escapes(buf);
}

static bool test_note_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_note_render(&(cs_note_t){.variant = CS_NOTE_WARNING,
                              .title = "Heads up",
                              .body = "This cannot be undone",
                              .width = 40},
                 s);
  char buf[512];
  capture(s, stream, buf, sizeof(buf));
  return strstr(buf, "Heads up") && strstr(buf, "This cannot be undone") &&
         no_escapes(buf);
}

static bool test_keyvalue_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_keyvalue_pair_t pairs[] = {
      {"Application", "myapp"},
      {"Version", "0.1.0"},
  };
  cs_keyvalue_render(&(cs_keyvalue_t){.pairs = pairs, .count = 2}, s);
  char buf[256];
  capture(s, stream, buf, sizeof(buf));
  // Keys pad to the widest ("Application" = 11). "Application" then takes only
  // the 2-space separator; "Version" (7) gets 4 pad + 2 separator = 6 spaces.
  return strstr(buf, "Application  myapp") &&
         strstr(buf, "Version      0.1.0") && no_escapes(buf);
}

static bool test_list_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  const char *items[] = {"Alpha", "Beta"};
  cs_list_render(
      &(cs_list_t){.items = items, .count = 2, .style = CS_LIST_NUMBERED}, s);
  char buf[256];
  capture(s, stream, buf, sizeof(buf));
  return strstr(buf, "1. Alpha") && strstr(buf, "2. Beta") && no_escapes(buf);
}

static bool test_table_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_table_column_t cols[] = {{.header = "Name"}, {.header = "Status"}};
  const char *cells[] = {"build", "ok", "tests", "ok"};
  cs_table_render(&(cs_table_t){.columns = cols,
                                .column_count = 2,
                                .cells = cells,
                                .row_count = 2,
                                .header = true,
                                .width = 40},
                  s);
  char buf[512];
  capture(s, stream, buf, sizeof(buf));
  return strstr(buf, "Name") && strstr(buf, "Status") && strstr(buf, "build") &&
         strstr(buf, "tests") && no_escapes(buf);
}

static bool test_progress_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_progress_render(&(cs_progress_t){.label = "Building",
                                      .current = 7,
                                      .total = 10,
                                      .width = 40,
                                      .show_percent = true},
                     s);
  char buf[256];
  capture(s, stream, buf, sizeof(buf));
  return strstr(buf, "Building") && strstr(buf, "70%") && no_escapes(buf);
}

static bool test_spinner_plain(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_spinner_render(&(cs_spinner_t){.label = "Working"}, 0, s);
  char buf[128];
  capture(s, stream, buf, sizeof(buf));
  bool frames_ok = cs_spinner_frame_count(CS_SPINNER_LINE, false) == 4 &&
                   cs_spinner_frame_count(CS_SPINNER_DOTS, true) == 10;
  return strstr(buf, "Working") && no_escapes(buf) && frames_ok;
}

static bool test_theme_registry(void) {
  cs_theme_t theme;
  bool ok =
      cs_theme_by_name("amber", &theme) && strcmp(theme.name, "amber") == 0;
  ok =
      ok && cs_theme_by_name("mono", &theme) && strcmp(theme.name, "mono") == 0;
  ok = ok && !cs_theme_by_name("does-not-exist", &theme);
  cs_theme_t def = cs_theme_default();
  ok = ok && def.name && strcmp(def.name, "amber") == 0;
  const char *const *names = cs_theme_names();
  bool saw_amber = false, saw_mono = false;
  for (size_t i = 0; names[i]; i++) {
    if (strcmp(names[i], "amber") == 0) {
      saw_amber = true;
    }
    if (strcmp(names[i], "mono") == 0) {
      saw_mono = true;
    }
  }
  return ok && saw_amber && saw_mono;
}

// A non-finite progress fraction must not hang or emit garbage. (Regression: a
// NaN escaped the [0,1] clamp, making the bar-fill repeat counts wrap huge.)
static bool test_progress_nan_is_safe(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_progress_render(
      &(cs_progress_t){
          .value = NAN, .total = 0, .width = 20, .show_percent = true},
      s);
  char buf[256];
  capture(s, stream, buf, sizeof(buf));
  // NaN folds to 0%, and the call returns promptly (no hang).
  return strstr(buf, "0%") && no_escapes(buf);
}

// A table with row_count > 0 but cells == NULL must not dereference NULL.
// (Regression: the natural-width scan loaded through the NULL cells pointer.)
static bool test_table_null_cells_is_safe(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_table_column_t cols[] = {{.header = "Name"}, {.header = "Status"}};
  cs_table_render(&(cs_table_t){.columns = cols,
                                .column_count = 2,
                                .cells = NULL,
                                .row_count = 3,
                                .header = true,
                                .width = 40},
                  s);
  char buf[64];
  capture(s, stream, buf, sizeof(buf));
  // Invalid prop combo is rejected (no output), and nothing crashes.
  return buf[0] == '\0';
}

// Even when there are more columns than can fit with one visible cell per
// column, table rendering must honor the caller's total width budget rather
// than raising the budget and smearing into neighboring UI.
static bool test_table_respects_narrow_width_budget(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  cs_table_column_t cols[] = {
      {.header = "Alpha"}, {.header = "Beta"}, {.header = "Gamma"}};
  const char *cells[] = {"one", "two", "three"};
  const int width = 2;
  cs_table_render(&(cs_table_t){.columns = cols,
                                .column_count = 3,
                                .cells = cells,
                                .row_count = 1,
                                .header = true,
                                .width = (size_t)width},
                  s);
  char buf[512];
  capture(s, stream, buf, sizeof(buf));
  return rows_within_width(buf, width) && no_escapes(buf);
}

// The all-indexed "mono" theme must still resolve to concrete colors on a
// truecolor terminal. (Regression: explicit indices collapsed to NONE there.)
static bool test_mono_theme_colored_on_truecolor(void) {
  cs_theme_t mono;
  if (!cs_theme_by_name("mono", &mono)) {
    return false;
  }
  app_ui_resolved_color_t text = cs_theme_resolve(
      &mono, CS_ROLE_TEXT, APP_CLI_COLOR_PROFILE_TRUECOLOR, 256);
  app_ui_resolved_color_t border = cs_theme_resolve(
      &mono, CS_ROLE_BORDER, APP_CLI_COLOR_PROFILE_TRUECOLOR, 256);
  return text.kind == APP_UI_RESOLVED_INDEXED &&
         border.kind == APP_UI_RESOLVED_INDEXED;
}

static bool test_explicit_text_role_overrides_non_text_default(void) {
#ifdef APP_ENABLE_CLI_STYLE
  test_env_save_t env[] = {{"APP_CLI_TEST_PROFILE", NULL}, {"NO_COLOR", NULL},
                           {"APP_CLI_COLOR", NULL},        {"TERM", NULL},
                           {"FORCE_COLOR", NULL},          {"CLICOLOR", NULL},
                           {"CLICOLOR_FORCE", NULL}};
  bool ok = true;
  for (size_t i = 0; i < sizeof(env) / sizeof(env[0]); i++) {
    const char *value = getenv(env[i].name);
    if (value) {
      size_t len = strlen(value) + 1;
      env[i].value = malloc(len);
      if (!env[i].value) {
        ok = false;
      } else {
        memcpy(env[i].value, value, len);
      }
    }
  }

  unsetenv("NO_COLOR");
  unsetenv("APP_CLI_COLOR");
  unsetenv("FORCE_COLOR");
  unsetenv("CLICOLOR");
  unsetenv("CLICOLOR_FORCE");
  setenv("TERM", "xterm-256color", 1);
  setenv("APP_CLI_TEST_PROFILE", "truecolor", 1);

  cs_theme_t theme = cs_theme_default();
  ok = ok && cs_theme_set_role_spec(&theme, CS_ROLE_TEXT, "#010203") &&
       cs_theme_set_role_spec(&theme, CS_ROLE_MUTED, "#040506") &&
       cs_theme_set_role_spec(&theme, CS_ROLE_BORDER, "#070809");

  FILE *stream = NULL;
  cs_surface_t *s = open_capture_theme(&stream, &theme);
  if (!s) {
    ok = false;
  } else {
    cs_keyvalue_pair_t pairs[] = {{"Key", "Value"}};
    cs_keyvalue_render(&(cs_keyvalue_t){.pairs = pairs,
                                        .count = 1,
                                        .key_role = CS_ROLE_TEXT,
                                        .key_role_set = true},
                       s);
    char buf[256];
    capture(s, stream, buf, sizeof(buf));
    ok = ok && strstr(buf, "\x1b[38;2;1;2;3mKey") != NULL &&
         strstr(buf, "\x1b[38;2;4;5;6mKey") == NULL;
  }

  stream = NULL;
  s = open_capture_theme(&stream, &theme);
  if (!s) {
    ok = false;
  } else {
    cs_heading_render(&(cs_heading_t){.text = "Head",
                                      .underline = true,
                                      .role = CS_ROLE_TEXT,
                                      .role_set = true},
                      s);
    char buf[512];
    capture(s, stream, buf, sizeof(buf));
    ok = ok && strstr(buf, "\x1b[38;2;1;2;3m") != NULL &&
         strstr(buf, "Head") != NULL && strstr(buf, "\x1b[38;2;7;8;9m") == NULL;
  }

  for (size_t i = 0; i < sizeof(env) / sizeof(env[0]); i++) {
    if (env[i].value) {
      setenv(env[i].name, env[i].value, 1);
      free(env[i].value);
    } else {
      unsetenv(env[i].name);
    }
  }
  return ok;
#else
  return true;
#endif
}

// A label that nearly fills the budget must be truncated, not allowed to push
// the rendered line past `width`. (Regression: the label and percent were
// written untruncated even after the bar was clamped to its 1-column floor, so
// a 20-column budget rendered 31 columns.)
static bool test_progress_respects_width_budget(void) {
  FILE *stream = NULL;
  cs_surface_t *s = open_capture(&stream);
  if (!s) {
    return false;
  }
  const int width = 20;
  cs_progress_render(&(cs_progress_t){.label = "A very long build label",
                                      .current = 10,
                                      .total = 10,
                                      .width = (size_t)width,
                                      .show_percent = true},
                     s);
  char buf[256];
  capture(s, stream, buf, sizeof(buf));
  size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') {
    buf[len - 1] = '\0';  // measure the row, not the trailing newline
  }
  return app_text_width_utf8(buf) <= width && no_escapes(buf);
}

void run_components_unit_tests(unit_stats_t *stats) {
  unit_record(stats, test_surface_caps_plain(),
              "surface reports plain caps on a non-tty stream");
  unit_record(stats, test_surface_caps_ascii_locale(),
              "surface reports ASCII glyph fallback under C locale");
  unit_record(stats, test_rule_plain(), "cs_rule renders a labelled divider");
  unit_record(stats, test_heading_plain(),
              "cs_heading upper-cases and renders a title");
  unit_record(stats, test_badge_plain(), "cs_badge renders a bracketed label");
  unit_record(stats, test_note_plain(),
              "cs_note renders title and wrapped body");
  unit_record(stats, test_keyvalue_plain(),
              "cs_keyvalue aligns keys to a column");
  unit_record(stats, test_list_plain(), "cs_list numbers items");
  unit_record(stats, test_table_plain(), "cs_table renders headers and rows");
  unit_record(stats, test_progress_plain(),
              "cs_progress renders a bar and percentage");
  unit_record(stats, test_spinner_plain(), "cs_spinner renders a frame");
  unit_record(stats, test_theme_registry(),
              "cs_theme resolves named themes and the default");
  unit_record(stats, test_progress_nan_is_safe(),
              "cs_progress tolerates a non-finite fraction");
  unit_record(stats, test_progress_respects_width_budget(),
              "cs_progress truncates a long label to its width budget");
  unit_record(stats, test_table_null_cells_is_safe(),
              "cs_table rejects row_count>0 with NULL cells");
  unit_record(stats, test_table_respects_narrow_width_budget(),
              "cs_table respects narrow width budgets");
  unit_record(stats, test_mono_theme_colored_on_truecolor(),
              "mono theme stays colored on a truecolor profile");
  unit_record(stats, test_explicit_text_role_overrides_non_text_default(),
              "explicit CS_ROLE_TEXT overrides non-text role defaults");
}
