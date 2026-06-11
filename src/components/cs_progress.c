/*
 * cs_progress — one-shot progress bar. See cs_progress.h.
 */

#include "cs_progress.h"

#include <stdio.h>

#include "../ui/text_layout.h"
#include "cs_glyphs.h"

void cs_progress_render(const cs_progress_t *progress, cs_surface_t *s) {
  if (!progress || !s) {
    return;
  }
  cs_caps_t caps = cs_surface_caps(s);
  double fraction = progress->total > 0
                        ? (double)progress->current / (double)progress->total
                        : progress->value;
  if (fraction < 0.0) {
    fraction = 0.0;
  }
  if (fraction > 1.0) {
    fraction = 1.0;
  }
  // A NaN passes both comparisons above (every compare with NaN is false), so
  // guard it explicitly: an unhandled NaN would make (int)(fraction * cols) UB
  // and the bar-fill repeat counts wrap to a near-infinite loop. (+/-inf are
  // already folded to 1.0 / 0.0 by the clamps.)
  if (fraction != fraction) {
    fraction = 0.0;
  }

  cs_role_t bar_role = cs_role_or_default(
      progress->bar_role, progress->bar_role_set, CS_ROLE_SUCCESS);
  cs_role_t track_role = cs_role_or_default(
      progress->track_role, progress->track_role_set, CS_ROLE_MUTED);
  size_t width = progress->width ? progress->width : cs_surface_width(s);
  if (width == 0) {
    width = 80;
  }

  char percent[8] = {0};
  int percent_cols = 0;
  if (progress->show_percent) {
    snprintf(percent, sizeof(percent), " %3d%%", (int)(fraction * 100.0 + 0.5));
    percent_cols = app_text_width_utf8(percent);
  }

  // Truncate the label so the label + "  " separator + a >=1-column bar + the
  // percent never overflow `width` (matches cs_table's per-cell truncation).
  // Writing the label untruncated would smear a constrained line.
  size_t label_bytes = 0;
  int label_used = 0;
  if (progress->label && progress->label[0]) {
    int label_budget = (int)width - percent_cols - 1 - 2;  // bar>=1, "  " sep
    if (label_budget > 0) {
      label_bytes = app_text_truncate_utf8_columns(progress->label,
                                                   label_budget, &label_used);
    }
  }
  int label_cols = label_used > 0 ? label_used + 2 : 0;  // include "  " sep

  int bar_cols = (int)width - label_cols - percent_cols;
  if (bar_cols < 1) {
    bar_cols = 1;
  }
  int filled = (int)(fraction * bar_cols + 0.5);
  if (filled > bar_cols) {
    filled = bar_cols;
  }

  if (label_used > 0) {
    cs_surface_set_role(s, CS_ROLE_TEXT);
    cs_surface_write_n(s, progress->label, label_bytes);
    cs_surface_reset(s);
    cs_surface_write(s, "  ");
  }

  cs_surface_set_role(s, bar_role);
  cs_surface_repeat(s, cs_glyph_bar_full(caps.unicode), (size_t)filled);
  cs_surface_reset(s);
  cs_surface_set_role(s, track_role);
  cs_surface_repeat(s, cs_glyph_bar_empty(caps.unicode),
                    (size_t)(bar_cols - filled));
  cs_surface_reset(s);

  if (progress->show_percent) {
    cs_surface_set_role(s, CS_ROLE_MUTED);
    cs_surface_write(s, percent);
    cs_surface_reset(s);
  }
  cs_surface_newline(s);
}
