/*
 * cs_rule — horizontal divider. See cs_rule.h.
 */

#include "cs_rule.h"

#include "../ui/text_layout.h"
#include "cs_glyphs.h"

void cs_rule_render(const cs_rule_t *rule, cs_surface_t *s) {
  if (!rule || !s) {
    return;
  }
  cs_caps_t caps = cs_surface_caps(s);
  size_t width = rule->width ? rule->width : cs_surface_width(s);
  if (width == 0) {
    width = 80;
  }
  const char *glyph = rule->glyph ? rule->glyph : cs_glyph_hline(caps.unicode);
  cs_role_t line_role =
      cs_role_or_default(rule->role, rule->role_set, CS_ROLE_BORDER);
  cs_role_t label_role =
      cs_role_or_default(rule->label_role, rule->label_role_set, CS_ROLE_TITLE);

  if (!rule->label || !rule->label[0]) {
    cs_surface_set_role(s, line_role);
    cs_surface_repeat(s, glyph, width);
    cs_surface_reset(s);
    cs_surface_newline(s);
    return;
  }

  // " label " centered between two glyph runs.
  int label_cols = app_text_width_utf8(rule->label) + 2;  // surrounding spaces
  if (label_cols <= 0 || (size_t)label_cols >= width) {
    cs_surface_styled(s, label_role, CS_ATTR_BOLD, rule->label);
    cs_surface_newline(s);
    return;
  }
  size_t remaining = width - (size_t)label_cols;
  size_t left = remaining / 2;
  size_t right = remaining - left;

  cs_surface_set_role(s, line_role);
  cs_surface_repeat(s, glyph, left);
  cs_surface_reset(s);

  cs_surface_set_role(s, label_role);
  cs_surface_set_attr(s, CS_ATTR_BOLD);
  cs_surface_write(s, " ");
  cs_surface_write(s, rule->label);
  cs_surface_write(s, " ");
  cs_surface_reset(s);

  cs_surface_set_role(s, line_role);
  cs_surface_repeat(s, glyph, right);
  cs_surface_reset(s);
  cs_surface_newline(s);
}
