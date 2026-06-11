/*
 * Minimal JSON reader for app configuration files.
 */

#pragma once

#include <stdbool.h>

#include "config.h"
#include "error.h"

// Staged boolean state used while parsing. The order matches app_flag_id so
// values can be indexed directly with a flag id.
typedef struct {
  bool values[APP_FLAG_COUNT];
} app_config_json_state_t;

APP_NODISCARD app_error app_config_parse_json_state(
    app_config_json_state_t *staged, const char *content);
