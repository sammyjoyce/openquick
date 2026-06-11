/*
 * Help text display.
 *
 * Both concise and verbose help iterate over the command and flag tables in
 * commands.c / config.c, so adding a command or flag automatically shows up
 * without editing this file unless command metadata hides it from root help.
 *
 * When the CLI styling layer is compiled in (APP_ENABLE_CLI_STYLE), the public
 * entry points delegate to the styled renderers in src/cli/style. Otherwise a
 * self-contained plain-text implementation is used.
 */

#include "help.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../core/config.h"
#include "../core/types.h"
#include "commands.h"

#ifdef APP_ENABLE_CLI_STYLE

#include "style/cli_help_render.h"

void app_print_concise_help_ex(const char *program_name,
                               const app_config_t *config) {
  app_cli_render_root_help(config, stdout, program_name, false);
}

void app_print_verbose_usage_ex(const char *program_name,
                                const app_config_t *config) {
  app_cli_render_root_help(config, stdout, program_name, true);
}

void app_print_command_help_ex(const char *program_name,
                               const app_config_t *config,
                               const app_command_t *command) {
  app_cli_render_command_help(config, stdout, program_name, command);
}

#else /* !APP_ENABLE_CLI_STYLE : plain-text fallback */

#include "../utils/colors.h"
#include "../utils/logging.h"
#include "opencli_contract.h"
#include "option_meta.h"

static const char *program_or_default(const char *program_name) {
  if (program_name == nullptr || program_name[0] == '\0') {
    LOG_ERROR("Invalid program name");
    return APP_NAME;
  }
  return program_name;
}

static void print_commands_block(void) {
  size_t count = 0;
  const app_command_t *commands = app_commands(&count);
  printf("Commands:\n");
  for (size_t i = 0; i < count; i++) {
    if (!app_command_is_visible(&commands[i])) {
      continue;
    }
    printf("  %-16s%s\n", commands[i].name,
           commands[i].summary ? commands[i].summary : "");
  }
}

static void print_builtin_options(void) {
  size_t count = 0;
  const app_builtin_option_t *options = app_builtin_options(&count);
  for (size_t i = 0; i < count; i++) {
    char left[64];
    app_option_format_label(left, sizeof(left), options[i].name,
                            options[i].alias, NULL, 0, APP_OPTION_LABEL_CLI);
    printf("  %-20s%s\n", left, options[i].description);
  }
}

static void print_global_value_options(void) {
  size_t count = 0;
  const app_global_value_option_t *options = app_global_value_options(&count);
  for (size_t i = 0; i < count; i++) {
    char left[64];
    app_option_format_label(left, sizeof(left), options[i].name,
                            options[i].alias, options[i].arguments,
                            options[i].argument_count, APP_OPTION_LABEL_CLI);
    printf("  %-20s%s\n", left, options[i].description);
  }
}

static void print_flag_options(bool include_env_hints) {
  size_t count = 0;
  const app_flag_spec_t *flags = app_flag_table(&count);
  for (size_t i = 0; i < count; i++) {
    const app_flag_spec_t *spec = &flags[i];
    char left[64];
    if (!spec->cli_short && !spec->cli_long) {
      continue;
    }
    app_option_format_label(left, sizeof(left), spec->cli_long, spec->cli_short,
                            NULL, 0, APP_OPTION_LABEL_CLI);
    const char *description =
        spec->description ? spec->description : "Boolean flag";
    if (include_env_hints && spec->env_var && spec->env_var[0] != '\0') {
      printf("  %-20s%s (env: %s)\n", left, description, spec->env_var);
    } else {
      printf("  %-20s%s\n", left, description);
    }
  }
}

void app_print_concise_help_ex(const char *program_name,
                               const app_config_t *config) {
  (void)config;
  program_name = program_or_default(program_name);

  printf("%s - %s [version %s]\n\n", APP_NAME, APP_DESCRIPTION, APP_VERSION);
  printf("Usage: %s [options] <command> [arguments]\n\n", program_name);

  print_commands_block();
  printf("\n");

  printf("Options:\n");
  print_builtin_options();
  print_flag_options(false);
  print_global_value_options();
  printf("\n");

  printf("For more options, run %s --help\n", program_name);
}

