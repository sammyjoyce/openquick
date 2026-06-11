/*
 * Help text display for the application.
 *
 * Provides two levels of help: concise for quick reminders and verbose for
 * full documentation. This dual approach balances discoverability with avoiding
 * information overload. Help text is kept in sync with actual functionality
 * to prevent documentation drift.
 */

#pragma once

#include "commands.h"

// Display concise help when no arguments are provided.
// Shows only essential usage to avoid overwhelming new users while encouraging
// them to use --help for detailed information. This appears on stderr to keep
// stdout clean for pipeline usage.
void app_print_concise_help(const char *program_name);

// Display full help for --help flag with complete option documentation.
// Includes all options, environment variables, and examples. Goes to stdout
// as this is explicitly requested documentation, not an error condition.
void app_print_verbose_usage(const char *program_name);

// Display command-specific help for `<command> --help`.
void app_print_command_help(const char *program_name,
                            const app_command_t *command);

// Config-aware variants. When a config is available, pass it so that
// --plain/--no-color/--json are honored by the styled renderer. A NULL config
// falls back to environment-based detection (NO_COLOR, TERM, isatty).
void app_print_concise_help_ex(const char *program_name,
                               const app_config_t *config);
void app_print_verbose_usage_ex(const char *program_name,
                                const app_config_t *config);
void app_print_command_help_ex(const char *program_name,
                               const app_config_t *config,
                               const app_command_t *command);
