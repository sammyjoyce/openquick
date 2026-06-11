/*
 * Unit tests for core config, error, color, and memory helpers.
 */

#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "../src/core/config.h"
#include "../src/core/config_json.h"
#include "../src/core/error.h"
#include "../src/core/request_json.h"
#include "../src/utils/colors.h"
#include "../src/utils/memory.h"
#include "unit_support.h"

#ifndef _WIN32
static bool test_config_env_no_color_empty_sets_flag(void) {
  const char *previous = getenv("NO_COLOR");
  char *previous_copy = previous ? strdup(previous) : NULL;
  if (previous && !previous_copy) {
    return false;
  }

  bool ok = setenv("NO_COLOR", "", 1) == 0;
  app_config_t *config = NULL;
  if (ok) {
    ok = app_config_create(&config) == APP_SUCCESS;
  }
  if (ok) {
    ok = app_config_load_env(config) == APP_SUCCESS &&
         app_config_is_no_color(config);
  }

  app_config_destroy(config);
  if (previous_copy) {
    (void)setenv("NO_COLOR", previous_copy, 1);
  } else {
    (void)unsetenv("NO_COLOR");
  }
  free(previous_copy);
  return ok;
}

static bool test_use_colors_honors_no_color_without_env_load(void) {
  const char *previous = getenv("NO_COLOR");
  char *previous_copy = previous ? strdup(previous) : NULL;
  if (previous && !previous_copy) {
    return false;
  }

  bool ok = setenv("NO_COLOR", "", 1) == 0;
  app_config_t *config = NULL;
  if (ok) {
    ok = app_config_create(&config) == APP_SUCCESS;
  }
  if (ok) {
    ok = !app_use_colors(config);
  }

  app_config_destroy(config);
  if (previous_copy) {
    (void)setenv("NO_COLOR", previous_copy, 1);
  } else {
    (void)unsetenv("NO_COLOR");
  }
  free(previous_copy);
  return ok;
}

static void cfg_env_set(const char *name, const char *value) {
  if (value) {
    (void)setenv(name, value, 1);
  } else {
    (void)unsetenv(name);
  }
}

static char *cfg_env_dup(const char *name) {
  const char *value = getenv(name);
  return value ? strdup(value) : NULL;
}

static bool test_color_env_force_resolves_conventions(void) {
  char *save_fc = cfg_env_dup("FORCE_COLOR");
  char *save_cf = cfg_env_dup("CLICOLOR_FORCE");
  char *save_cc = cfg_env_dup("CLICOLOR");

  cfg_env_set("FORCE_COLOR", NULL);
  cfg_env_set("CLICOLOR_FORCE", NULL);
  cfg_env_set("CLICOLOR", NULL);
  bool ok = app_color_env_force() == APP_COLOR_FORCE_AUTO;

  // FORCE_COLOR parsed as a level: 0/false off, anything else (incl. empty) on.
  cfg_env_set("FORCE_COLOR", "0");
  ok = ok && app_color_env_force() == APP_COLOR_FORCE_OFF;
  cfg_env_set("FORCE_COLOR", "false");
  ok = ok && app_color_env_force() == APP_COLOR_FORCE_OFF;
  cfg_env_set("FORCE_COLOR", "1");
  ok = ok && app_color_env_force() == APP_COLOR_FORCE_ON;
  cfg_env_set("FORCE_COLOR", "");
  ok = ok && app_color_env_force() == APP_COLOR_FORCE_ON;

  // FORCE_COLOR outranks CLICOLOR.
  cfg_env_set("CLICOLOR", "0");
  cfg_env_set("FORCE_COLOR", "1");
  ok = ok && app_color_env_force() == APP_COLOR_FORCE_ON;

  // CLICOLOR_FORCE forces on even with CLICOLOR=0; CLICOLOR_FORCE=0 is inert.
  cfg_env_set("FORCE_COLOR", NULL);
  cfg_env_set("CLICOLOR_FORCE", "1");
  ok = ok && app_color_env_force() == APP_COLOR_FORCE_ON;
  cfg_env_set("CLICOLOR_FORCE", "0");
  ok = ok && app_color_env_force() == APP_COLOR_FORCE_OFF;  // CLICOLOR=0 wins
  cfg_env_set("CLICOLOR", "1");
  ok = ok && app_color_env_force() == APP_COLOR_FORCE_AUTO;

  cfg_env_set("FORCE_COLOR", save_fc);
  cfg_env_set("CLICOLOR_FORCE", save_cf);
  cfg_env_set("CLICOLOR", save_cc);
  free(save_fc);
  free(save_cf);
  free(save_cc);
  return ok;
}

