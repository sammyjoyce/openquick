/*
 * Curses-free diagnostic check collection shared by CLI doctor and future TUI
 * system/diagnostics screens. Rendering remains caller-specific.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "error.h"

#define APP_DIAGNOSTIC_DETAIL_MAX 512

typedef enum {
  APP_CHECK_OK = 0,
  APP_CHECK_WARN,
  APP_CHECK_SKIP,
  APP_CHECK_FAIL,
} app_check_status_t;

typedef struct {
  const char *name;
  app_check_status_t status;
  char detail[APP_DIAGNOSTIC_DETAIL_MAX];
  bool enabled;
  bool has_enabled;
} app_diagnostic_check_t;

const char *app_check_status_name(app_check_status_t status);
APP_NODISCARD app_error app_diagnostics_collect(const app_config_t *config,
                                                app_diagnostic_check_t *buffer,
                                                size_t buffer_count,
                                                size_t *out_count);
