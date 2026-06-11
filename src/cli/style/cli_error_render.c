/*
 * Styled CLI error rendering. See cli_error_render.h.
 */

#include "cli_error_render.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cli_layout.h"

#define APP_CLI_ERROR_HEADER " ERROR "

// Title-case the first ASCII letter of `message` into `out`.
static void error_title_first(char *out, size_t out_size, const char *message) {
  snprintf(out, out_size, "%s", message ? message : "");
  for (char *p = out; *p; p++) {
    if (isalpha((unsigned char)*p)) {
      *p = (char)toupper((unsigned char)*p);
      break;
    }
    if (!isspace((unsigned char)*p)) {
      break;
    }
  }
}

void app_cli_render_error(const app_config_t *config, FILE *err,
                          const char *program_name, const char *message,
                          app_cli_error_kind_id kind) {
  app_cli_render_ctx_t ctx;
  app_cli_term_opts_t opts = {.is_error = true};
  app_cli_render_ctx_init(&ctx, config, err ? err : stderr, program_name,
                          &opts);

  char titled[256];
  error_title_first(titled, sizeof(titled), message);

  if (!ctx.styled) {
    // Plain fallback.
    fprintf(ctx.term.stream, "Error: %s\n", titled);
    if (kind == APP_CLI_ERROR_KIND_USAGE) {
      fprintf(ctx.term.stream, "Try %s --help for usage.\n", ctx.program_name);
    }
    app_cli_render_ctx_deinit(&ctx);
    return;
  }

  // Header block.
  app_cli_style_begin(&ctx.term, &ctx.styles.error_header);
  app_cli_write(&ctx, APP_CLI_ERROR_HEADER);
  app_cli_style_end(&ctx.term);

  const size_t header_w = app_cli_text_width(APP_CLI_ERROR_HEADER);
  const size_t indent = header_w + 1;
  app_cli_write(&ctx, " ");
  app_cli_wrap_from(
      &ctx, app_cli_style(&ctx.styles, APP_CLI_COLOR_TOKEN_ERROR_DETAILS),
      titled, indent, indent);

  if (kind == APP_CLI_ERROR_KIND_USAGE) {
    char hint[160];
    snprintf(hint, sizeof(hint), "Try %s --help for usage.", ctx.program_name);
    app_cli_repeat(&ctx, ' ', indent);
    app_cli_wrap_from(&ctx,
                      app_cli_style(&ctx.styles, APP_CLI_COLOR_TOKEN_COMMENT),
                      hint, indent, indent);
  }

  app_cli_render_ctx_deinit(&ctx);
}

void app_cli_render_error_code(const app_config_t *config, FILE *err,
                               const char *program_name, app_error code,
                               const char *detail, app_cli_error_kind_id kind) {
  char message[256];
  const char *base = app_strerror(code);
  if (detail && detail[0]) {
    snprintf(message, sizeof(message), "%s: %s", base, detail);
  } else {
    snprintf(message, sizeof(message), "%s", base);
  }
  app_cli_render_error(config, err, program_name, message, kind);
}