static bool test_use_colors_no_color_beats_force_color(void) {
  // NO_COLOR must win over FORCE_COLOR=1.
  char *save_nc = cfg_env_dup("NO_COLOR");
  char *save_fc = cfg_env_dup("FORCE_COLOR");

  cfg_env_set("NO_COLOR", "");
  cfg_env_set("FORCE_COLOR", "1");
  const bool ok = !app_use_colors(NULL);

  cfg_env_set("NO_COLOR", save_nc);
  cfg_env_set("FORCE_COLOR", save_fc);
  free(save_nc);
  free(save_fc);
  return ok;
}

static bool test_use_colors_honors_cli_color_never(void) {
  char *save_color = cfg_env_dup("APP_CLI_COLOR");
  char *save_fc = cfg_env_dup("FORCE_COLOR");
  char *save_nc = cfg_env_dup("NO_COLOR");

  cfg_env_set("NO_COLOR", NULL);
  cfg_env_set("FORCE_COLOR", "1");
  cfg_env_set("APP_CLI_COLOR", "never");
  const bool ok = !app_use_colors(NULL);

  cfg_env_set("APP_CLI_COLOR", save_color);
  cfg_env_set("FORCE_COLOR", save_fc);
  cfg_env_set("NO_COLOR", save_nc);
  free(save_color);
  free(save_fc);
  free(save_nc);
  return ok;
}
#endif

static bool test_strerror_covers_every_code(void) {
  size_t count = 0;
  const app_error_info_t *errors = app_error_table(&count);
  if (!errors || count == 0) {
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    const char *msg = app_strerror(errors[i].code);
    if (!msg || msg[0] == '\0') {
      fprintf(stderr, "app_strerror returned empty for code %d\n",
              errors[i].code);
      return false;
    }
    if (strcmp(msg, "Unknown error") == 0) {
      fprintf(stderr, "code %d hit the default branch\n", errors[i].code);
      return false;
    }
  }
  return true;
}

static bool test_config_json_parses_valid_input(void) {
  app_config_json_state_t state = {0};
  const char *input = "{\"debug\":true,\"quiet\":false}";
  if (app_config_parse_json_state(&state, input) != APP_SUCCESS) {
    return false;
  }
  return state.values[APP_FLAG_DEBUG] && !state.values[APP_FLAG_QUIET];
}

static bool test_config_json_rejects_unicode_escape(void) {
  app_config_json_state_t state = {0};
  const char *input = "{\"debug\":\"\\u00e9\"}";
  return app_config_parse_json_state(&state, input) != APP_SUCCESS;
}

static bool test_config_json_skips_nested_unknown_value(void) {
  // An unknown key whose value is a nested object/array must be skipped (not
  // rejected), and a sibling known flag must still apply.
  app_config_json_state_t state = {0};
  const char *input = "{\"ignored\":{\"a\":[1,{\"b\":2}]},\"quiet\":true}";
  return app_config_parse_json_state(&state, input) == APP_SUCCESS &&
         state.values[APP_FLAG_QUIET];
}

static bool test_config_json_caps_nested_depth(void) {
  // 64 levels of array nesting under an unknown key exceeds the 32-deep cap and
  // must fail cleanly rather than overflow the stack.
  char input[200];
  size_t pos = 0;
  const char *const prefix = "{\"x\":";
  memcpy(input + pos, prefix, strlen(prefix));
  pos += strlen(prefix);
  for (int i = 0; i < 64; i++) {
    input[pos++] = '[';
  }
  for (int i = 0; i < 64; i++) {
    input[pos++] = ']';
  }
  input[pos++] = '}';
  input[pos] = '\0';

  app_config_json_state_t state = {0};
  return app_config_parse_json_state(&state, input) == APP_ERROR_OUT_OF_RANGE;
}

static bool test_config_json_rejects_trailing_garbage(void) {
  app_config_json_state_t state = {0};
  const char *input = "{}garbage";
  return app_config_parse_json_state(&state, input) != APP_SUCCESS;
}

