/*
 * Shared option-name matching and formatting helpers for args, help, OpenCLI,
 * and future TUI command/option listings.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "commands.h"

typedef enum {
  APP_OPTION_LABEL_CLI = 0,
  APP_OPTION_LABEL_USAGE,
} app_option_label_style_t;

const char *app_option_normalized_long_name(const char *name);
const char *app_option_normalized_short_name(const char *name);
bool app_option_token_matches(const char *token, const char *long_name,
                              const char *short_name);
size_t app_option_format_label(char *buffer, size_t buffer_size,
                               const char *long_name, const char *short_name,
                               const app_command_arg_t *arguments,
                               size_t argument_count,
                               app_option_label_style_t style);
