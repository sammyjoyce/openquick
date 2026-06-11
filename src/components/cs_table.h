/*
 * cs_table — a columnar table with alignment and width budgeting.
 *
 *   cs_table_column_t cols[] = {{.header = "Name"}, {.header = "Status"}};
 *   const char *cells[] = {"build", "ok", "tests", "ok"}; // row-major
 *   cs_table_render(&(cs_table_t){
 *       .columns = cols, .column_count = 2,
 *       .cells = cells, .row_count = 2,
 *       .header = true,
 *   }, s);
 *
 * Column widths are sized to their content (header + cells), capped per column
 * and shrunk proportionally to fit the surface width; cells that exceed their
 * column are truncated. Cells are addressed row-major: cells[row*cols + col].
 */

#pragma once

#include "../surface/surface.h"

typedef enum cs_align {
  CS_ALIGN_LEFT = 0,
  CS_ALIGN_RIGHT,
} cs_align_t;

typedef struct cs_table_column {
  const char *header;
  cs_align_t align;
  int max_width;  // 0 => unbounded (still subject to the overall width budget)
} cs_table_column_t;

typedef struct cs_table {
  const cs_table_column_t *columns;
  size_t column_count;
  const char *const *cells;  // row-major, column_count entries per row
  size_t row_count;
  bool header;            // render the header row + a separator rule
  cs_role_t header_role;  // default: CS_ROLE_TITLE
  cs_role_t text_role;    // default: CS_ROLE_TEXT
  cs_role_t border_role;  // default: CS_ROLE_BORDER
  bool header_role_set;   // true when `header_role` is intentional
  bool text_role_set;     // true when `text_role` is intentional
  bool border_role_set;   // true when `border_role` is intentional
  size_t width;           // total budget (0 => the surface width)
  const char *gap;        // inter-column gap (NULL => "  ")
} cs_table_t;

void cs_table_render(const cs_table_t *table, cs_surface_t *s);
