/*
 * Shared low-level JSON scanning primitives.
 *
 * The config reader (config_json.c) and the headless request reader
 * (request_json.c) layer different policies on top of JSON - flat scalar
 * config vs. a nested request envelope - but both need the same byte-level
 * scanning. These helpers are that common floor; higher-level structure
 * (string allocation, object/array nesting, key dispatch) stays in each
 * reader.
 */
#pragma once

#include <stdbool.h>

#include "error.h"

// Advance past JSON whitespace. Returns cursor unchanged when it is NULL or
// already at a non-space byte.
const char *app_json_skip_ws(const char *cursor);

// Match literal at cursor, requiring a value boundary immediately after. On a
// match, *end (when non-NULL) is set just past the literal. Used for the
// true/false/null keywords. Returns false when cursor or literal is NULL.
bool app_json_match_literal(const char *cursor, const char *literal,
                            const char **end);

// Skip one JSON number (RFC 8259 grammar). Advances *cursor past the number on
// success. Returns APP_ERROR_INVALID_ARG when cursor or *cursor is NULL and
// APP_ERROR_CONFIG_PARSE on a malformed number.
app_error app_json_skip_number(const char **cursor);

// Read a JSON boolean (true/false) at *cursor. On success, stores the value and
// advances *cursor past the literal. Returns APP_ERROR_INVALID_ARG for NULL
// arguments and APP_ERROR_CONFIG_PARSE when the value is not a boolean literal.
app_error app_json_read_bool(const char **cursor, bool *value);
