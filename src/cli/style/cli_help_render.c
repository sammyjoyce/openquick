/*
 * Styled help rendering. See cli_help_render.h. Iterates the same command/flag
 * tables the plain renderer used, so new commands and flags appear
 * automatically unless command metadata hides them from root help.
 */

#include "cli_help_render.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../../core/error.h"
#include "../opencli_contract.h"
#include "../option_meta.h"
#include "cli_layout.h"

#define APP_HELP_MAX_ROWS 64

typedef struct {
  char label[96];
  char desc[256];
} help_row_t;

static void help_row_set(help_row_t *row, const char *label, const char *desc,
                         const char *env_hint) {
  snprintf(row->label, sizeof(row->label), "%s", label ? label : "");
  if (env_hint && env_hint[0]) {
    snprintf(row->desc, sizeof(row->desc), "%s (env: %s)", desc ? desc : "",
             env_hint);
  } else {
    snprintf(row->desc, sizeof(row->desc), "%s", desc ? desc : "");
  }
}

// Render an aligned two-column row: styled label, padding, wrapped description.
static void help_render_row(app_cli_render_ctx_t *ctx,
                            app_cli_color_token_id label_token,
                            const help_row_t *row, size_t label_col) {
  const size_t left = 2;
  app_cli_repeat(ctx, ' ', left);
  app_cli_write_token(ctx, label_token, row->label);

  size_t labw = app_cli_text_width(row->label);
  size_t desc_start;
  if (labw + 1 > label_col) {
    app_cli_newline(ctx);
    app_cli_repeat(ctx, ' ', left + label_col);
    desc_start = left + label_col;
  } else {
    app_cli_repeat(ctx, ' ', label_col - labw);
    desc_start = left + label_col;
  }
  app_cli_wrap_from(
      ctx, app_cli_style(&ctx->styles, APP_CLI_COLOR_TOKEN_DESCRIPTION),
      row->desc, desc_start, left + label_col);
}

static void help_render_rows(app_cli_render_ctx_t *ctx, const help_row_t *rows,
                             size_t count) {
  size_t maxw = 0;
  for (size_t i = 0; i < count; i++) {
    size_t w = app_cli_text_width(rows[i].label);
    if (w > maxw) {
      maxw = w;
    }
  }
  size_t label_col = maxw + 2;
  if (label_col < 18) {
    label_col = 18;
  }
  size_t cap = ctx->width / 2;
  if (label_col > 42) {
    label_col = 42;
  }
  if (label_col > cap) {
    label_col = cap;
  }
  for (size_t i = 0; i < count; i++) {
    help_render_row(ctx, APP_CLI_COLOR_TOKEN_FLAG, &rows[i], label_col);
  }
}

// Gather the global option rows (builtins, boolean flags, value options).
static size_t help_gather_global_rows(help_row_t *rows, size_t cap,
                                      bool include_env) {
  size_t n = 0;

  size_t bcount = 0;
  const app_builtin_option_t *builtins = app_builtin_options(&bcount);
  for (size_t i = 0; i < bcount && n < cap; i++) {
    char label[96];
    if (!builtins[i].name) {
      continue;
    }
    app_option_format_label(label, sizeof(label), builtins[i].name,
                            builtins[i].alias, NULL, 0, APP_OPTION_LABEL_CLI);
    help_row_set(&rows[n++], label, builtins[i].description, NULL);
  }

  size_t fcount = 0;
  const app_flag_spec_t *flags = app_flag_table(&fcount);
  for (size_t i = 0; i < fcount && n < cap; i++) {
    const app_flag_spec_t *spec = &flags[i];
    char label[96];
    if (!spec->cli_long && !spec->cli_short) {
      continue;
    }
    app_option_format_label(label, sizeof(label), spec->cli_long,
                            spec->cli_short, NULL, 0, APP_OPTION_LABEL_CLI);
    const char *env = (include_env && spec->env_var && spec->env_var[0])
                          ? spec->env_var
                          : NULL;
    help_row_set(&rows[n++], label,
                 spec->description ? spec->description : "Boolean flag", env);
  }

  size_t gcount = 0;
  const app_global_value_option_t *globals = app_global_value_options(&gcount);
  for (size_t i = 0; i < gcount && n < cap; i++) {
    char label[96];
    if (!globals[i].name) {
      continue;
    }
    app_option_format_label(label, sizeof(label), globals[i].name,
                            globals[i].alias, globals[i].arguments,
                            globals[i].argument_count, APP_OPTION_LABEL_CLI);
    help_row_set(&rows[n++], label, globals[i].description, NULL);
  }

  return n;
}

