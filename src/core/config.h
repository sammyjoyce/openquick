/*
 * Configuration management for the application.
 *
 * Implements a layered configuration system where values can come from files,
 * environment variables, and command-line arguments, with later sources
 * overriding earlier ones. This approach allows users to set defaults in config
 * files while overriding specific values for individual runs via command line.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "error.h"
#include "types.h"

// The opaque app_config_t type is declared in types.h.

// Identifiers for every boolean flag the application understands.
// Adding a new flag is a three-step operation:
//   1. add an enum value here
//   2. add a row in g_app_flag_table (config.c)
//   3. add a getter via the APP_FLAG_GETTERS macro pattern, if needed
typedef enum {
  APP_FLAG_DEBUG,
  APP_FLAG_QUIET,
  APP_FLAG_VERBOSE,
  APP_FLAG_JSON_OUTPUT,
  APP_FLAG_PLAIN_OUTPUT,
  APP_FLAG_NO_COLOR,
  APP_FLAG_COUNT,
} app_flag_id;

typedef unsigned int app_flag_mask_t;

#define APP_FLAG_MASK(id) (1u << (unsigned)(id))

// Metadata about a flag: how it appears on the command line, in env vars,
// in JSON config files, and which flags it should reset when set to true.
typedef struct {
  app_flag_id id;
  const char *json_key;
  const char *env_var;    // NULL when no env override is supported
  const char *env_match;  // NULL means env-var presence enables the flag
  const char *cli_short;  // NULL when there is no short option
  const char *cli_long;   // long option, including leading "--"
  const char *description;
  app_flag_mask_t exclusive_mask;
} app_flag_spec_t;

// Iterate over every flag spec. count is non-NULL on return.
const app_flag_spec_t *app_flag_table(size_t *count);

// Look up by JSON key (used while loading config files).
const app_flag_spec_t *app_flag_find_by_json_key(const char *key);

// Apply one flag value to a boolean flag array and enforce exclusivity.
void app_flag_apply(bool values[APP_FLAG_COUNT], app_flag_id id, bool value);

// Evaluate an environment-backed flag using the table row for that flag.
bool app_flag_env_enabled(app_flag_id id);

// Create and destroy configuration objects with proper lifecycle management.
// The create function allocates and initializes with defaults, while destroy
// ensures all allocated resources are properly freed to prevent leaks.
APP_NODISCARD app_error app_config_create(app_config_t **config);
void app_config_destroy(app_config_t *config);

// Load configuration from various sources with cumulative override semantics.
// Each load function merges new values with existing configuration, allowing
// users to build up configuration in layers: file -> environment -> command
// line. This precedence order ensures command-line args always win for maximum
// flexibility.
APP_NODISCARD app_error app_config_load_file(app_config_t *config,
                                             const char *path);
APP_NODISCARD app_error app_config_load_env(app_config_t *config);

// Configuration getters return pointers owned by the config object. The values
// remain valid until the next setter call or app_config_destroy(). The config
// object is mutable and not internally synchronized; callers that share it
// across threads must provide their own locking or publish it read-only.
const char *app_config_get_program_name(const app_config_t *config);
const char *app_config_get_command(const app_config_t *config);
char *const *app_config_get_command_args(const app_config_t *config,
                                         int *count);
const char *app_config_get_config_file(const app_config_t *config);
bool app_config_get_flag(const app_config_t *config, app_flag_id id);

bool app_config_is_quiet(const app_config_t *config);
bool app_config_is_debug(const app_config_t *config);
bool app_config_is_json_output(const app_config_t *config);
bool app_config_is_plain_output(const app_config_t *config);
bool app_config_is_no_color(const app_config_t *config);
bool app_config_is_verbose(const app_config_t *config);

// Apply output defaults that depend on the launch context after all explicit
// config sources have been loaded. Explicit plain/json flags already present on
// the config win; otherwise stdout redirection defaults to JSON output.
APP_NODISCARD app_error app_config_apply_output_defaults(app_config_t *config,
                                                         bool stdout_is_tty);

// Generic flag setter that also enforces exclusivity (e.g. setting json
// clears plain). Prefer this from new code; the named setters below remain
// for source compatibility.
APP_NODISCARD app_error app_config_set_flag(app_config_t *config,
                                            app_flag_id id, bool value);

APP_NODISCARD app_error app_config_set_debug(app_config_t *config, bool debug);
APP_NODISCARD app_error app_config_set_quiet(app_config_t *config, bool quiet);
APP_NODISCARD app_error app_config_set_verbose(app_config_t *config,
                                               bool verbose);
APP_NODISCARD app_error app_config_set_json_output(app_config_t *config,
                                                   bool json);
APP_NODISCARD app_error app_config_set_plain_output(app_config_t *config,
                                                    bool plain);
APP_NODISCARD app_error app_config_set_no_color(app_config_t *config,
                                                bool no_color);
APP_NODISCARD app_error app_config_set_program_name(app_config_t *config,
                                                    const char *name);
APP_NODISCARD app_error app_config_set_command(app_config_t *config,
                                               const char *command);
APP_NODISCARD app_error app_config_add_command_arg(app_config_t *config,
                                                   const char *arg);
APP_NODISCARD app_error app_config_set_config_file(app_config_t *config,
                                                   const char *path);
