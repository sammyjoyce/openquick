/*
 * cs_spinner — a frame-based activity indicator.
 *
 * Stateless: the caller owns the frame counter and the redraw cadence. Render a
 * frame, sleep, advance, repeat — moving the cursor back with "\r" on a stream
 * or redrawing the cell on a curses surface:
 *
 *   for (int f = 0; working; f++) {
 *       fputc('\r', stdout);
 *       cs_spinner_render(&(cs_spinner_t){.label = "Working"}, f, s);
 *       fflush(stdout); nap();
 *   }
 *
 * Renders inline (no trailing newline). Braille frames degrade to an ASCII
 * line-spinner when unicode is unavailable.
 */

#pragma once

#include "../surface/surface.h"

typedef enum cs_spinner_style {
  CS_SPINNER_DOTS = 0,  // braille dots (unicode), ASCII line fallback
  CS_SPINNER_LINE,      // pipe, slash, dash, backslash
} cs_spinner_style_t;

typedef struct cs_spinner {
  const char *label;  // optional, shown after the spinner glyph
  cs_spinner_style_t style;
  cs_role_t role;  // spinner glyph color (default: CS_ROLE_ACCENT)
  bool role_set;   // true when `role` is intentional
} cs_spinner_t;

// Number of distinct frames for a style at a given unicode capability.
int cs_spinner_frame_count(cs_spinner_style_t style, bool unicode);

// The glyph for `frame` (taken modulo the frame count).
const char *cs_spinner_frame(cs_spinner_style_t style, bool unicode, int frame);

// Render one frame inline (glyph + optional label), no trailing newline.
void cs_spinner_render(const cs_spinner_t *spinner, int frame, cs_surface_t *s);