static bool test_config_json_rejects_long_keys(void) {
  char input[96];
  memset(input, 'a', sizeof(input));
  input[0] = '{';
  input[1] = '"';
  input[sizeof(input) - 8] = '"';
  input[sizeof(input) - 7] = ':';
  input[sizeof(input) - 6] = 't';
  input[sizeof(input) - 5] = 'r';
  input[sizeof(input) - 4] = 'u';
  input[sizeof(input) - 3] = 'e';
  input[sizeof(input) - 2] = '}';
  input[sizeof(input) - 1] = '\0';

  app_config_json_state_t state = {0};
  return app_config_parse_json_state(&state, input) == APP_ERROR_OUT_OF_RANGE;
}

static bool test_config_json_output_exclusivity(void) {
  app_config_json_state_t state = {0};
  const char *input = "{\"plain_output\":true,\"json_output\":true}";
  if (app_config_parse_json_state(&state, input) != APP_SUCCESS) {
    return false;
  }
  return state.values[APP_FLAG_JSON_OUTPUT] &&
         !state.values[APP_FLAG_PLAIN_OUTPUT];
}

static bool test_config_json_log_level_exclusivity(void) {
  app_config_json_state_t state = {0};
  const char *input = "{\"debug\":true,\"quiet\":true,\"verbose\":true}";
  if (app_config_parse_json_state(&state, input) != APP_SUCCESS) {
    return false;
  }
  return !state.values[APP_FLAG_DEBUG] && !state.values[APP_FLAG_QUIET] &&
         state.values[APP_FLAG_VERBOSE];
}

static bool test_config_setter_log_level_exclusivity(void) {
  app_config_t *config = NULL;
  if (app_config_create(&config) != APP_SUCCESS) {
    return false;
  }

  bool ok = app_config_set_debug(config, true) == APP_SUCCESS &&
            app_config_is_debug(config) && !app_config_is_quiet(config) &&
            !app_config_is_verbose(config);
  ok = ok && app_config_set_quiet(config, true) == APP_SUCCESS &&
       !app_config_is_debug(config) && app_config_is_quiet(config) &&
       !app_config_is_verbose(config);
  ok = ok && app_config_set_verbose(config, true) == APP_SUCCESS &&
       !app_config_is_debug(config) && !app_config_is_quiet(config) &&
       app_config_is_verbose(config);

  app_config_destroy(config);
  return ok;
}

static bool test_request_json_parses_command_args_and_flags(void) {
  app_request_t request;
  app_request_init(&request);
  const char *input =
      "{\"command\":\"hello\",\"args\":[\"Alice\"],"
      "\"flags\":{\"debug\":true}}";
  bool ok = app_request_parse_json(&request, input) == APP_SUCCESS &&
            request.command && strcmp(request.command, "hello") == 0 &&
            request.arg_count == 1 && strcmp(request.args[0], "Alice") == 0 &&
            request.flag_seen[APP_FLAG_DEBUG] &&
            request.flag_values[APP_FLAG_DEBUG];
  app_request_destroy(&request);
  return ok;
}

static bool test_request_json_applies_to_config(void) {
  app_request_t request;
  app_request_init(&request);
  app_config_t *config = NULL;
  bool ok = app_request_parse_json(&request,
                                   "{\"command\":\"echo\",\"args\":[\"hi\"],"
                                   "\"flags\":{\"plain_output\":true}}") ==
                APP_SUCCESS &&
            app_config_create(&config) == APP_SUCCESS;
  if (ok) {
    ok = app_request_apply_to_config(&request, config) == APP_SUCCESS &&
         strcmp(app_config_get_command(config), "echo") == 0 &&
         app_config_is_plain_output(config);
  }

  int count = 0;
  char *const *args =
      config ? app_config_get_command_args(config, &count) : NULL;
  ok = ok && count == 1 && args && strcmp(args[0], "hi") == 0;

  app_config_destroy(config);
  app_request_destroy(&request);
  return ok;
}

static bool test_request_json_rejects_unknown_flag(void) {
  app_request_t request;
  app_request_init(&request);
  const app_error err = app_request_parse_json(
      &request, "{\"command\":\"hello\",\"flags\":{\"bogus\":true}}");
  app_request_destroy(&request);
  return err == APP_ERROR_UNKNOWN_OPTION;
}