static void help_render_usage_root(app_cli_render_ctx_t *ctx) {
  app_cli_section_title(ctx, "USAGE");
  app_cli_repeat(ctx, ' ', 2);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_PROGRAM, ctx->program_name);
  app_cli_write(ctx, " ");
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_DIMMED_ARGUMENT, "[options]");
  app_cli_write(ctx, " ");
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_COMMAND, "<command>");
  app_cli_write(ctx, " ");
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_DIMMED_ARGUMENT, "[arguments]");
  app_cli_newline(ctx);
  app_cli_newline(ctx);
}

static void help_render_commands(app_cli_render_ctx_t *ctx) {
  size_t count = 0;
  const app_command_t *commands = app_commands(&count);
  help_row_t rows[APP_HELP_MAX_ROWS];
  size_t n = 0;
  for (size_t i = 0; i < count && n < APP_HELP_MAX_ROWS; i++) {
    if (!app_command_is_visible(&commands[i])) {
      continue;
    }
    help_row_set(&rows[n++], commands[i].name,
                 commands[i].summary ? commands[i].summary : "", NULL);
  }
  app_cli_section_title(ctx, "COMMANDS");
  for (size_t i = 0; i < n; i++) {
    // command name column uses the Command token.
    help_render_row(ctx, APP_CLI_COLOR_TOKEN_COMMAND, &rows[i], 18);
  }
  app_cli_newline(ctx);
}

static void help_render_header(app_cli_render_ctx_t *ctx) {
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_PROGRAM, APP_NAME);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_DESCRIPTION, " - ");
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_DESCRIPTION, APP_DESCRIPTION);
  app_cli_newline(ctx);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_COMMENT, "Version " APP_VERSION);
  app_cli_newline(ctx);
  app_cli_newline(ctx);
}

static void help_render_options(app_cli_render_ctx_t *ctx, bool include_env) {
  help_row_t rows[APP_HELP_MAX_ROWS];
  size_t n = help_gather_global_rows(rows, APP_HELP_MAX_ROWS, include_env);
  app_cli_section_title(ctx, "OPTIONS");
  help_render_rows(ctx, rows, n);
  app_cli_newline(ctx);
}

static void help_render_concise(app_cli_render_ctx_t *ctx) {
  help_render_header(ctx);
  help_render_usage_root(ctx);
  help_render_commands(ctx);
  help_render_options(ctx, false);

  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_COMMENT, "Run ");
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_PROGRAM, ctx->program_name);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_COMMENT,
                      " --help for full documentation.");
  app_cli_newline(ctx);
}

static void help_render_plain_block(app_cli_render_ctx_t *ctx,
                                    const char *const *lines, size_t count) {
  for (size_t i = 0; i < count; i++) {
    app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_DESCRIPTION, lines[i]);
    app_cli_newline(ctx);
  }
}

static void help_render_verbose(app_cli_render_ctx_t *ctx) {
  help_render_header(ctx);
  help_render_usage_root(ctx);

  app_cli_section_title(ctx, "DESCRIPTION");
  static const char *const desc_lines[] = {
      ("  " APP_DESCRIPTION),
      "  It provides a solid foundation with error handling, configuration,",
      "  and testing baked in.",
  };
  help_render_plain_block(ctx, desc_lines,
                          sizeof(desc_lines) / sizeof(desc_lines[0]));
  app_cli_newline(ctx);

  help_render_commands(ctx);
  help_render_options(ctx, true);

  app_cli_section_title(ctx, "ENVIRONMENT");
  // Render the same canonical environment table the OpenCLI contract publishes
  // (app_opencli_environment_docs), so this styled help, the plain help, and
  // the machine contract all document one identical set of variables.
  size_t env_count = 0;
  const app_opencli_metadata_field_t *env_docs =
      app_opencli_environment_docs(&env_count);
  for (size_t i = 0; i < env_count; i++) {
    char line[160];
    snprintf(line, sizeof(line), "  %-20s%s", env_docs[i].name,
             env_docs[i].description ? env_docs[i].description : "");
    app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_DESCRIPTION, line);
    app_cli_newline(ctx);
  }
  app_cli_newline(ctx);

  app_cli_section_title(ctx, "CONFIGURATION");
  static const char *const cfg_lines[] = {
      "  Loaded from (first hit wins):",
      "    - the path passed to --config",
      "    - $APP_CONFIG_PATH",
      "    - ~/.config/" APP_NAME "/config.json",
      "    - /etc/" APP_NAME "/config.json",
  };
  help_render_plain_block(ctx, cfg_lines,
                          sizeof(cfg_lines) / sizeof(cfg_lines[0]));
  app_cli_newline(ctx);

  app_cli_section_title(ctx, "EXAMPLES");
  app_cli_repeat(ctx, ' ', 2);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_CODEBLOCK, ctx->program_name);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_CODEBLOCK, " hello Alice");
  app_cli_newline(ctx);
  app_cli_repeat(ctx, ' ', 2);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_CODEBLOCK, ctx->program_name);
  app_cli_write_token(ctx, APP_CLI_COLOR_TOKEN_CODEBLOCK, " --json info");
  app_cli_newline(ctx);
  app_cli_newline(ctx);

  app_cli_section_title(ctx, "EXIT CODES");
  size_t error_count = 0;
  const app_error_info_t *errors = app_error_table(&error_count);
  for (size_t i = 0; i < error_count; i++) {
    char code[8];
    snprintf(code, sizeof(code), "%d", (int)errors[i].code);
    help_row_t row;
    help_row_set(&row, code, errors[i].description, NULL);
    help_render_row(ctx, APP_CLI_COLOR_TOKEN_FLAG_DEFAULT, &row, 6);
  }
}

