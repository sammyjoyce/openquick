#pragma once

#include <stddef.h>

#include "tui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *key;
  const char *value;
  tui_color_pair_t color;
} quick_tui_kv_row_t;

void quick_tui_show_keyvalue_panel(const char *title,
                                   const quick_tui_kv_row_t *rows,
                                   size_t row_count, const char *footer);
void quick_tui_show_lines_panel(const char *title, const char *const *lines,
                                size_t line_count, const char *footer);

#ifdef __cplusplus
}
#endif
