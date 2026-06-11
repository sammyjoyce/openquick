/*
 * cs_note — callout block. See cs_note.h.
 */

#include "cs_note.h"

#include "../ui/text_layout.h"
#include "cs_glyphs.h"

static cs_role_t cs_note_role(cs_note_variant_t variant) {
  switch (variant) {
  case CS_NOTE_SUCCESS:
    return CS_ROLE_SUCCESS;
  case CS_NOTE_WARNING:
    return CS_ROLE_WARNING;
  case CS_NOTE_ERROR:
    return CS_ROLE_ERROR;
  case CS_NOTE_INFO:
  default:
    return CS_ROLE_INFO;
  }
}

typedef struct {
  cs_surface_t *surface;
  const char *gutter;
  cs_role_t gutter_role;
  cs_role_t text_role;
} cs_note_emit_ctx_t;

static void cs_note_gutter(cs_note_emit_ctx_t *ctx) {
  cs_surface_set_role(ctx->surface, ctx->gutter_role);
  cs_surface_write(ctx->surface, ctx->gutter);
  cs_surface_reset(ctx->surface);
  cs_surface_write(ctx->surface, " ");
}

static bool cs_note_emit_line(void *user, const char *bytes, size_t byte_count,
                              int columns) {
  (void)columns;
  cs_note_emit_ctx_t *ctx = user;
  cs_note_gutter(ctx);
  cs_surface_set_role(ctx->surface, ctx->text_role);
  cs_surface_write_n(ctx->surface, bytes, byte_count);
  cs_surface_reset(ctx->surface);
  cs_surface_newline(ctx->surface);
  return true;
}

void cs_note_render(const cs_note_t *note, cs_surface_t *s) {
  if (!note || !s) {
    return;
  }
  cs_caps_t caps = cs_surface_caps(s);
  cs_role_t role = cs_note_role(note->variant);
  size_t width = note->width ? note->width : cs_surface_width(s);
  if (width == 0) {
    width = 80;
  }
  // content = width minus the gutter glyph and its trailing space.
  int content = (int)width - 2;
  if (content < 1) {
    content = 1;
  }

  cs_note_emit_ctx_t ctx = {.surface = s,
                            .gutter = cs_glyph_gutter(caps.unicode),
                            .gutter_role = role,
                            .text_role = CS_ROLE_TEXT};

  if (note->title && note->title[0]) {
    cs_note_gutter(&ctx);
    cs_surface_set_role(s, role);
    cs_surface_set_attr(s, CS_ATTR_BOLD);
    cs_surface_write(s, note->title);
    cs_surface_reset(s);
    cs_surface_newline(s);
  }

  if (note->body && note->body[0]) {
    app_text_wrap_utf8(note->body, content, 0, 0, cs_note_emit_line, &ctx);
  }
}
