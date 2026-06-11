/*
 * Error handling definitions for the application.
 *
 * Centralizes all error codes to ensure consistent error reporting across the
 * application. By using numeric codes with human-readable descriptions, we
 * enable both programmatic error handling and meaningful user feedback. The
 * error code ranges are designed to help quickly identify the error category
 * during debugging.
 */

#pragma once

#include "types.h"

// Public error codes are grouped by category with reserved ranges to aid
// debugging. Keep descriptions user-facing; this list generates both the enum
// and the public error table. Signal-derived interactive exits use the shell
// convention: 128 + signal number.
#define APP_ERROR_LIST(X)                                                 \
  X(APP_SUCCESS, 0, "Success")                                            \
  X(APP_ERROR_INVALID_ARG, 1, "Invalid argument")                         \
  X(APP_ERROR_INVALID_COMMAND, 2, "Invalid or unknown command")           \
  X(APP_ERROR_CONFIG, 3, "Configuration file error")                      \
  X(APP_ERROR_CONFIG_PARSE, 4, "Configuration file parse error")          \
  X(APP_ERROR_CONFIG_INVALID, 5, "Configuration file has invalid values") \
  X(APP_ERROR_MISSING_ARG, 6, "Missing required argument")                \
  X(APP_ERROR_UNKNOWN_OPTION, 7, "Unknown option")                        \
  X(APP_ERROR_MEMORY, 10, "Memory allocation error")                      \
  X(APP_ERROR_IO, 11, "I/O error")                                        \
  X(APP_ERROR_PERMISSION, 12, "Permission denied")                        \
  X(APP_ERROR_INTERNAL, 13, "Internal error")                             \
  X(APP_ERROR_THREADING, 14, "Thread/mutex error")                        \
  X(APP_ERROR_RESOURCE, 15, "Resource exhaustion")                        \
  X(APP_ERROR_SIGNAL, 16, "Signal handling error")                        \
  X(APP_ERROR_NOT_FOUND, 17, "File or resource not found")                \
  X(APP_ERROR_INVALID_DATA, 20, "Invalid data format")                    \
  X(APP_ERROR_PARSE_ERROR, 21, "Parse error")                             \
  X(APP_ERROR_VALIDATION, 22, "Validation failed")                        \
  X(APP_ERROR_OVERFLOW, 23, "Numeric overflow")                           \
  X(APP_ERROR_UNDERFLOW, 24, "Numeric underflow")                         \
  X(APP_ERROR_OUT_OF_RANGE, 25, "Value out of range")                     \
  X(APP_ERROR_INTERRUPTED, 130, "Interrupted")                            \
  X(APP_ERROR_TERMINATED, 143, "Terminated")

typedef enum {
#define APP_ERROR_ENUM_ITEM(name, code, description) name = code,
  APP_ERROR_LIST(APP_ERROR_ENUM_ITEM)
#undef APP_ERROR_ENUM_ITEM

  // Feature-specific errors may use 30..125. Keep 126..127 available for
  // common shell launch/command statuses, and keep 128+signal reserved for
  // signal-derived exits such as APP_ERROR_INTERRUPTED and
  // APP_ERROR_TERMINATED.
  APP_ERROR_FEATURE_BASE = 30,
  APP_ERROR_FEATURE_MAX = 125,
} app_error;

typedef struct {
  app_error code;
  const char *description;
} app_error_info_t;

// Return the concrete error codes that the application exposes publicly.
const app_error_info_t *app_error_table(size_t *count);

// Get human-readable error description for user-facing messages.
// This function ensures users receive meaningful feedback instead of cryptic
// error codes, improving the debugging experience and reducing support burden.
APP_NODISCARD const char *app_strerror(app_error error_code);
