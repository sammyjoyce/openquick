/*
 * cs_spinner — frame-based activity indicator. See cs_spinner.h.
 */

#include "cs_spinner.h"

// Braille dot frames (UTF-8). Ten frames give a smooth spin.
static const char *const CS_SPINNER_DOTS_FRAMES[] = {
    "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
    "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7",
    "\xe2\xa0\x87", "\xe2\xa0\x8f",
};
static const char *const CS_SPINNER_LINE_FRAMES[] = {"|", "/", "-", "\\"};

int cs_spinner_frame_count(cs_spinner_style_t style, bool unicode) {
  if (style == CS_SPINNER_DOTS && unicode) {
    return (int)(sizeof(CS_SPINNER_DOTS_FRAMES) /
                 sizeof(CS_SPINNER_DOTS_FRAMES[0]));
  }
  return (int)(sizeof(CS_SPINNER_LINE_FRAMES) /
               sizeof(CS_SPINNER_LINE_FRAMES[0]));
}

const char *cs_spinner_frame(cs_spinner_style_t style, bool unicode,
                             int frame) {
  int count = cs_spinner_frame_count(style, unicode);
  int idx = ((frame % count) + count) % count;  // tolerate negative frames
  if (style == CS_SPINNER_DOTS && unicode) {
    return CS_SPINNER_DOTS_FRAMES[idx];
  }
  return CS_SPINNER_LINE_FRAMES[idx];
}

void cs_spinner_render(const cs_spinner_t *spinner, int frame,
                       cs_surface_t *s) {
  if (!spinner || !s) {
    return;
  }
  cs_caps_t caps = cs_surface_caps(s);
  cs_role_t role =
      cs_role_or_default(spinner->role, spinner->role_set, CS_ROLE_ACCENT);

  cs_surface_set_role(s, role);
  cs_surface_set_attr(s, CS_ATTR_BOLD);
  cs_surface_write(s, cs_spinner_frame(spinner->style, caps.unicode, frame));
  cs_surface_reset(s);

  if (spinner->label && spinner->label[0]) {
    cs_surface_write(s, " ");
    cs_surface_set_role(s, CS_ROLE_TEXT);
    cs_surface_write(s, spinner->label);
    cs_surface_reset(s);
  }
}
