/*
 * cs_note — a callout block (info / success / warning / error).
 *
 *   cs_note_render(&(cs_note_t){
 *       .variant = CS_NOTE_WARNING,
 *       .title   = "Heads up",
 *       .body    = "This action cannot be undone.",
 *   }, s);
 *
 * Draws a colored gutter bar down the left edge, an optional bold title, and a
 * word-wrapped body. Width defaults to the surface width. Emits trailing
 * newlines.
 */

#pragma once

#include "../surface/surface.h"

typedef enum cs_note_variant {
  CS_NOTE_INFO = 0,
  CS_NOTE_SUCCESS,
  CS_NOTE_WARNING,
  CS_NOTE_ERROR,
} cs_note_variant_t;

typedef struct cs_note {
  cs_note_variant_t variant;
  const char *title;  // optional
  const char *body;   // wrapped to the content width
  size_t width;       // total columns (0 => the surface width)
} cs_note_t;

void cs_note_render(const cs_note_t *note, cs_surface_t *s);
