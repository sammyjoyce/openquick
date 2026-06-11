/*
 * cs_badge — inline status label. See cs_badge.h.
 */

#include "cs_badge.h"

#include "cs_glyphs.h"

static cs_role_t cs_badge_role(cs_badge_variant_t variant) {
  switch (variant) {
  case CS_BADGE_INFO:
    return CS_ROLE_INFO;
  case CS_BADGE_SUCCESS:
    return CS_ROLE_SUCCESS;
  case CS_BADGE_WARNING:
    return CS_ROLE_WARNING;
  case CS_BADGE_ERROR:
    return CS_ROLE_ERROR;
  case CS_BADGE_NEUTRAL:
  default:
    return CS_ROLE_MUTED;
  }
}

static const char *cs_badge_marker(cs_badge_variant_t variant, bool unicode) {
  switch (variant) {
  case CS_BADGE_INFO:
    return cs_glyph_info(unicode);
  case CS_BADGE_SUCCESS:
    return cs_glyph_check(unicode);
  case CS_BADGE_WARNING:
    return cs_glyph_warning(unicode);
  case CS_BADGE_ERROR:
    return cs_glyph_cross(unicode);
  case CS_BADGE_NEUTRAL:
  default:
    return cs_glyph_bullet(unicode);
  }
}

void cs_badge_render(const cs_badge_t *badge, cs_surface_t *s) {
  if (!badge || !s || !badge->text) {
    return;
  }
  cs_caps_t caps = cs_surface_caps(s);
  cs_role_t role = cs_badge_role(badge->variant);

  cs_surface_set_role(s, role);
  cs_surface_set_attr(s, CS_ATTR_BOLD);
  cs_surface_write(s, "[");
  if (!badge->no_marker) {
    cs_surface_write(s, cs_badge_marker(badge->variant, caps.unicode));
    cs_surface_write(s, " ");
  }
  cs_surface_write(s, badge->text);
  cs_surface_write(s, "]");
  cs_surface_reset(s);
}
