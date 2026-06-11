/*
 * Styled CLI error rendering, mirroring Fang's error handler: an "ERROR" header
 * block followed by an indented, wrapped message, with a "Try --help" hint for
 * usage errors. Falls back to plain "Error: ..." text when styling is off (e.g.
 * stderr is not a TTY, NO_COLOR, or --json).
 */

#pragma once

#include <stdio.h>

#include "../../core/config.h"
#include "../../core/error.h"

typedef enum app_cli_error_kind_id {
  APP_CLI_ERROR_KIND_RUNTIME = 0,
  APP_CLI_ERROR_KIND_USAGE,
} app_cli_error_kind_id;

// Render `message` as an error to `err` (stderr by default). `program_name` is
// used in the usage hint.
void app_cli_render_error(const app_config_t *config, FILE *err,
                          const char *program_name, const char *message,
                          app_cli_error_kind_id kind);

// Convenience: render using an app_error code's description, optionally
// suffixed with `detail` (e.g. the offending option).
void app_cli_render_error_code(const app_config_t *config, FILE *err,
                               const char *program_name, app_error code,
                               const char *detail, app_cli_error_kind_id kind);
