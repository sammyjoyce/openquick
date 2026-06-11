/*
 * cs_list — bulleted/numbered list. See cs_list.h.
 */

#include "cs_list.h"

#include <stdio.h>

#include "../ui/text_layout.h"
#include "cs_glyphs.h"

typedef struct {
  cs_surface_t *surface;
  cs_role_t text_role;
  int marker_cols;  // columns to indent continuation lines under the text
  int line_index;   // 0 for the first line of an item
} cs_list_emit_ctx_t;

static bool cs_list_emit_line(void *user, const char *bytes, size_t byte_count,
                              int columns) {
  (void)columns;
  cs_list_emit_ctx_t *ctx = user;
  if (ctx->line_index > 0) {
    if (ctx->marker_cols > 0) {
      cs_surface_repeat(ctx->surface, " ", (size_t)ctx->marker_cols);
    }
  }
  cs_surface_set_role(ctx->surface, ctx->text_role);
  cs_surface_write_n(ctx->surface, bytes, byte_count);
  cs_surface_reset(ctx->surface);
  cs_surface_newline(ctx->surface);
  ctx->line_index++;
  return true;
}

void cs_list_render(const cs_list_t *list, cs_surface_t *s) {
  if (!list || !s || !list->items || list->count == 0) {
    return;
  }
  cs_caps_t caps = cs_surface_caps(s);
  cs_role_t marker_role = cs_role_or_default(
      list->marker_role, list->marker_role_set, CS_ROLE_ACCENT);
  cs_role_t text_role =
      cs_role_or_default(list->text_role, list->text_role_set, CS_ROLE_TEXT);
  size_t width = list->width ? list->width : cs_surface_width(s);
  if (width == 0) {
    width = 80;
  }
  int start = list->start ? list->start : 1;

  for (size_t i = 0; i < list->count; i++) {
    const char *item = list->items[i] ? list->items[i] : "";

    char marker[24];
    if (list->style == CS_LIST_NUMBERED) {
      snprintf(marker, sizeof(marker), "%d. ", start + (int)i);
    } else {
      snprintf(marker, sizeof(marker), "%s ", cs_glyph_bullet(caps.unicode));
    }
    int marker_cols = app_text_width_utf8(marker);

    cs_surface_set_role(s, marker_role);
    cs_surface_write(s, marker);
    cs_surface_reset(s);

    int content = (int)width - marker_cols;
    if (content < 1) {
      content = 1;
    }
    cs_list_emit_ctx_t ctx = {.surface = s,
                              .text_role = text_role,
                              .marker_cols = marker_cols,
                              .line_index = 0};
    app_text_wrap_utf8(item, content, 0, 0, cs_list_emit_line, &ctx);
    // An empty item still needs to terminate its row.
    if (ctx.line_index == 0) {
      cs_surface_newline(s);
    }
  }
}