static bool test_request_json_decodes_bmp_unicode_escape(void) {
  // "café" must decode to UTF-8 "café" (é == 0xC3 0xA9).
  app_request_t request;
  app_request_init(&request);
  const char *input = "{\"command\":\"echo\",\"args\":[\"caf\\u00e9\"]}";
  const bool ok = app_request_parse_json(&request, input) == APP_SUCCESS &&
                  request.arg_count == 1 &&
                  strcmp(request.args[0], "caf\xc3\xa9") == 0;
  app_request_destroy(&request);
  return ok;
}

static bool test_request_json_decodes_surrogate_pair(void) {
  // 😀 == U+1F600 grinning face == UTF-8 F0 9F 98 80.
  app_request_t request;
  app_request_init(&request);
  const char *input = "{\"command\":\"echo\",\"args\":[\"\\uD83D\\uDE00\"]}";
  const bool ok = app_request_parse_json(&request, input) == APP_SUCCESS &&
                  request.arg_count == 1 &&
                  strcmp(request.args[0], "\xf0\x9f\x98\x80") == 0;
  app_request_destroy(&request);
  return ok;
}

static bool test_request_json_rejects_lone_surrogate(void) {
  app_request_t request;
  app_request_init(&request);
  const char *input = "{\"command\":\"echo\",\"args\":[\"\\uD800\"]}";
  const app_error err = app_request_parse_json(&request, input);
  app_request_destroy(&request);
  return err == APP_ERROR_CONFIG_PARSE;
}

static bool test_request_json_rejects_escaped_nul(void) {
  // \u0000 would embed a NUL that silently truncates the C-string argument, so
  // it is rejected rather than accepted like other escaped controls.
  app_request_t request;
  app_request_init(&request);
  const char *input = "{\"command\":\"echo\",\"args\":[\"a\\u0000b\"]}";
  const app_error err = app_request_parse_json(&request, input);
  app_request_destroy(&request);
  return err == APP_ERROR_CONFIG_PARSE;
}

static bool test_request_json_decodes_escaped_control(void) {
  // 	 must decode to a literal tab even though an unescaped control byte
  // would be rejected.
  app_request_t request;
  app_request_init(&request);
  const char *input = "{\"command\":\"echo\",\"args\":[\"a\\u0009b\"]}";
  const bool ok = app_request_parse_json(&request, input) == APP_SUCCESS &&
                  request.arg_count == 1 &&
                  strcmp(request.args[0], "a\tb") == 0;
  app_request_destroy(&request);
  return ok;
}

static bool test_secret_zero_clears_buffer(void) {
  unsigned char buf[16];
  for (size_t i = 0; i < sizeof(buf); i++) {
    buf[i] = (unsigned char)(i + 1);
  }
  app_secret_zero(buf, sizeof(buf));
  for (size_t i = 0; i < sizeof(buf); i++) {
    if (buf[i] != 0) {
      return false;
    }
  }
  app_secret_zero(NULL, 0);
  app_secret_zero(buf, 0);
  return true;
}

// app_flag_apply() indexes g_app_flag_table[id] directly (config.c), so the
// table MUST stay parallel to the app_flag_id enum: entry i describes flag i.
// A reorder of either the enum or the table would silently apply the wrong
// flag's exclusivity mask. Guard the invariant (and json_key hygiene) so that
// drift fails a fast in-process test instead of corrupting runtime behavior.
static bool test_flag_table_matches_enum_order(void) {
  size_t count = 0;
  const app_flag_spec_t *table = app_flag_table(&count);
  if (!table || count != APP_FLAG_COUNT) {
    return false;
  }
  for (size_t i = 0; i < count; i++) {
    if (table[i].id != (app_flag_id)i) {
      return false;
    }
    if (!table[i].json_key || table[i].json_key[0] == '\0') {
      return false;
    }
    for (size_t j = 0; j < i; j++) {
      if (strcmp(table[i].json_key, table[j].json_key) == 0) {
        return false;  // duplicate json_key
      }
    }
  }
  return true;
}

