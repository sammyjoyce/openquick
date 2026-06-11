/*
 * cs_heading — a styled section heading, optionally underlined with a rule.
 *
 *   cs_heading_render(&(cs_heading_t){.text = "Usage", .underline = true}, s);
 *
 * Emits the heading line (and the underline, if requested) with trailing
 * newlines. The CLI help renderer's "USAGE"/"COMMANDS" section titles are the
 * same idea; this is the reusable, theme-aware form.
 */

#pragma once

#include "../surface/surface.h"

typedef struct cs_heading {
  const char *text;
  cs_role_t role;  // heading color role (default: CS_ROLE_TITLE)
  bool role_set;   // true when `role` is intentional (allows CS_ROLE_TEXT)
  bool uppercase;  // upper-case the text (ASCII letters only)
  bool underline;  // draw a rule beneath, as wide as the heading
} cs_heading_t;

void cs_heading_render(const cs_heading_t *heading, cs_surface_t *s);
