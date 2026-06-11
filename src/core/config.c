/*
 * Configuration management implementation.
 *
 * Boolean flags are described once in g_app_flag_table and consumed by the
 * arg parser, the JSON loader, and the env loader. Adding a new bool means
 * touching one table row plus an enum entry in config.h.
 */

#include "config.h"

#include <errno.h>
#include <limits.h>

#include "config_json.h"
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#else
#include <io.h>
#define R_OK 4
#define access _access
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logging.h"

struct app_config {
  char *program_name;
  char *command;
  char *command_args[APP_MAX_COMMAND_ARGS];
  int command_arg_count;
  char *config_file;
  bool flags[APP_FLAG_COUNT];
};

// Single source of truth for boolean flags. The order matches the
// app_flag_id enum so direct indexing is safe.
static const app_flag_spec_t g_app_flag_table[APP_FLAG_COUNT] = {
    {.id = APP_FLAG_DEBUG,
     .json_key = "debug",
     .env_var = "APP_LOG_LEVEL",
     .env_match = "DEBUG",
     .cli_short = "-d",
     .cli_long = "--debug",
     .description = "Enable debug output (DEBUG level logs)",
     .exclusive_mask =
         APP_FLAG_MASK(APP_FLAG_QUIET) | APP_FLAG_MASK(APP_FLAG_VERBOSE)},
    {.id = APP_FLAG_QUIET,
     .json_key = "quiet",
     .env_var = NULL,
     .env_match = NULL,
     .cli_short = "-q",
     .cli_long = "--quiet",
     .description = "Suppress non-essential output",
     .exclusive_mask =
         APP_FLAG_MASK(APP_FLAG_DEBUG) | APP_FLAG_MASK(APP_FLAG_VERBOSE)},
    {.id = APP_FLAG_VERBOSE,
     .json_key = "verbose",
     .env_var = NULL,
     .env_match = NULL,
     .cli_short = "-v",
     .cli_long = "--verbose",
     .description = "Enable verbose output",
     .exclusive_mask =
         APP_FLAG_MASK(APP_FLAG_DEBUG) | APP_FLAG_MASK(APP_FLAG_QUIET)},
    {.id = APP_FLAG_JSON_OUTPUT,
     .json_key = "json_output",
     .env_var = NULL,
     .env_match = NULL,
     .cli_short = NULL,
     .cli_long = "--json",
     .description =
         "Force a versioned JSON response (default when stdout is not a TTY)",
     .exclusive_mask = APP_FLAG_MASK(APP_FLAG_PLAIN_OUTPUT)},
    {.id = APP_FLAG_PLAIN_OUTPUT,
     .json_key = "plain_output",
     .env_var = NULL,
     .env_match = NULL,
     .cli_short = NULL,
     .cli_long = "--plain",
     .description =
         "Force plain text without colors, even when stdout is not a TTY",
     .exclusive_mask = APP_FLAG_MASK(APP_FLAG_JSON_OUTPUT)},
    {.id = APP_FLAG_NO_COLOR,
     .json_key = "no_color",
     .env_var = "NO_COLOR",
     .env_match = NULL,
     .cli_short = NULL,
     .cli_long = "--no-color",
     .description = "Disable colored output",
     .exclusive_mask = 0},
};

const app_flag_spec_t *app_flag_table(size_t *count) {
  if (count) {
    *count = APP_FLAG_COUNT;
  }
  return g_app_flag_table;
}

const app_flag_spec_t *app_flag_find_by_json_key(const char *key) {
  if (!key) {
    return NULL;
  }
  for (size_t i = 0; i < APP_FLAG_COUNT; i++) {
    if (g_app_flag_table[i].json_key &&
        strcmp(g_app_flag_table[i].json_key, key) == 0) {
      return &g_app_flag_table[i];
    }
  }
  return NULL;
}

void app_flag_apply(bool values[APP_FLAG_COUNT], app_flag_id id, bool value) {
  if (!values || (int)id < 0 || id >= APP_FLAG_COUNT) {
    return;
  }
  values[id] = value;
  if (!value) {
    return;
  }

  const app_flag_spec_t *spec = &g_app_flag_table[id];
  for (app_flag_id other = 0; other < APP_FLAG_COUNT; other++) {
    if ((spec->exclusive_mask & APP_FLAG_MASK(other)) != 0) {
      values[other] = false;
    }
  }
}

bool app_flag_env_enabled(app_flag_id id) {
  if ((int)id < 0 || id >= APP_FLAG_COUNT) {
    return false;
  }

  const app_flag_spec_t *spec = &g_app_flag_table[id];
  if (!spec->env_var) {
    return false;
  }

  const char *value = getenv(spec->env_var);
  if (!value) {
    return false;
  }
  return !spec->env_match || strcmp(value, spec->env_match) == 0;
}

static bool app_config_set_string(char **slot, const char *value) {
  if (!slot || !value) {
    return false;
  }

  char *copy = strdup(value);
  if (!copy) {
    return false;
  }

  free(*slot);
  *slot = copy;
  return true;
}

