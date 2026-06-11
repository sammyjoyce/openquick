/*
 * Terminal color handling implementation.
 */

#include "colors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "../core/config.h"

static bool app_stderr_is_terminal(void) {
#ifdef _WIN32
  return _isatty(_fileno(stderr)) != 0;
#else
  return isatty(STDERR_FILENO) != 0;
#endif
}

app_color_force_t app_color_env_force(void) {
  const char *force_color = getenv("FORCE_COLOR");
  if (force_color) {
    if (strcmp(force_color, "0") == 0 || strcmp(force_color, "false") == 0) {
      return APP_COLOR_FORCE_OFF;
    }
    return APP_COLOR_FORCE_ON;
  }

  const char *clicolor_force = getenv("CLICOLOR_FORCE");
  if (clicolor_force && clicolor_force[0] != '\0' &&
      strcmp(clicolor_force, "0") != 0) {
    return APP_COLOR_FORCE_ON;
  }

  const char *clicolor = getenv("CLICOLOR");
  if (clicolor && strcmp(clicolor, "0") == 0) {
    return APP_COLOR_FORCE_OFF;
  }

  return APP_COLOR_FORCE_AUTO;
}

bool app_use_colors(const app_config_t *config) {
  // Explicit plain/no-color modes take precedence over terminal heuristics.
  if (config &&
      (app_config_is_plain_output(config) || app_config_is_no_color(config))) {
    return false;
  }

  // NO_COLOR uses the same table-driven env policy as app_config_load_env().
  if (app_flag_env_enabled(APP_FLAG_NO_COLOR)) {
    return false;
  }

  // Shared UI style policy: APP_CLI_COLOR=never disables both the styled CLI
  // layer and the generated TUI color bridge. The APP_CLI_ prefix is kept for
  // compatibility with the existing public environment contract. Treat it as a
  // hard disable, like NO_COLOR, so it wins over FORCE_COLOR=1.
  const char *cli_color = getenv("APP_CLI_COLOR");
  if (cli_color && strcmp(cli_color, "never") == 0) {
    return false;
  }

  // FORCE_COLOR / CLICOLOR_FORCE / CLICOLOR, parsed per the de-facto spec:
  // FORCE_COLOR=0 disables, a force flag enables even off a TTY.
  switch (app_color_env_force()) {
  case APP_COLOR_FORCE_OFF:
    return false;
  case APP_COLOR_FORCE_ON:
    return true;
  case APP_COLOR_FORCE_AUTO:
    break;
  }

  // Check if TERM is dumb
  const char *term = getenv("TERM");
  if (term != NULL && strcmp(term, "dumb") == 0) {
    return false;
  }

  // Check if output is to a terminal
  return app_stderr_is_terminal();
}
