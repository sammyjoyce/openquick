/*
 * cs_rule — a horizontal divider, optionally with a centered label.
 *
 *   cs_rule_render(&(cs_rule_t){0}, s);                  // a plain full-width
 * rule cs_rule_render(&(cs_rule_t){.label = "OPTIONS"}, s); // ──── OPTIONS
 * ────
 *
 * Renders on any surface (CLI stream or TUI window) and emits a trailing
 * newline. Zero-initialised fields fall back to sensible defaults.
 */

#pragma once

#include "../surface/surface.h"

typedef struct cs_rule {
  const char *label;     // optional; centered in the rule (NULL => plain line)
  cs_role_t role;        // line color role (default: CS_ROLE_BORDER)
  cs_role_t label_role;  // label color role (default: CS_ROLE_TITLE)
  bool role_set;         // true when `role` is intentional
  bool label_role_set;   // true when `label_role` is intentional
  size_t width;          // total columns (0 => the surface width)
  const char *glyph;     // line glyph (NULL => unicode "─" / ascii "-")
} cs_rule_t;

void cs_rule_render(const cs_rule_t *rule, cs_surface_t *s);
