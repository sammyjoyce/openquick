/*
 * CLI layout: a render context (terminal + compiled styles + clamped width) and
 * text-layout primitives (display width, repeats, indentation, word-wrap with
 * hanging indent, section titles). The help/error/version renderers build on
 * these.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "../../core/config.h"
#include "cli_sgr.h"
#include "cli_term.h"
#include "cli_theme.h"

#define APP_CLI_WIDTH_MIN 40
#define APP_CLI_WIDTH_MAX 120

typedef struct app_cli_render_ctx {
  app_cli_term_t term;
  app_cli_styles_t styles;
  size_t width;  // clamped to [APP_CLI_WIDTH_MIN, APP_CLI_WIDTH_MAX]
  const char *program_name;
  bool styled;
} app_cli_render_ctx_t;

// Initialize a render context for `stream`. Resolves terminal capabilities,
// theme mode (APP_CLI_THEME=auto|dark|light, default auto with dark fallback),
// compiles styles, and clamps width. Returns ctx->styled. Always safe; falls
// back to plain output.
bool app_cli_render_ctx_init(app_cli_render_ctx_t *ctx,
                             const app_config_t *config, FILE *stream,
                             const char *program_name,
                             const app_cli_term_opts_t *opts);

void app_cli_render_ctx_deinit(app_cli_render_ctx_t *ctx);

// Display width of a UTF-8 string in columns. (Counts codepoints; wide-char
// handling is a later refinement.)
size_t app_cli_text_width(const char *s);

// Display width of the first `n` bytes of a UTF-8 string.
size_t app_cli_text_width_n(const char *s, size_t n);

// Plain writers.
void app_cli_write(app_cli_render_ctx_t *ctx, const char *s);
void app_cli_write_n(app_cli_render_ctx_t *ctx, const char *s, size_t n);
void app_cli_newline(app_cli_render_ctx_t *ctx);
void app_cli_repeat(app_cli_render_ctx_t *ctx, char ch, size_t count);

// Styled writers keyed by token.
void app_cli_write_token(app_cli_render_ctx_t *ctx,
                         app_cli_color_token_id token, const char *text);

// Emit an uppercase section title (e.g. "USAGE") in the Title style.
void app_cli_section_title(app_cli_render_ctx_t *ctx, const char *title);

// Wrap `text` assuming the cursor is already at column `start_col`.
// Continuation lines are indented to `cont_indent`. Emits a trailing newline.
void app_cli_wrap_from(app_cli_render_ctx_t *ctx, const app_cli_style_t *style,
                       const char *text, size_t start_col, size_t cont_indent);
