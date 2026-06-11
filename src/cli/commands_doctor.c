/*
 * "doctor" command - emits a short readiness report.
 *
 * The interactive TUI runtime smoke test only runs when the user asks for it
 * via `doctor --deep`. Default `doctor` is non-destructive and never enters
 * ncurses, so it is safe to run mid-pipeline.
 */

#include <stdio.h>
#include <string.h>

#include "../core/app_info.h"
#include "../core/config.h"
#include "../core/diagnostics.h"
#include "../core/error.h"
#include "../core/types.h"
#include "../io/output.h"
#include "../io/terminal.h"
#ifdef ENABLE_TUI
#include "../tui/tui.h"
#endif
#include "commands.h"

app_error app_cmd_doctor(const app_config_t *config, int argc,
                         char *const argv[]);

static app_error doctor_tui_runtime_smoke(bool terminal_ready) {
#ifdef ENABLE_TUI
  if (!terminal_ready) {
    return APP_ERROR_IO;
  }

  const app_error err = tui_init();
  if (err != APP_SUCCESS) {
    return err;
  }
  if (!tui_terminal_meets_minimum()) {
    tui_cleanup();
    return APP_ERROR_OUT_OF_RANGE;
  }

  tui_cleanup();
  return APP_SUCCESS;
#else
  (void)terminal_ready;
  return APP_ERROR_FEATURE_BASE;
#endif
}

static void doctor_write_json_check(const app_diagnostic_check_t *check,
                                    bool *needs_comma) {
  if (*needs_comma) {
    fputc(',', stdout);
  }
  *needs_comma = true;

  bool field_comma = false;
  app_json_begin_object(stdout);
  app_json_write_string_field(stdout, "name", check->name, &field_comma);
  app_json_write_string_field(
      stdout, "status", app_check_status_name(check->status), &field_comma);
  app_json_write_string_field(stdout, "detail", check->detail, &field_comma);
  if (check->has_enabled) {
    app_json_write_bool_field(stdout, "enabled", check->enabled, &field_comma);
  }
  app_json_end_object(stdout);
}

app_error app_cmd_doctor(const app_config_t *config, int argc,
                         char *const argv[]) {
  if (app_config_is_quiet(config)) {
    return APP_SUCCESS;
  }

  bool deep = false;
  const app_command_t *doctor_command = app_command_find("doctor");
  for (int i = 0; i < argc; i++) {
    const app_command_option_t *option =
        app_command_option_find(doctor_command, argv[i]);
    if (option && option->id == APP_COMMAND_OPTION_DOCTOR_DEEP) {
      deep = true;
    }
  }

  const app_build_info_t *build = app_build_info();
  const app_feature_info_t *tui_feature = app_feature_find(APP_FEATURE_TUI);
  const bool tui_enabled = tui_feature && tui_feature->compiled;
  const bool terminal_ready = app_terminal_is_interactive();

  app_error tui_runtime_err = APP_ERROR_IO;
  bool tui_runtime_ok = false;
  app_check_status_t tui_runtime_status = APP_CHECK_SKIP;
  if (deep && tui_enabled && terminal_ready) {
    tui_runtime_err = doctor_tui_runtime_smoke(terminal_ready);
    tui_runtime_ok = (tui_runtime_err == APP_SUCCESS);
    tui_runtime_status = tui_runtime_ok ? APP_CHECK_OK : APP_CHECK_WARN;
  }

  app_diagnostic_check_t checks[9];
  size_t check_count = 0;
  app_error err = app_diagnostics_collect(
      config, checks, sizeof(checks) / sizeof(checks[0]), &check_count);
  if (err != APP_SUCCESS) {
    return err;
  }

  app_diagnostic_check_t runtime_check = {.name = "tui_runtime",
                                          .status = tui_runtime_status,
                                          .enabled = tui_runtime_ok,
                                          .has_enabled = true};
  if (!deep) {
    snprintf(runtime_check.detail, sizeof(runtime_check.detail), "%s",
             "pass --deep to exercise the TUI runtime");
  } else if (!tui_enabled) {
    snprintf(runtime_check.detail, sizeof(runtime_check.detail), "%s",
             "TUI support not compiled");
  } else if (!terminal_ready) {
    snprintf(runtime_check.detail, sizeof(runtime_check.detail), "%s",
             "runtime smoke skipped without a TTY");
  } else if (tui_runtime_ok) {
    snprintf(runtime_check.detail, sizeof(runtime_check.detail), "%s",
             "ncurses initialized successfully");
#ifdef ENABLE_TUI
  } else if (tui_runtime_err == APP_ERROR_OUT_OF_RANGE) {
    snprintf(runtime_check.detail, sizeof(runtime_check.detail),
             "terminal is too small (minimum %dx%d)", TUI_MIN_COLS,
             TUI_MIN_ROWS);
#endif
  } else {
    snprintf(runtime_check.detail, sizeof(runtime_check.detail), "%s",
             app_strerror(tui_runtime_err));
  }

  if (check_count >= sizeof(checks) / sizeof(checks[0])) {
    return APP_ERROR_OUT_OF_RANGE;
  }
  /* Insert the runtime check immediately AFTER the "tui_terminal" check so the
   * runtime smoke result sits beside the related terminal-readiness check.
   * Anchoring to a stable check name (rather than a positional guess) keeps the
   * insertion point correct even if app_diagnostics_collect() reorders checks.
   * Fall back to appending if the anchor is absent so we never leave an
   * uninitialized gap. */
  size_t runtime_index = check_count;
  for (size_t i = 0; i < check_count; i++) {
    if (strcmp(checks[i].name, "tui_terminal") == 0) {
      runtime_index = i + 1;
      break;
    }
  }
  for (size_t i = check_count; i > runtime_index; i--) {
    checks[i] = checks[i - 1];
  }
  checks[runtime_index] = runtime_check;
  check_count++;

  if (app_config_is_json_output(config)) {
    bool root_comma = false;
    bool check_comma = false;

    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "format_version", "1.0", &root_comma);
    app_json_write_raw_field(stdout, "checks", "[", &root_comma);
    for (size_t i = 0; i < check_count; i++) {
      doctor_write_json_check(&checks[i], &check_comma);
    }
    fputc(']', stdout);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
    return APP_SUCCESS;
  }

  app_output_format(config, false, "%s doctor", build->name);
  for (size_t i = 0; i < check_count; i++) {
    app_output_format(config, false, "  %-13s %-4s (%s)", checks[i].name,
                      app_check_status_name(checks[i].status),
                      checks[i].detail);
  }
  return APP_SUCCESS;
}
