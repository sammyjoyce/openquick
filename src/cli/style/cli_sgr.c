/*
 * Style application. See cli_sgr.h.
 */

#include "cli_sgr.h"

static void app_cli_emit_color(app_cli_term_t *term, bool background,
                               const app_cli_resolved_color_t *color) {
  switch (color->kind) {
  case APP_CLI_RESOLVED_COLOR_RGB:
    app_cli_term_emit_truecolor(term, background, color->rgb);
    break;
  case APP_CLI_RESOLVED_COLOR_INDEXED:
    app_cli_term_emit_indexed(term, background, color->index);
    break;
  case APP_CLI_RESOLVED_COLOR_NONE:
  case APP_CLI_RESOLVED_COLOR_DEFAULT:
  default:
    break;
  }
}

void app_cli_style_begin(app_cli_term_t *term, const app_cli_style_t *style) {
  if (!term || !style || !term->style_enabled) {
    return;
  }

  static const app_cli_attr_bit attr_bits[] = {
      APP_CLI_ATTR_BOLD, APP_CLI_ATTR_DIM, APP_CLI_ATTR_UNDERLINE,
      APP_CLI_ATTR_ITALIC};
  for (size_t i = 0; i < sizeof(attr_bits) / sizeof(attr_bits[0]); i++) {
    if (style->attrs & attr_bits[i]) {
      app_cli_term_emit_attr(term, attr_bits[i]);
    }
  }

  app_cli_emit_color(term, false, &style->fg);
  app_cli_emit_color(term, true, &style->bg);
}

void app_cli_style_end(app_cli_term_t *term) {
  if (!term || !term->style_enabled) {
    return;
  }
  app_cli_term_emit_reset(term);
}

void app_cli_write_styled(app_cli_term_t *term, const app_cli_style_t *style,
                          const char *text) {
  if (!text) {
    return;
  }
  app_cli_style_begin(term, style);
  app_cli_term_puts(term, text);
  app_cli_style_end(term);
}
