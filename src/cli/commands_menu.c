/*
 * "menu" command - launches the TUI showcase when compiled in.
 */

#include "../core/app_info.h"
#include "../core/config.h"
#include "../core/error.h"
#include "../core/types.h"
#include "../io/output.h"
#include "../utils/colors.h"
#ifdef ENABLE_TUI
#include "../tui/tui.h"
#endif
#include "commands.h"

app_error app_run_tui(const app_config_t *config) {
  // Single TUI launch precondition shared by `myapp menu` and the bare-TTY
  // launch in main(): the interactive TUI cannot coexist with machine JSON
  // output. Reject it once, identically, on both entry points (this was
  // duplicated with divergent wording in main.c and app_cmd_menu).
  if (app_config_is_json_output(config)) {
    app_output(
        "JSON output is incompatible with the interactive TUI; remove "
        "--json (or unset json_output) to launch it.",
        config, true);
    return APP_ERROR_INVALID_ARG;
  }
#ifdef ENABLE_TUI
  // Mirror the CLI's resolved color policy in the TUI so NO_COLOR / FORCE_COLOR
  // / CLICOLOR(_FORCE) / --no-color / --plain behave identically on both
  // surfaces. The TUI otherwise consulted only terminal capability.
  tui_set_color_enabled(app_use_colors(config));
  const app_error tui_err = tui_run_app();
  // Signal-driven exits already use conventional shell statuses. Keep them
  // quiet instead of printing a misleading TUI failure diagnostic.
  if (tui_err == APP_ERROR_INTERRUPTED || tui_err == APP_ERROR_TERMINATED) {
    return tui_err;
  }
  if (tui_err == APP_ERROR_OUT_OF_RANGE) {
    app_output_format(config, true,
                      "TUI failed: terminal is too small (minimum %dx%d).",
                      TUI_MIN_COLS, TUI_MIN_ROWS);
  } else if (tui_err != APP_SUCCESS) {
    app_output_format(config, true, "TUI failed: %s", app_strerror(tui_err));
  }
  return tui_err;
#else
  const app_feature_info_t *feature = app_feature_find(APP_FEATURE_TUI);
  if (feature && feature->build_option) {
    app_output_format(
        config, true,
        "TUI support is not compiled in. Rebuild with 'zig build %s'.",
        feature->build_option);
  } else {
    app_output("TUI support is not compiled in.", config, true);
  }
  return APP_ERROR_CONFIG;
#endif
}

app_error app_cmd_menu(const app_config_t *config, int argc,
                       char *const argv[]);

app_error app_cmd_menu(const app_config_t *config, int argc,
                       char *const argv[]) {
  (void)argc;
  (void)argv;

  // The --json precondition lives in app_run_tui() now, so `menu` and the
  // bare-TTY launch reject it identically.
  return app_run_tui(config);
}
