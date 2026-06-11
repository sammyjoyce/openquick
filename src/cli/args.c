/*
 * Command-line argument parsing implementation.
 *
 * Bool flags are looked up against g_app_flag_table (see config.c) so adding a
 * new flag does not require a change here.
 */

#include "args.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/app_info.h"
#include "../core/config.h"
#include "../utils/logging.h"
#include "commands.h"
#include "help.h"
#include "option_meta.h"
#ifdef APP_ENABLE_CLI_STYLE
#include "style/cli_version_render.h"
#endif

typedef struct {
  const char *config_path;
  int command_index;
} app_global_args_t;

static bool app_args_try_bool_flag(const char *arg, app_config_t *config) {
  size_t count = 0;
  const app_flag_spec_t *specs = app_flag_table(&count);
  for (size_t i = 0; i < count; i++) {
    const app_flag_spec_t *spec = &specs[i];
    if (app_option_token_matches(arg, spec->cli_long, spec->cli_short)) {
      if (config) {
        if (app_config_set_flag(config, spec->id, true) != APP_SUCCESS) {
          return false;
        }
      }
      return true;
    }
  }
  return false;
}

static app_error app_scan_global_args(int argc, char *argv[],
                                      app_config_t *config,
                                      app_global_args_t *args) {
  CHECK_NULL(argv, APP_ERROR_INVALID_ARG);

  if (args) {
    args->config_path = NULL;
    args->command_index = -1;
  }

  for (int i = 1; i < argc; i++) {
    if (!argv[i] || argv[i][0] != '-') {
      if (args) {
        args->command_index = i;
      }
      break;
    }

    if (app_args_try_bool_flag(argv[i], config)) {
      continue;
    }

    const app_global_value_option_t *value_option =
        app_global_value_option_find(argv[i]);
    if (value_option) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires an argument\n", argv[i]);
        return APP_ERROR_MISSING_ARG;
      }

      const char *config_path = argv[++i];
      if (value_option->id == APP_GLOBAL_VALUE_OPTION_CONFIG && args) {
        args->config_path = config_path;
      }
      if (value_option->id == APP_GLOBAL_VALUE_OPTION_CONFIG && config) {
        const app_error err = app_config_set_config_file(config, config_path);
        if (err != APP_SUCCESS) {
          return err;
        }
      }
      continue;
    }

    fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
    fprintf(stderr, "Run '%s --help' for usage information\n", argv[0]);
    return APP_ERROR_UNKNOWN_OPTION;
  }

  return APP_SUCCESS;
}

app_error app_args_handle_immediate_exit(int argc, char *argv[]) {
  CHECK_NULL(argv, APP_ERROR_INVALID_ARG);

  for (int i = 1; i < argc; i++) {
    if (!argv[i]) {
      return APP_ERROR_INVALID_ARG;
    }

    if (strcmp(argv[i], "--") == 0) {
      break;
    }

    if (argv[i][0] != '-') {
      break;
    }

    const app_builtin_option_t *option = app_builtin_option_find(argv[i]);
    if (!option) {
      const app_global_value_option_t *value_option =
          app_global_value_option_find(argv[i]);
      if (value_option) {
        i++;
      }
      continue;
    }
    switch (option->id) {
    case APP_BUILTIN_OPTION_HELP:
      app_print_verbose_usage(argv[0]);
      exit(0);
    case APP_BUILTIN_OPTION_VERSION:
#ifdef APP_ENABLE_CLI_STYLE
      app_cli_render_version(nullptr, stdout, argv[0]);
#else
      const app_build_info_t *build = app_build_info();
      printf("%s %s\n", build->name, build->version);
      printf("%s\n", APP_DESCRIPTION);
      printf("Built with: Zig, C23\n");
      printf("TUI: %s\n", app_feature_compiled(APP_FEATURE_TUI)
                              ? "enabled via curses"
                              : "disabled");
#endif
      exit(0);
    }
  }

  return APP_SUCCESS;
}

app_error app_args_find_config_path(int argc, char *argv[],
                                    const char **config_path) {
  CHECK_NULL(config_path, APP_ERROR_INVALID_ARG);

  app_global_args_t args;
  const app_error err = app_scan_global_args(argc, argv, NULL, &args);
  if (err != APP_SUCCESS) {
    return err;
  }

  *config_path = args.config_path;
  return APP_SUCCESS;
}

app_error app_parse_args(int argc, char *argv[], app_config_t *config) {
  CHECK_NULL(argv, APP_ERROR_INVALID_ARG);
  CHECK_NULL(config, APP_ERROR_INVALID_ARG);

  // Store program name
  app_error err = app_config_set_program_name(config, argv[0]);
  if (err != APP_SUCCESS) {
    return err;
  }

  err = app_args_handle_immediate_exit(argc, argv);
  if (err != APP_SUCCESS) {
    return err;
  }

  app_global_args_t args;
  err = app_scan_global_args(argc, argv, config, &args);
  if (err != APP_SUCCESS) {
    return err;
  }

  // Set command and its arguments
  if (args.command_index >= 0) {
    err = app_config_set_command(config, argv[args.command_index]);
    if (err != APP_SUCCESS) {
      return err;
    }

    // Add remaining arguments as command arguments. A bare -- is preserved so
    // validation can treat subsequent flag-shaped tokens as positionals.
    for (int i = args.command_index + 1; i < argc; i++) {
      err = app_config_add_command_arg(config, argv[i]);
      if (err != APP_SUCCESS) {
        return err;
      }
    }
  }

  return APP_SUCCESS;
}
