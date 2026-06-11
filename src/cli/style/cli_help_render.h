/*
 * Styled help rendering driven by the command/flag metadata tables. Produces
 * Fang-style sectioned output (USAGE / COMMANDS / OPTIONS / ...) with aligned
 * columns, degrading to clean plain text when styling is unavailable.
 */

#pragma once

#include <stdio.h>

#include "../../core/config.h"
#include "../commands.h"

// Root help. `verbose` selects the full --help layout vs. the concise summary.
void app_cli_render_root_help(const app_config_t *config, FILE *out,
                              const char *program_name, bool verbose);

// Help for a single command (`<program> <command> --help`).
void app_cli_render_command_help(const app_config_t *config, FILE *out,
                                 const char *program_name,
                                 const app_command_t *command);
