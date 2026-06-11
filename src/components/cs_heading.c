/*
 * cs_heading — styled section heading. See cs_heading.h.
 */

#include "cs_heading.h"

#include <ctype.h>
#include <string.h>

#include "../ui/text_layout.h"
#include "cs_rule.h"

void cs_heading_render(const cs_heading_t *heading, cs_surface_t *s) {
  if (!heading || !s || !heading->text) {
    return;
  }
  cs_role_t role =
      cs_role_or_default(heading->role, heading->role_set, CS_ROLE_TITLE);

  cs_surface_set_role(s, role);
  cs_surface_set_attr(s, CS_ATTR_BOLD);
  if (heading->uppercase) {
    // Upper-case ASCII bytes only; multibyte UTF-8 sequences (bytes >= 0x80)
    // pass through untouched so this never corrupts a code point.
    char buf[256];
    size_t len = 0;
    for (const char *p = heading->text; *p; p++) {
      unsigned char c = (unsigned char)*p;
      buf[len++] = (c < 0x80) ? (char)toupper(c) : *p;
      if (len == sizeof(buf)) {
        cs_surface_write_n(s, buf, len);
        len = 0;
      }
    }
    if (len > 0) {
      cs_surface_write_n(s, buf, len);
    }
  } else {
    cs_surface_write(s, heading->text);
  }
  cs_surface_reset(s);
  cs_surface_newline(s);

  if (heading->underline) {
    int cols = app_text_width_utf8(heading->text);
    if (cols > 0) {
      cs_rule_render(
          &(cs_rule_t){.width = (size_t)cols, .role = role, .role_set = true},
          s);
    }
  }
}