static app_config_json_state_t app_config_json_state_from_config(
    const app_config_t *config) {
  app_config_json_state_t state = {0};
  for (size_t i = 0; i < APP_FLAG_COUNT; i++) {
    state.values[i] = config->flags[i];
  }
  return state;
}

static void app_config_commit_json_state(
    app_config_t *config, const app_config_json_state_t *staged) {
  for (size_t i = 0; i < APP_FLAG_COUNT; i++) {
    config->flags[i] = staged->values[i];
  }
}

app_error app_config_create(app_config_t **config) {
  CHECK_NULL(config, APP_ERROR_INVALID_ARG);

  *config = calloc(1, sizeof(app_config_t));
  if (*config == NULL) {
    return APP_ERROR_MEMORY;
  }

  return APP_SUCCESS;
}

void app_config_destroy(app_config_t *config) {
  if (config == NULL) {
    return;
  }

  free(config->program_name);
  free(config->command);
  free(config->config_file);

  for (int i = 0; i < config->command_arg_count; i++) {
    free(config->command_args[i]);
  }

  free(config);
}

// Find default config file path
static char *find_config_file(void) {
  static char config_path[PATH_MAX];

  // Check environment variable first
  const char *config_env = getenv("APP_CONFIG_PATH");
  if (config_env && config_env[0] != '\0') {
    return strdup(config_env);
  }

  // Try user config directory
#ifdef _WIN32
  const char *home = getenv("USERPROFILE");
  if (home) {
    snprintf(config_path, PATH_MAX, "%s\\AppData\\Local\\%s\\config.json", home,
             APP_NAME);
    if (access(config_path, R_OK) == 0) {
      return strdup(config_path);
    }
  }
#else
  const char *home = getenv("HOME");
  if (home) {
    snprintf(config_path, PATH_MAX, "%s/.config/%s/config.json", home,
             APP_NAME);
    if (access(config_path, R_OK) == 0) {
      return strdup(config_path);
    }
  }
#endif

  // Try system config directory
#ifdef _WIN32
  snprintf(config_path, PATH_MAX, "C:\\ProgramData\\%s\\config.json", APP_NAME);
#else
  snprintf(config_path, PATH_MAX, "/etc/%s/config.json", APP_NAME);
#endif
  if (access(config_path, R_OK) == 0) {
    return strdup(config_path);
  }

  return NULL;
}

app_error app_config_load_file(app_config_t *const config, const char *path) {
  CHECK_NULL(config, APP_ERROR_INVALID_ARG);

  char *config_path = path ? strdup(path) : find_config_file();
  if (path && !config_path) {
    return APP_ERROR_MEMORY;
  }

  if (!config_path) {
    LOG_DEBUG("No configuration file found");
    return APP_SUCCESS;  // Not an error if no config file exists
  }

  FILE *f = fopen(config_path, "rb");
  if (!f) {
    // Capture errno before any other call can clobber it, then map the cause to
    // a specific public exit code so a CI wrapper can tell "wrong path" from
    // "unreadable" from a genuine I/O fault (all three previously collapsed to
    // APP_ERROR_IO).
    const int open_errno = errno;
    LOG_WARNING("Failed to open config file: %s", config_path);
    free(config_path);
    if (open_errno == ENOENT || open_errno == ENOTDIR) {
      return APP_ERROR_NOT_FOUND;
    }
    if (open_errno == EACCES || open_errno == EPERM) {
      return APP_ERROR_PERMISSION;
    }
    return APP_ERROR_IO;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    free(config_path);
    return APP_ERROR_IO;
  }

  long file_size = ftell(f);
  if (file_size < 0 || (uintmax_t)file_size > (uintmax_t)CONFIG_MAX_SIZE) {
    fclose(f);
    free(config_path);
    return APP_ERROR_OUT_OF_RANGE;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    free(config_path);
    return APP_ERROR_IO;
  }

  const size_t content_size = (size_t)file_size;
  char *content = malloc(content_size + 1);
  if (!content) {
    fclose(f);
    free(config_path);
    return APP_ERROR_MEMORY;
  }

  if (content_size > 0 && fread(content, 1, content_size, f) != content_size) {
    free(content);
    fclose(f);
    free(config_path);
    return APP_ERROR_IO;
  }
  content[content_size] = '\0';
  fclose(f);

  app_config_json_state_t staged = app_config_json_state_from_config(config);
  app_error err = app_config_parse_json_state(&staged, content);
  if (err != APP_SUCCESS) {
    free(content);
    free(config_path);
    return err;
  }

  char *loaded_path = strdup(config_path);
  if (!loaded_path) {
    free(content);
    free(config_path);
    return APP_ERROR_MEMORY;
  }

  app_config_commit_json_state(config, &staged);
  free(config->config_file);
  config->config_file = loaded_path;
  LOG_INFO("Loaded configuration from %s", config_path);

  free(content);
  free(config_path);
  return APP_SUCCESS;
}

