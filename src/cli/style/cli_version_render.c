/*
 * Styled version rendering. See cli_version_render.h.
 */

#include "cli_version_render.h"

#include "cli_layout.h"

#ifndef APP_GIT_COMMIT
#define APP_GIT_COMMIT "unknown"
#endif
#ifndef APP_BUILD_DATE
#define APP_BUILD_DATE "unknown"
#endif

static void version_meta_row(app_cli_render_ctx_t *ctx, const char *key,
                             const char *value) {
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_COMMENT, key);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_DESCRIPTION, value);
  app_cli_newline(ctx);
}

void app_cli_render_version(const app_config_t *config, FILE *out,
                            const char *program_name) {
  app_cli_render_ctx_t ctx;
  app_cli_render_ctx_init(&ctx, config, out ? out : stdout, program_name, NULL);

  app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_PROGRAM, APP_NAME);
  app_cli_write(&ctx, " ");
  app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_BASE, APP_VERSION);
  app_cli_newline(&ctx);

  version_meta_row(&ctx, "commit:  ", APP_GIT_COMMIT);
  version_meta_row(&ctx, "built:   ", APP_BUILD_DATE);
#ifdef ENABLE_TUI
  version_meta_row(&ctx, "tui:     ", "enabled (curses)");
#else
  version_meta_row(&ctx, "tui:     ", "disabled");
#endif

  app_cli_render_ctx_deinit(&ctx);
}
