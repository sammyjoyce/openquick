/*
 * Output handling for the application.
 *
 * Manages output formatting to support multiple output modes (JSON, plain text,
 * quiet). The module ensures proper formatting based on configuration while
 * maintaining compatibility with downstream tools expecting specific formats.
 * Output goes to stdout for pipeline integration, with errors going to stderr.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../core/types.h"

// The app_config_t type comes from core/types.h so this header can respect
// config settings without including config.h.

// Output text with appropriate formatting based on configuration.
// Handles JSON output mode, plain text, or colored output as configured.
// The output goes to stdout for normal output or stderr for errors.
void app_output(const char *text, const app_config_t *config, bool is_error);

// Output formatted text similar to printf.
// Provides convenience wrapper around app_output with printf-style formatting.
void app_output_format(const app_config_t *config, bool is_error,
                       const char *fmt, ...);

// JSON helpers for command-specific structured output.
void app_json_begin_object(FILE *stream);
void app_json_end_object(FILE *stream);
void app_json_end_line(FILE *stream);
void app_json_write_string(FILE *stream, const char *text);
void app_json_write_string_field(FILE *stream, const char *key,
                                 const char *value, bool *needs_comma);
void app_json_write_bool_field(FILE *stream, const char *key, bool value,
                               bool *needs_comma);
void app_json_write_raw_field(FILE *stream, const char *key, const char *value,
                              bool *needs_comma);

// Pretty JSON helpers for stable, checked-in machine contract documents.
void app_json_write_indent(FILE *stream, int level);
void app_json_write_pretty_string_field(FILE *stream, int level,
                                        const char *key, const char *value,
                                        bool comma);
void app_json_write_pretty_bool_field(FILE *stream, int level, const char *key,
                                      bool value, bool comma);
void app_json_write_pretty_int_field(FILE *stream, int level, const char *key,
                                     int value, bool comma);
void app_json_write_pretty_null_field(FILE *stream, int level, const char *key,
                                      bool comma);