#ifndef _WIN32
static bool test_config_load_reports_not_found(void) {
  app_config_t *config = NULL;
  if (app_config_create(&config) != APP_SUCCESS) {
    return false;
  }
  const app_error err =
      app_config_load_file(config, "/nonexistent/myapp-xyz.json");
  app_config_destroy(config);
  return err == APP_ERROR_NOT_FOUND;
}

static bool test_config_load_reports_permission(void) {
  if (geteuid() == 0) {
    return true;  // root bypasses permission bits; the distinction is moot
  }
  char path[] = "/tmp/c23cfgXXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0) {
    return false;
  }
  const bool wrote = write(fd, "{}", 2) == 2;
  (void)close(fd);

  bool ok = false;
  if (wrote && chmod(path, 0) == 0) {
    app_config_t *config = NULL;
    if (app_config_create(&config) == APP_SUCCESS) {
      ok = app_config_load_file(config, path) == APP_ERROR_PERMISSION;
      app_config_destroy(config);
    }
  }
  (void)chmod(path, 0600);
  (void)unlink(path);
  return ok;
}
#endif

void run_config_unit_tests(unit_stats_t *stats) {
  unit_record(stats, test_flag_table_matches_enum_order(),
              "flag table stays parallel to app_flag_id with unique json keys");
  unit_record(stats, test_strerror_covers_every_code(),
              "app_strerror covers every code");
  unit_record(stats, test_config_json_parses_valid_input(),
              "config_json parses valid input");
  unit_record(stats, test_config_json_rejects_unicode_escape(),
              "config_json rejects \\uXXXX escapes");
  unit_record(stats, test_config_json_skips_nested_unknown_value(),
              "config_json skips nested unknown values and applies siblings");
  unit_record(stats, test_config_json_caps_nested_depth(),
              "config_json caps nested-skip recursion depth");
  unit_record(stats, test_config_json_rejects_trailing_garbage(),
              "config_json rejects trailing garbage");
  unit_record(stats, test_config_json_rejects_long_keys(),
              "config_json rejects truncated keys");
  unit_record(stats, test_config_json_output_exclusivity(),
              "config_json enforces output flag exclusivity");
  unit_record(stats, test_config_json_log_level_exclusivity(),
              "config_json enforces log-level exclusivity");
  unit_record(stats, test_config_setter_log_level_exclusivity(),
              "config setters enforce log-level exclusivity");
  unit_record(stats, test_request_json_parses_command_args_and_flags(),
              "request_json parses command, args, and flags");
  unit_record(stats, test_request_json_applies_to_config(),
              "request_json applies parsed values to config");
  unit_record(stats, test_request_json_rejects_unknown_flag(),
              "request_json rejects unknown flags");
  unit_record(stats, test_request_json_decodes_bmp_unicode_escape(),
              "request_json decodes \\uXXXX BMP escapes to UTF-8");
  unit_record(stats, test_request_json_decodes_surrogate_pair(),
              "request_json decodes \\uXXXX surrogate pairs to UTF-8");
  unit_record(stats, test_request_json_rejects_lone_surrogate(),
              "request_json rejects lone \\uXXXX surrogates");
  unit_record(stats, test_request_json_rejects_escaped_nul(),
              "request_json rejects an escaped NUL (\\u0000)");
  unit_record(stats, test_request_json_decodes_escaped_control(),
              "request_json decodes escaped control code points");
#ifndef _WIN32
  unit_record(stats, test_config_env_no_color_empty_sets_flag(),
              "config env treats empty NO_COLOR as present");
  unit_record(stats, test_use_colors_honors_no_color_without_env_load(),
              "colors honor NO_COLOR even when config skipped env load");
  unit_record(stats, test_color_env_force_resolves_conventions(),
              "color env resolver follows FORCE_COLOR/CLICOLOR conventions");
  unit_record(stats, test_use_colors_no_color_beats_force_color(),
              "NO_COLOR beats FORCE_COLOR=1 in app_use_colors");
  unit_record(stats, test_use_colors_honors_cli_color_never(),
              "APP_CLI_COLOR=never disables app_use_colors");
  unit_record(stats, test_config_load_reports_not_found(),
              "config load maps a missing explicit path to NOT_FOUND");
  unit_record(stats, test_config_load_reports_permission(),
              "config load maps an unreadable file to PERMISSION");
#endif
  unit_record(stats, test_secret_zero_clears_buffer(),
              "app_secret_zero clears buffer");
}
