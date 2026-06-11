/*
 * Headless JSON request parser.
 *
 * The bare non-TTY launch path accepts a small request object on stdin and maps
 * it onto the same command/config data used by ordinary argv dispatch.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "error.h"
#include "types.h"

typedef struct {
  char *command;
  char *args[APP_MAX_COMMAND_ARGS];
  size_t arg_count;
  bool flag_seen[APP_FLAG_COUNT];
  bool flag_values[APP_FLAG_COUNT];
} app_request_t;

void app_request_init(app_request_t *request);
void app_request_destroy(app_request_t *request);

APP_NODISCARD app_error app_request_parse_json(app_request_t *request,
                                               const char *content);
APP_NODISCARD app_error
app_request_apply_to_config(const app_request_t *request, app_config_t *config);
