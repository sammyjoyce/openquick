/*
 * cs_table — columnar table. See cs_table.h.
 */

#include "cs_table.h"

#include <limits.h>
#include <stdlib.h>

#include "../ui/text_layout.h"
#include "cs_glyphs.h"

static const char *cs_table_cell(const cs_table_t *t, size_t row, size_t col) {
  const char *c = t->cells[row * t->column_count + col];
  return c ? c : "";
}

// Write `text` truncated to `col_width` columns, padded to fill the column,
// honoring alignment.
static void cs_table_write_cell(cs_surface_t *s, cs_role_t role,
                                const char *text, int col_width,
                                cs_align_t align) {
  int used = 0;
  size_t bytes = app_text_truncate_utf8_columns(text, col_width, &used);
  int pad = col_width - used;
  if (pad < 0) {
    pad = 0;
  }

  if (align == CS_ALIGN_RIGHT) {
    if (pad > 0) {
      cs_surface_repeat(s, " ", (size_t)pad);
    }
  }
  cs_surface_set_role(s, role);
  cs_surface_write_n(s, text, bytes);
  cs_surface_reset(s);
  if (align == CS_ALIGN_LEFT) {
    if (pad > 0) {
      cs_surface_repeat(s, " ", (size_t)pad);
    }
  }
}

void cs_table_render(const cs_table_t *table, cs_surface_t *s) {
  if (!table || !s || !table->columns || table->column_count == 0) {
    return;
  }
  // A non-zero row_count promises a cells array; without this guard the
  // natural width scan would dereference a NULL base pointer. (A header-only
  // table is expressed as cells == NULL with row_count == 0.)
  if (table->row_count > 0 && !table->cells) {
    return;
  }
  const size_t ncol = table->column_count;
  cs_caps_t caps = cs_surface_caps(s);
  cs_role_t header_role = cs_role_or_default(
      table->header_role, table->header_role_set, CS_ROLE_TITLE);
  cs_role_t text_role =
      cs_role_or_default(table->text_role, table->text_role_set, CS_ROLE_TEXT);
  cs_role_t border_role = cs_role_or_default(
      table->border_role, table->border_role_set, CS_ROLE_BORDER);
  const char *gap = table->gap ? table->gap : "  ";
  int gap_cols = app_text_width_utf8(gap);
  size_t budget = table->width ? table->width : cs_surface_width(s);
  if (budget == 0) {
    budget = 80;
  }

  int *widths = malloc(ncol * sizeof(int));
  if (!widths) {
    return;
  }

  // Natural width per column: max(header, cells), capped by max_width.
  for (size_t c = 0; c < ncol; c++) {
    int w = table->header
                ? app_text_width_utf8(
                      table->columns[c].header ? table->columns[c].header : "")
                : 0;
    for (size_t r = 0; r < table->row_count; r++) {
      int cw = app_text_width_utf8(cs_table_cell(table, r, c));
      if (cw > w) {
        w = cw;
      }
    }
    if (w < 1) {
      w = 1;
    }
    if (table->columns[c].max_width > 0 && w > table->columns[c].max_width) {
      w = table->columns[c].max_width;
    }
    widths[c] = w;
  }

  // Shrink proportionally if the row overflows the budget.
  const int budget_cols = budget > (size_t)INT_MAX ? INT_MAX : (int)budget;
  const size_t gap_count = ncol > 1 ? ncol - 1 : 0;
  if (gap_cols < 0) {
    gap_cols = 0;
  }
  int gaps_total = (gap_cols > 0 && gap_count > (size_t)(INT_MAX / gap_cols))
                       ? INT_MAX
                       : (int)gap_count * gap_cols;
  if (gaps_total >= budget_cols) {
    // If separators alone would exhaust the whole row, drop them before
    // shrinking cells. Rendering content under the caller's width budget beats
    // preserving cosmetic inter-column whitespace.
    gap = "";
    gap_cols = 0;
    gaps_total = 0;
  }
  int content_budget = budget_cols - gaps_total;
  if (content_budget < 0) {
    content_budget = 0;
  }
  int sum = 0;
  for (size_t c = 0; c < ncol; c++) {
    sum += widths[c];
  }
  if (sum > content_budget) {
    int min_width = ncol <= (size_t)content_budget ? 1 : 0;
    int remaining = content_budget;
    for (size_t c = 0; c < ncol; c++) {
      int scaled = (int)((long long)widths[c] * content_budget / sum);
      if (scaled < min_width) {
        scaled = min_width;
      }
      widths[c] = scaled;
      remaining -= scaled;
    }
    // Hand any rounding leftover to the first column.
    if (remaining > 0) {
      widths[0] += remaining;
    }
    // The min-1 clamp above can push the assigned widths past the budget when
    // several columns floor to 0 and round up. Reclaim that overrun from the
    // widest columns so the table never renders wider than requested.
    while (remaining < 0) {
      size_t widest = 0;
      for (size_t c = 1; c < ncol; c++) {
        if (widths[c] > widths[widest]) {
          widest = c;
        }
      }
      if (widths[widest] <= min_width) {
        break;  // every column is already at its floor
      }
      widths[widest]--;
      remaining++;
    }
  }

  int total = gaps_total;
  for (size_t c = 0; c < ncol; c++) {
    total += widths[c];
  }

  // Header row + separator.
  if (table->header) {
    for (size_t c = 0; c < ncol; c++) {
      if (c > 0) {
        cs_surface_write(s, gap);
      }
      const char *h = table->columns[c].header ? table->columns[c].header : "";
      cs_surface_set_attr(s, CS_ATTR_BOLD);
      cs_table_write_cell(s, header_role, h, widths[c],
                          table->columns[c].align);
    }
    cs_surface_newline(s);
    cs_surface_set_role(s, border_role);
    cs_surface_repeat(s, cs_glyph_hline(caps.unicode), (size_t)total);
    cs_surface_reset(s);
    cs_surface_newline(s);
  }

  // Body rows.
  for (size_t r = 0; r < table->row_count; r++) {
    for (size_t c = 0; c < ncol; c++) {
      if (c > 0) {
        cs_surface_write(s, gap);
      }
      cs_table_write_cell(s, text_role, cs_table_cell(table, r, c), widths[c],
                          table->columns[c].align);
    }
    cs_surface_newline(s);
  }

  free(widths);
}
