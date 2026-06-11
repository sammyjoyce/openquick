/*
 * Error handling implementation for the application.
 *
 * Provides human-readable error messages for all concrete error codes.
 */

#include "error.h"

static const app_error_info_t g_app_error_table[] = {
#define APP_ERROR_TABLE_ITEM(name, code, description) {name, description},
    APP_ERROR_LIST(APP_ERROR_TABLE_ITEM)
#undef APP_ERROR_TABLE_ITEM
};

const app_error_info_t *app_error_table(size_t *count) {
  if (count) {
    *count = sizeof(g_app_error_table) / sizeof(g_app_error_table[0]);
  }
  return g_app_error_table;
}

const char *app_strerror(app_error error_code) {
  size_t count = 0;
  const app_error_info_t *errors = app_error_table(&count);
  for (size_t i = 0; i < count; i++) {
    if (errors[i].code == error_code) {
      return errors[i].description;
    }
  }
  return "Unknown error";
}