void app_cli_render_root_help(const app_config_t *config, FILE *out,
                              const char *program_name, bool verbose) {
  app_cli_render_ctx_t ctx;
  app_cli_term_opts_t opts = {.is_error = !verbose};
  app_cli_render_ctx_init(&ctx, config, out, program_name, &opts);

  if (verbose) {
    help_render_verbose(&ctx);
  } else {
    help_render_concise(&ctx);
  }

  app_cli_render_ctx_deinit(&ctx);
}

void app_cli_render_command_help(const app_config_t *config, FILE *out,
                                 const char *program_name,
                                 const app_command_t *command) {
  if (!command) {
    app_cli_render_root_help(config, out, program_name, false);
    return;
  }

  app_cli_render_ctx_t ctx;
  app_cli_render_ctx_init(&ctx, config, out, program_name, NULL);

  // Header: "<command> - <summary>"
  app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_COMMAND, command->name);
  app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_DESCRIPTION, " - ");
  app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_DESCRIPTION,
                      command->summary ? command->summary : "Command");
  app_cli_newline(&ctx);
  app_cli_newline(&ctx);

  // Usage line.
  app_cli_section_title(&ctx, "USAGE");
  app_cli_repeat(&ctx, ' ', 2);
  app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_PROGRAM, ctx.program_name);
  app_cli_write(&ctx, " ");
  app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_COMMAND, command->name);
  for (size_t i = 0; i < command->argument_count; i++) {
    const app_command_arg_t *arg = &command->arguments[i];
    char buf[96];
    const char *fmt = arg->required ? " <%s>%s" : " [%s]%s";
    snprintf(buf, sizeof(buf), fmt, arg->name,
             arg->arity_maximum == APP_ARG_ARITY_UNBOUNDED ? "..." : "");
    app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_DIMMED_ARGUMENT, buf);
  }
  app_cli_newline(&ctx);
  app_cli_newline(&ctx);

  if (command->option_count > 0) {
    help_row_t rows[APP_HELP_MAX_ROWS];
    size_t n = 0;
    for (size_t i = 0; i < command->option_count && n < APP_HELP_MAX_ROWS;
         i++) {
      char label[96];
      app_option_format_label(label, sizeof(label), command->options[i].name,
                              NULL, NULL, 0, APP_OPTION_LABEL_CLI);
      help_row_set(&rows[n++], label, command->options[i].description, NULL);
    }
    app_cli_section_title(&ctx, "OPTIONS");
    help_render_rows(&ctx, rows, n);
    app_cli_newline(&ctx);
  }

  if (command->argument_count > 0) {
    help_row_t rows[APP_HELP_MAX_ROWS];
    size_t n = 0;
    for (size_t i = 0; i < command->argument_count && n < APP_HELP_MAX_ROWS;
         i++) {
      help_row_set(&rows[n++], command->arguments[i].name,
                   command->arguments[i].description
                       ? command->arguments[i].description
                       : "",
                   NULL);
    }
    app_cli_section_title(&ctx, "ARGUMENTS");
    help_render_rows(&ctx, rows, n);
    app_cli_newline(&ctx);
  }

  if (command->example_count > 0) {
    app_cli_section_title(&ctx, "EXAMPLES");
    for (size_t i = 0; i < command->example_count; i++) {
      app_cli_repeat(&ctx, ' ', 2);
      app_cli_write_token(&ctx, APP_CLI_COLOR_TOKEN_CODEBLOCK,
                          command->examples[i]);
      app_cli_newline(&ctx);
    }
  }

  app_cli_render_ctx_deinit(&ctx);
}
