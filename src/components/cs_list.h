/*
 * cs_list — a bulleted or numbered list with word-wrap and hanging indent.
 *
 *   const char *items[] = {"First point", "A longer second point that wraps"};
 *   cs_list_render(&(cs_list_t){.items = items, .count = 2}, s);
 *
 * Each item wraps to the surface width; continuation lines align under the
 * item text (hanging indent), so long items stay readable. One block per call,
 * trailing newline after each item.
 */

#pragma once

#include "../surface/surface.h"

typedef enum cs_list_style {
  CS_LIST_BULLET = 0,
  CS_LIST_NUMBERED,
} cs_list_style_t;

typedef struct cs_list {
  const char *const *items;
  size_t count;
  cs_list_style_t style;
  cs_role_t marker_role;  // default: CS_ROLE_ACCENT
  cs_role_t text_role;    // default: CS_ROLE_TEXT
  bool marker_role_set;   // true when `marker_role` is intentional
  bool text_role_set;     // true when `text_role` is intentional
  size_t width;           // 0 => the surface width
  int start;              // numbered lists: first number (0 => 1)
} cs_list_t;

void cs_list_render(const cs_list_t *list, cs_surface_t *s);
