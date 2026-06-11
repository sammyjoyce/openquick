/*
 * cs_glyphs — capability-aware glyph picks shared by components.
 *
 * Header-only: every glyph getter takes the surface's `unicode` capability and
 * returns a UTF-8 box/marker glyph or a plain-ASCII fallback, so components
 * draw the same shapes on a UTF-8 terminal and a legacy/ASCII one without each
 * re-deriving the fallbacks.
 */

#pragma once

#include <stdbool.h>

static inline const char *cs_glyph_hline(bool unicode) {
  return unicode ? "\xe2\x94\x80" : "-";  // ─
}
static inline const char *cs_glyph_vline(bool unicode) {
  return unicode ? "\xe2\x94\x82" : "|";  // │
}
static inline const char *cs_glyph_gutter(bool unicode) {
  return unicode ? "\xe2\x94\x83" : "|";  // ┃ (heavy vertical)
}
static inline const char *cs_glyph_bullet(bool unicode) {
  return unicode ? "\xe2\x80\xa2" : "*";  // •
}
static inline const char *cs_glyph_check(bool unicode) {
  return unicode ? "\xe2\x9c\x93" : "+";  // ✓
}
static inline const char *cs_glyph_cross(bool unicode) {
  return unicode ? "\xe2\x9c\x97" : "x";  // ✗
}
static inline const char *cs_glyph_warning(bool unicode) {
  return unicode ? "\xe2\x9a\xa0" : "!";  // ⚠
}
static inline const char *cs_glyph_info(bool unicode) {
  return unicode ? "\xe2\x84\xb9" : "i";  // ℹ
}
static inline const char *cs_glyph_arrow(bool unicode) {
  return unicode ? "\xe2\x9d\xaf" : ">";  // ❯
}
static inline const char *cs_glyph_bar_full(bool unicode) {
  return unicode ? "\xe2\x96\x88" : "#";  // █
}
static inline const char *cs_glyph_bar_empty(bool unicode) {
  return unicode ? "\xe2\x96\x91" : ".";  // ░
}