app_error app_config_load_env(app_config_t *config) {
  CHECK_NULL(config, APP_ERROR_INVALID_ARG);

  for (size_t i = 0; i < APP_FLAG_COUNT; i++) {
    const app_flag_spec_t *spec = &g_app_flag_table[i];
    if (app_flag_env_enabled(spec->id)) {
      const app_error err = app_config_set_flag(config, spec->id, true);
      if (err != APP_SUCCESS) {
        return err;
      }
    }
  }

  return APP_SUCCESS;
}

// Getters
const char *app_config_get_program_name(const app_config_t *config) {
  if (!config || !config->program_name) {
    return APP_NAME;
  }
  return config->program_name;
}

const char *app_config_get_command(const app_config_t *config) {
  return config ? config->command : NULL;
}

char *const *app_config_get_command_args(const app_config_t *config,
                                         int *count) {
  if (!config || !count) {
    return NULL;
  }
  *count = config->command_arg_count;
  return config->command_args;
}

const char *app_config_get_config_file(const app_config_t *config) {
  return config ? config->config_file : NULL;
}

bool app_config_get_flag(const app_config_t *config, app_flag_id id) {
  if (!config || (int)id < 0 || id >= APP_FLAG_COUNT) {
    return false;
  }
  return config->flags[id];
}

bool app_config_is_quiet(const app_config_t *config) {
  return app_config_get_flag(config, APP_FLAG_QUIET);
}

bool app_config_is_debug(const app_config_t *config) {
  return app_config_get_flag(config, APP_FLAG_DEBUG);
}

bool app_config_is_json_output(const app_config_t *config) {
  return app_config_get_flag(config, APP_FLAG_JSON_OUTPUT) &&
         !app_config_get_flag(config, APP_FLAG_PLAIN_OUTPUT);
}

bool app_config_is_plain_output(const app_config_t *config) {
  return app_config_get_flag(config, APP_FLAG_PLAIN_OUTPUT);
}

bool app_config_is_no_color(const app_config_t *config) {
  return app_config_get_flag(config, APP_FLAG_NO_COLOR);
}

bool app_config_is_verbose(const app_config_t *config) {
  return app_config_get_flag(config, APP_FLAG_VERBOSE);
}

app_error app_config_apply_output_defaults(app_config_t *config,
                                           bool stdout_is_tty) {
  CHECK_NULL(config, APP_ERROR_INVALID_ARG);

  if (!stdout_is_tty && !app_config_is_plain_output(config) &&
      !app_config_is_json_output(config)) {
    return app_config_set_json_output(config, true);
  }
  return APP_SUCCESS;
}

// Setters
app_error app_config_set_flag(app_config_t *config, app_flag_id id,
                              bool value) {
  if (!config || (int)id < 0 || id >= APP_FLAG_COUNT) {
    return APP_ERROR_INVALID_ARG;
  }
  app_flag_apply(config->flags, id, value);
  return APP_SUCCESS;
}

app_error app_config_set_debug(app_config_t *config, bool debug) {
  return app_config_set_flag(config, APP_FLAG_DEBUG, debug);
}

app_error app_config_set_quiet(app_config_t *config, bool quiet) {
  return app_config_set_flag(config, APP_FLAG_QUIET, quiet);
}

app_error app_config_set_verbose(app_config_t *config, bool verbose) {
  return app_config_set_flag(config, APP_FLAG_VERBOSE, verbose);
}

app_error app_config_set_json_output(app_config_t *config, bool json) {
  return app_config_set_flag(config, APP_FLAG_JSON_OUTPUT, json);
}

app_error app_config_set_plain_output(app_config_t *config, bool plain) {
  return app_config_set_flag(config, APP_FLAG_PLAIN_OUTPUT, plain);
}

app_error app_config_set_no_color(app_config_t *config, bool no_color) {
  return app_config_set_flag(config, APP_FLAG_NO_COLOR, no_color);
}

app_error app_config_set_program_name(app_config_t *config, const char *name) {
  if (!config || !name) {
    return APP_ERROR_INVALID_ARG;
  }
  return app_config_set_string(&config->program_name, name) ? APP_SUCCESS
                                                            : APP_ERROR_MEMORY;
}

app_error app_config_set_command(app_config_t *config, const char *command) {
  if (!config || !command) {
    return APP_ERROR_INVALID_ARG;
  }
  return app_config_set_string(&config->command, command) ? APP_SUCCESS
                                                          : APP_ERROR_MEMORY;
}

app_error app_config_add_command_arg(app_config_t *config, const char *arg) {
  if (!config || !arg) {
    return APP_ERROR_INVALID_ARG;
  }
  if (config->command_arg_count >= APP_MAX_COMMAND_ARGS) {
    return APP_ERROR_OUT_OF_RANGE;
  }
  char *copy = strdup(arg);
  if (!copy) {
    return APP_ERROR_MEMORY;
  }
  config->command_args[config->command_arg_count++] = copy;
  return APP_SUCCESS;
}

app_error app_config_set_config_file(app_config_t *config, const char *path) {
  if (!config || !path) {
    return APP_ERROR_INVALID_ARG;
  }
  return app_config_set_string(&config->config_file, path) ? APP_SUCCESS
                                                           : APP_ERROR_MEMORY;
}