void app_print_verbose_usage_ex(const char *program_name,
                                const app_config_t *config) {
  (void)config;
  program_name = program_or_default(program_name);

  const char *bold = app_use_colors(nullptr) ? APP_COLOR_BOLD : "";
  const char *reset = app_use_colors(nullptr) ? APP_COLOR_RESET : "";

  printf("%s%s - %s%s\n", bold, APP_NAME, APP_DESCRIPTION, reset);
  printf("Version %s\n\n", APP_VERSION);

  printf("%sUSAGE%s\n", bold, reset);
  printf("  %s [options] <command> [arguments]\n\n", program_name);

  printf("%sDESCRIPTION%s\n", bold, reset);
  printf("  %s\n\n", APP_DESCRIPTION);

  printf("%sCOMMANDS%s\n", bold, reset);
  size_t cmd_count = 0;
  const app_command_t *commands = app_commands(&cmd_count);
  for (size_t i = 0; i < cmd_count; i++) {
    if (!app_command_is_visible(&commands[i])) {
      continue;
    }
    printf("  %-18s%s\n", commands[i].name,
           commands[i].summary ? commands[i].summary : "");
  }
  printf("\n");

  printf("%sOPTIONS%s\n", bold, reset);
  print_builtin_options();
  print_flag_options(true);
  print_global_value_options();
  printf("\n");

  printf("%sENVIRONMENT%s\n", bold, reset);
  // Render the same canonical environment table the OpenCLI contract publishes,
  // so this plain help and the machine contract document identical variables.
  size_t env_count = 0;
  const app_opencli_metadata_field_t *env_docs =
      app_opencli_environment_docs(&env_count);
  for (size_t i = 0; i < env_count; i++) {
    printf("  %-20s%s\n", env_docs[i].name,
           env_docs[i].description ? env_docs[i].description : "");
  }
  printf("\n");

  printf("%sEXIT CODES%s\n", bold, reset);
  size_t error_count = 0;
  const app_error_info_t *errors = app_error_table(&error_count);
  for (size_t i = 0; i < error_count; i++) {
    printf("  %-4d %s\n", (int)errors[i].code, errors[i].description);
  }
}

void app_print_command_help_ex(const char *program_name,
                               const app_config_t *config,
                               const app_command_t *command) {
  (void)config;
  program_name = program_or_default(program_name);
  if (!command) {
    app_print_concise_help_ex(program_name, NULL);
    return;
  }

  printf("%s - %s\n\n", command->name,
         command->summary ? command->summary : "Command");
  printf("Usage: %s %s", program_name, command->name);
  for (size_t i = 0; i < command->argument_count; i++) {
    const app_command_arg_t *arg = &command->arguments[i];
    printf(arg->required ? " <%s>" : " [%s]", arg->name);
    if (arg->arity_maximum == APP_ARG_ARITY_UNBOUNDED) {
      printf("...");
    }
  }
  printf("\n\n");

  if (command->option_count > 0) {
    printf("Options:\n");
    for (size_t i = 0; i < command->option_count; i++) {
      char left[64];
      app_option_format_label(left, sizeof(left), command->options[i].name,
                              NULL, NULL, 0, APP_OPTION_LABEL_CLI);
      printf("  %-20s%s\n", left, command->options[i].description);
    }
    printf("\n");
  }

  if (command->argument_count > 0) {
    printf("Arguments:\n");
    for (size_t i = 0; i < command->argument_count; i++) {
      const app_command_arg_t *arg = &command->arguments[i];
      printf("  %-20s%s\n", arg->name,
             arg->description ? arg->description : "");
    }
    printf("\n");
  }

  if (command->example_count > 0) {
    printf("Examples:\n");
    for (size_t i = 0; i < command->example_count; i++) {
      printf("  %s\n", command->examples[i]);
    }
  }
}

#endif /* APP_ENABLE_CLI_STYLE */

/* Thin wrappers preserving the original API (both build configurations). */
void app_print_concise_help(const char *program_name) {
  app_print_concise_help_ex(program_name, nullptr);
}

void app_print_verbose_usage(const char *program_name) {
  app_print_verbose_usage_ex(program_name, nullptr);
}

void app_print_command_help(const char *program_name,
                            const app_command_t *command) {
  app_print_command_help_ex(program_name, nullptr, command);
}
