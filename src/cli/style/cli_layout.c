/*
 * CLI layout primitives. See cli_layout.h.
 */

#include "cli_layout.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "../../style/cs_theme.h"
#include "../../ui/text_layout.h"

bool app_cli_render_ctx_init(app_cli_render_ctx_t *ctx,
                             const app_config_t *config, FILE *stream,
                             const char *program_name,
                             const app_cli_term_opts_t *opts) {
  if (!ctx) {
    return false;
  }
  *ctx = (app_cli_render_ctx_t){0};
  ctx->program_name =
      (program_name && program_name[0]) ? program_name : APP_NAME;

  app_cli_term_opts_t local = opts ? *opts : (app_cli_term_opts_t){0};
  ctx->styled = app_cli_term_init(&ctx->term, stream, config, &local);

  size_t width = ctx->term.width ? ctx->term.width : 80;
  if (width < APP_CLI_WIDTH_MIN) {
    width = APP_CLI_WIDTH_MIN;
  }
  if (width > APP_CLI_WIDTH_MAX) {
    width = APP_CLI_WIDTH_MAX;
  }
  ctx->width = width;

  if (ctx->styled) {
    app_cli_color_scheme_t scheme = *app_cli_theme_default_scheme();
    app_cli_theme_apply_env_overrides(&scheme);
    const app_cli_theme_mode_id mode = cs_theme_mode_resolve() == CS_MODE_LIGHT
                                           ? APP_CLI_THEME_MODE_LIGHT
                                           : APP_CLI_THEME_MODE_DARK;
    app_cli_styles_compile(&ctx->styles, &scheme, mode, ctx->term.profile,
                           ctx->term.color_count);
  }
  return ctx->styled;
}

void app_cli_render_ctx_deinit(app_cli_render_ctx_t *ctx) {
  if (!ctx) {
    return;
  }
  app_cli_term_deinit(&ctx->term);
  *ctx = (app_cli_render_ctx_t){0};
}

size_t app_cli_text_width(const char *s) {
  const int width = app_text_width_utf8(s);
  return width > 0 ? (size_t)width : 0;
}

size_t app_cli_text_width_n(const char *s, size_t n) {
  const int width = app_text_width_utf8_n(s, n);
  return width > 0 ? (size_t)width : 0;
}

void app_cli_write_n(app_cli_render_ctx_t *ctx, const char *s, size_t n) {
  if (ctx) {
    app_cli_term_write(&ctx->term, s, n);
  }
}

void app_cli_write(app_cli_render_ctx_t *ctx, const char *s) {
  if (ctx && s) {
    app_cli_term_puts(&ctx->term, s);
  }
}

void app_cli_newline(app_cli_render_ctx_t *ctx) {
  app_cli_write_n(ctx, "\n", 1);
}

void app_cli_repeat(app_cli_render_ctx_t *ctx, char ch, size_t count) {
  char buf[64];
  memset(buf, ch, sizeof(buf));
  while (count > 0) {
    size_t chunk = count < sizeof(buf) ? count : sizeof(buf);
    app_cli_write_n(ctx, buf, chunk);
    count -= chunk;
  }
}

void app_cli_write_token(app_cli_render_ctx_t *ctx,
                         app_cli_color_token_id token, const char *text) {
  if (!ctx || !text) {
    return;
  }
  app_cli_write_styled(&ctx->term, app_cli_style(&ctx->styles, token), text);
}

void app_cli_section_title(app_cli_render_ctx_t *ctx, const char *title) {
  if (!ctx || !title) {
    return;
  }
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_TITLE, title);
  app_cli_newline(ctx);
}

void app_cli_wrap_from(app_cli_render_ctx_t *ctx, const app_cli_style_t *style,
                       const char *text, size_t start_col, size_t cont_indent) {
  if (!ctx) {
    return;
  }
  if (!text || !text[0]) {
    app_cli_newline(ctx);
    return;
  }
  size_t width = ctx->width;
  size_t col = start_col;
  bool first_on_line = true;
  bool style_open = false;  // a style run is open on the current line

  const char *word = text;
  while (*word) {
    if (*word == ' ') {
      word++;
      continue;
    }
    const char *end = word;
    while (*end && *end != ' ') {
      end++;
    }
    size_t wcols = app_cli_text_width_n(word, (size_t)(end - word));

    if (!first_on_line && col + 1 + wcols > width) {
      if (style_open) {
        app_cli_style_end(&ctx->term);
        style_open = false;
      }
      app_cli_newline(ctx);
      app_cli_repeat(ctx, ' ', cont_indent);  // indent stays unstyled
      col = cont_indent;
      first_on_line = true;
    }
    if (!style_open) {
      app_cli_style_begin(&ctx->term, style);
      style_open = true;
    }
    if (!first_on_line) {
      app_cli_write_n(ctx, " ", 1);  // interior space inherits the open style
      col += 1;
    }
    app_cli_write_n(ctx, word, (size_t)(end - word));
    col += wcols;
    first_on_line = false;
    word = end;
  }
  if (style_open) {
    app_cli_style_end(&ctx->term);
  }
  app_cli_newline(ctx);
}
