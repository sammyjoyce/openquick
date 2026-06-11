/*
 * Command-line argument parsing for the application.
 *
 * Handles parsing of all command-line options and arguments with proper
 * validation. Supports both short and long option formats for user convenience.
 * The parser integrates with the config system to apply command-line overrides
 * as the highest priority configuration source.
 */

#pragma once

#include "../core/error.h"
#include "../core/types.h"

// The parser updates config based on parsed arguments.

// Handle immediate process-level flags before configuration files are loaded.
// Special handling: exits with code 0 for --help/--version.
APP_NODISCARD app_error app_args_handle_immediate_exit(int argc, char *argv[]);

// Find the explicit global -c/--config path before lower-precedence config
// sources are loaded. Only options before the command are considered.
APP_NODISCARD app_error app_args_find_config_path(int argc, char *argv[],
                                                  const char **config_path);

// Parse command line arguments and update configuration accordingly.
// Returns APP_SUCCESS on success, or appropriate error code for invalid
// arguments. Special handling: exits with code 0 for --help/--version (not an
// error). This allows scripts to check app capabilities without error handling.
APP_NODISCARD app_error app_parse_args(int argc, char *argv[],
                                       app_config_t *config);
