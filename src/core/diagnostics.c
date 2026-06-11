#include "diagnostics.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../io/terminal.h"
#include "../utils/colors.h"
#include "app_info.h"

const char *app_check_status_name(app_check_status_t status) {
  switch (status) {
  case APP_CHECK_OK:
    return "ok";
  case APP_CHECK_WARN:
    return "warn";
  case APP_CHECK_SKIP:
    return "skip";
  case APP_CHECK_FAIL:
    return "fail";
  }
  /* Fail closed: an out-of-range status is a programming error (enum misuse or
   * memory corruption). Surface it as "unknown" rather than silently
   * downgrading it to a benign "warn" in user-facing doctor/info output. */
  assert(false && "unhandled app_check_status_t");
  return "unknown";
}

static bool append_check(app_diagnostic_check_t *buffer, size_t buffer_count,
                         size_t *count, const app_diagnostic_check_t *check) {
  if (*count >= buffer_count) {
    return false;
  }
  buffer[*count] = *check;
  (*count)++;
  return true;
}

app_error app_diagnostics_collect(const app_config_t *config,
                                  app_diagnostic_check_t *buffer,
                                  size_t buffer_count, size_t *out_count) {
  if (!config || !buffer || !out_count) {
    return APP_ERROR_INVALID_ARG;
  }
  *out_count = 0;

  const app_build_info_t *build = app_build_info();
  const app_feature_info_t *tui_feature = app_feature_find(APP_FEATURE_TUI);
  const bool tui_enabled = tui_feature && tui_feature->compiled;
  const bool git_known = strcmp(build->git_commit, "unknown") != 0;
  const bool terminal_ready = app_terminal_is_interactive();
  const bool color_enabled = app_use_colors(config);
  const char *config_file = app_config_get_config_file(config);
  const bool config_loaded = config_file && config_file[0] != '\0';

  app_diagnostic_check_t check = {.name = "binary", .status = APP_CHECK_OK};
  snprintf(check.detail, sizeof(check.detail), "%s %s", build->name,
           build->version);
  if (!append_check(buffer, buffer_count, out_count, &check)) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  check = (app_diagnostic_check_t){
      .name = "git", .status = git_known ? APP_CHECK_OK : APP_CHECK_WARN};
  snprintf(check.detail, sizeof(check.detail), "%s",
           git_known ? build->git_commit : "commit metadata unavailable");
  if (!append_check(buffer, buffer_count, out_count, &check)) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  check = (app_diagnostic_check_t){
      .name = "tui_compiled",
      .status = tui_enabled ? APP_CHECK_OK : APP_CHECK_WARN,
      .enabled = tui_enabled,
      .has_enabled = true};
  snprintf(check.detail, sizeof(check.detail), "%s",
           tui_enabled ? "compiled with TUI support"
                       : "rebuild with -Denable-tui=true");
  if (!append_check(buffer, buffer_count, out_count, &check)) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  check = (app_diagnostic_check_t){
      .name = "tui_terminal",
      .status = terminal_ready ? APP_CHECK_OK : APP_CHECK_WARN,
      .enabled = terminal_ready,
      .has_enabled = true};
  snprintf(check.detail, sizeof(check.detail), "%s",
           terminal_ready ? "stdin/stdout are TTYs" : "stdin/stdout not TTYs");
  if (!append_check(buffer, buffer_count, out_count, &check)) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  check = (app_diagnostic_check_t){
      .name = "color_output",
      .status = color_enabled ? APP_CHECK_OK : APP_CHECK_WARN,
      .enabled = color_enabled,
      .has_enabled = true};
  snprintf(check.detail, sizeof(check.detail), "%s",
           color_enabled ? "enabled" : "disabled for this output");
  if (!append_check(buffer, buffer_count, out_count, &check)) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  check = (app_diagnostic_check_t){
      .name = "config_file",
      .status = config_loaded ? APP_CHECK_OK : APP_CHECK_SKIP,
      .enabled = config_loaded,
      .has_enabled = true};
  snprintf(check.detail, sizeof(check.detail), "%s",
           config_loaded ? config_file : "no config file loaded");
  if (!append_check(buffer, buffer_count, out_count, &check)) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  check = (app_diagnostic_check_t){.name = "quiet_mode",
                                   .status = APP_CHECK_OK,
                                   .enabled = app_config_is_quiet(config),
                                   .has_enabled = true};
  snprintf(check.detail, sizeof(check.detail), "%s",
           app_bool_word(app_config_is_quiet(config)));
  if (!append_check(buffer, buffer_count, out_count, &check)) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  check = (app_diagnostic_check_t){.name = "json_output",
                                   .status = APP_CHECK_OK,
                                   .enabled = app_config_is_json_output(config),
                                   .has_enabled = true};
  snprintf(check.detail, sizeof(check.detail), "%s",
           app_bool_word(app_config_is_json_output(config)));
  if (!append_check(buffer, buffer_count, out_count, &check)) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  return APP_SUCCESS;
}
