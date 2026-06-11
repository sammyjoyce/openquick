/*
 * cs_badge — a small inline status label, colored by variant.
 *
 *   cs_badge_render(&(cs_badge_t){.text = "OK", .variant = CS_BADGE_SUCCESS},
 * s);
 *
 * Renders inline (no trailing newline) so badges can sit next to other text,
 * e.g. a doctor check row or a table cell. A leading marker glyph (✓ ⚠ ✗ ℹ •)
 * reinforces the variant; it degrades to ASCII and the label stays colored even
 * when glyphs are off.
 */

#pragma once

#include "../surface/surface.h"

typedef enum cs_badge_variant {
  CS_BADGE_NEUTRAL = 0,
  CS_BADGE_INFO,
  CS_BADGE_SUCCESS,
  CS_BADGE_WARNING,
  CS_BADGE_ERROR,
} cs_badge_variant_t;

typedef struct cs_badge {
  const char *text;
  cs_badge_variant_t variant;
  bool no_marker;  // omit the leading glyph, render just the bracketed label
} cs_badge_t;

void cs_badge_render(const cs_badge_t *badge, cs_surface_t *s);
