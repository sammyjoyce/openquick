/*
 * cs_progress — a one-shot progress bar rendered to any surface.
 *
 *   cs_progress_render(&(cs_progress_t){
 *       .label = "Building", .current = 7, .total = 10, .show_percent = true,
 *   }, s);
 *   // Building  ████████████░░░░░░░░   70%
 *
 * This is the stateless, surface-agnostic bar (distinct from the interactive
 * tui_progress, which owns a window and animates). Set either `value` (0..1) or
 * `current`/`total`. Emits one line with a trailing newline; for an animated
 * line, render onto a curses surface or print "\r" yourself between frames.
 */

#pragma once

#include "../surface/surface.h"

typedef struct cs_progress {
  const char *label;     // optional, shown before the bar
  double value;          // 0..1 fraction (used when total <= 0)
  long current;          // with total > 0, fraction = current/total
  long total;            // 0 => use `value`
  size_t width;          // total columns (0 => the surface width)
  cs_role_t bar_role;    // filled portion (default: CS_ROLE_SUCCESS)
  cs_role_t track_role;  // empty portion (default: CS_ROLE_MUTED)
  bool bar_role_set;     // true when `bar_role` is intentional
  bool track_role_set;   // true when `track_role` is intentional
  bool show_percent;     // append the percentage
} cs_progress_t;

void cs_progress_render(const cs_progress_t *progress, cs_surface_t *s);
