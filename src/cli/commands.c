/*
 * Command registration and dispatch.
 */

#include "commands.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../io/output.h"
#include "option_meta.h"

// Forward declarations for handlers defined in their own translation units.
app_error app_cmd_hello(const app_config_t *config, int argc,
                        char *const argv[]);
app_error app_cmd_echo(const app_config_t *config, int argc,
                       char *const argv[]);
app_error app_cmd_info(const app_config_t *config, int argc,
                       char *const argv[]);
app_error app_cmd_doctor(const app_config_t *config, int argc,
                         char *const argv[]);
app_error app_cmd_menu(const app_config_t *config, int argc,
                       char *const argv[]);
app_error app_cmd_opencli(const app_config_t *config, int argc,
                          char *const argv[]);

static const app_command_arg_t hello_args[] = {
    {.name = "name",
     .required = false,
     .arity_minimum = 0,
     .arity_maximum = 1,
     .description = "Name to greet (default: World)"},
};

static const char *const hello_examples[] = {
    APP_NAME " hello",
    APP_NAME " hello Alice",
};

static const app_command_arg_t echo_args[] = {
    {.name = "text",
     .required = false,
     .arity_minimum = 0,
     .arity_maximum = APP_ARG_ARITY_UNBOUNDED,
     .description = "Text to echo"},
};

static const app_command_arg_t config_option_args[] = {
    {.name = "path",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "Path to configuration file"},
};

static const char *const echo_examples[] = {
    APP_NAME " echo Hello World",
    APP_NAME " echo",
};

static const char *const info_examples[] = {
    APP_NAME " info",
    APP_NAME " --json info",
};

static const app_command_option_t doctor_options[] = {
    {.id = APP_COMMAND_OPTION_DOCTOR_DEEP,
     .name = "deep",
     .description =
         "Also exercise the optional TUI runtime when a TTY is available"},
};

static const char *const doctor_examples[] = {
    APP_NAME " doctor",
    APP_NAME " --json doctor",
};

static const char *const menu_examples[] = {
    APP_NAME " menu",
};

static const char *const opencli_examples[] = {
    APP_NAME " opencli",
};

static const app_builtin_option_t g_app_builtin_options[] = {
    {.id = APP_BUILTIN_OPTION_HELP,
     .name = "help",
     .alias = "h",
     .description = "Show help message and exit"},
    {.id = APP_BUILTIN_OPTION_VERSION,
     .name = "version",
     .alias = NULL,
     .description = "Show version information and exit"},
};

static const app_global_value_option_t g_app_global_value_options[] = {
    {.id = APP_GLOBAL_VALUE_OPTION_CONFIG,
     .name = "config",
     .alias = "c",
     .arguments = config_option_args,
     .argument_count = APP_COUNTOF(config_option_args),
     .description = "Specify configuration file path"},
};

static const app_command_t g_app_commands[] = {
    {.name = "hello",
     .summary = "Print a greeting message.",
     .handler = app_cmd_hello,
     .arguments = hello_args,
     .argument_count = APP_COUNTOF(hello_args),
     .examples = hello_examples,
     .example_count = APP_COUNTOF(hello_examples),
     .requires_terminal = false},
    {.name = "echo",
     .summary = "Echo the provided text.",
     .handler = app_cmd_echo,
     .arguments = echo_args,
     .argument_count = APP_COUNTOF(echo_args),
     .examples = echo_examples,
     .example_count = APP_COUNTOF(echo_examples),
     .requires_terminal = false},
    {.name = "info",
     .summary = "Display application metadata.",
     .handler = app_cmd_info,
     .examples = info_examples,
     .example_count = APP_COUNTOF(info_examples),
     .requires_terminal = false},
    {.name = "doctor",
     .summary = "Run starter diagnostics (add --deep for the TUI smoke test).",
     .handler = app_cmd_doctor,
     .options = doctor_options,
     .option_count = APP_COUNTOF(doctor_options),
     .examples = doctor_examples,
     .example_count = APP_COUNTOF(doctor_examples),
     .requires_terminal = false},
    {.name = "menu",
     .summary = "Launch the interactive TUI main menu.",
     .handler = app_cmd_menu,
     .examples = menu_examples,
     .example_count = APP_COUNTOF(menu_examples),
     .requires_terminal = true,
     .hidden_from_help = true},
    {.name = "opencli",
     .summary = "Print the OpenCLI contract as JSON.",
     .handler = app_cmd_opencli,
     .examples = opencli_examples,
     .example_count = APP_COUNTOF(opencli_examples),
     .requires_terminal = false},
};

#define G_APP_COMMANDS_COUNT APP_COUNTOF(g_app_commands)
#define G_APP_BUILTIN_OPTIONS_COUNT APP_COUNTOF(g_app_builtin_options)
#define G_APP_GLOBAL_VALUE_OPTIONS_COUNT APP_COUNTOF(g_app_global_value_options)

// The dispatch and help paths assume at least one command and at least one
// built-in option exist; an empty table would make the template silently
// broken rather than fail to build.
static_assert(APP_COUNTOF(g_app_commands) > 0,
              "command table must not be empty");
static_assert(APP_COUNTOF(g_app_builtin_options) > 0,
              "built-in option table must not be empty");
static_assert(APP_COUNTOF(g_app_global_value_options) > 0,
              "global value option table must not be empty");

const app_command_t *app_commands(size_t *count) {
  if (count) {
    *count = G_APP_COMMANDS_COUNT;
  }
  return g_app_commands;
}

const app_builtin_option_t *app_builtin_options(size_t *count) {
  if (count) {
    *count = G_APP_BUILTIN_OPTIONS_COUNT;
  }
  return g_app_builtin_options;
}

const app_builtin_option_t *app_builtin_option_find(const char *arg) {
  if (!arg) {
    return NULL;
  }

  for (size_t i = 0; i < G_APP_BUILTIN_OPTIONS_COUNT; i++) {
    const app_builtin_option_t *option = &g_app_builtin_options[i];
    if (app_option_token_matches(arg, option->name, option->alias)) {
      return option;
    }
  }
  return NULL;
}

const app_global_value_option_t *app_global_value_options(size_t *count) {
  if (count) {
    *count = G_APP_GLOBAL_VALUE_OPTIONS_COUNT;
  }
  return g_app_global_value_options;
}

const app_global_value_option_t *app_global_value_option_find(const char *arg) {
  if (!arg) {
    return NULL;
  }

  for (size_t i = 0; i < G_APP_GLOBAL_VALUE_OPTIONS_COUNT; i++) {
    const app_global_value_option_t *option = &g_app_global_value_options[i];
    if (app_option_token_matches(arg, option->name, option->alias)) {
      return option;
    }
  }
  return NULL;
}

const app_command_t *app_command_find(const char *name) {
  if (!name) {
    return NULL;
  }
  for (size_t i = 0; i < G_APP_COMMANDS_COUNT; i++) {
    if (strcmp(g_app_commands[i].name, name) == 0) {
      return &g_app_commands[i];
    }
  }
  return NULL;
}

bool app_command_is_visible(const app_command_t *command) {
  // Single definition of "appears in human-facing listings" shared by root
  // --help (plain and styled) and the TUI Commands browser, so those surfaces
  // can never disagree on which commands a person sees. Hidden commands stay
  // dispatchable and remain in the OpenCLI contract (the machine surface lists
  // every command); only human discovery filters on this.
  return command && !command->hidden_from_help;
}

// Levenshtein edit distance between a and b. Bounded to short inputs (a real
// typo is short): anything longer is treated as far away so the suggestion gate
// rejects it. Command names are tiny, so the row buffers are small.
#define APP_SUGGEST_MAX_LEN 64

static size_t app_command_edit_distance(const char *a, const char *b) {
  const size_t la = strlen(a);
  const size_t lb = strlen(b);
  if (la > APP_SUGGEST_MAX_LEN || lb > APP_SUGGEST_MAX_LEN) {
    return SIZE_MAX;
  }

  size_t prev[APP_SUGGEST_MAX_LEN + 1];
  size_t curr[APP_SUGGEST_MAX_LEN + 1];
  for (size_t j = 0; j <= lb; j++) {
    prev[j] = j;
  }
  for (size_t i = 1; i <= la; i++) {
    curr[0] = i;
    for (size_t j = 1; j <= lb; j++) {
      const size_t cost = a[i - 1] == b[j - 1] ? 0U : 1U;
      const size_t deletion = prev[j] + 1U;
      const size_t insertion = curr[j - 1] + 1U;
      const size_t substitution = prev[j - 1] + cost;
      size_t best = deletion < insertion ? deletion : insertion;
      best = best < substitution ? best : substitution;
      curr[j] = best;
    }
    for (size_t j = 0; j <= lb; j++) {
      prev[j] = curr[j];
    }
  }
  return prev[lb];
}

const char *app_command_suggest(const char *unknown) {
  if (!unknown || unknown[0] == '\0') {
    return NULL;
  }
  const size_t unknown_len = strlen(unknown);

  const char *best = NULL;
  size_t best_distance = SIZE_MAX;
  for (size_t i = 0; i < G_APP_COMMANDS_COUNT; i++) {
    const app_command_t *command = &g_app_commands[i];
    if (command->hidden_from_help) {
      continue;  // never steer users toward an internal command
    }
    const size_t distance = app_command_edit_distance(unknown, command->name);
    if (distance < best_distance) {
      best_distance = distance;
      best = command->name;
    }
  }

  // Conservative gate: only a clearly-close match (within two edits AND under
  // half the typed length) suggests. Short, ambiguous tokens and far-off words
  // produce no hint rather than a misleading one.
  if (best && best_distance <= 2U && best_distance * 2U < unknown_len) {
    return best;
  }
  return NULL;
}

const app_command_option_t *app_command_option_find(
    const app_command_t *command, const char *arg) {
  if (!command || !arg || strncmp(arg, "--", 2) != 0 || arg[2] == '\0') {
    return NULL;
  }

  for (size_t i = 0; i < command->option_count; i++) {
    const app_command_option_t *option = &command->options[i];
    if (app_option_token_matches(arg, option->name, NULL)) {
      return option;
    }
  }
  return NULL;
}

static int app_command_min_positionals(const app_command_t *command) {
  int minimum = 0;
  if (!command) {
    return minimum;
  }

  for (size_t i = 0; i < command->argument_count; i++) {
    minimum += command->arguments[i].arity_minimum;
  }
  return minimum;
}

static int app_command_max_positionals(const app_command_t *command) {
  int maximum = 0;
  if (!command) {
    return maximum;
  }

  for (size_t i = 0; i < command->argument_count; i++) {
    if (command->arguments[i].arity_maximum == APP_ARG_ARITY_UNBOUNDED) {
      return APP_ARG_ARITY_UNBOUNDED;
    }
    maximum += command->arguments[i].arity_maximum;
  }
  return maximum;
}

static void app_command_report_validation_error(const app_config_t *config,
                                                const char *message,
                                                const char *hint) {
  const bool have_hint = hint && *hint;
  if (config && app_config_is_json_output(config)) {
    // Emit a single JSON object so stderr stays one parseable document, even
    // when a message has an accompanying hint. Other error paths emit one
    // object per error; folding the hint in keeps the headless/--json contract
    // consistent for consumers that parse stderr as a single document.
    if (have_hint) {
      char combined[544];
      snprintf(combined, sizeof(combined), "%s. %s", message, hint);
      app_output(combined, config, true);
    } else {
      app_output(message, config, true);
    }
  } else {
    fprintf(stderr, "%s\n", message);
    if (have_hint) {
      fprintf(stderr, "%s\n", hint);
    }
  }
}

app_error app_command_validate_invocation(const app_command_t *command,
                                          int argc, char *const argv[],
                                          const app_config_t *config,
                                          const char *program_name) {
  if (!command || argc < 0 || (argc > 0 && !argv)) {
    return APP_ERROR_INVALID_ARG;
  }

  char message[256];
  char hint[256];

  bool end_of_options = false;
  int positional_count = 0;
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg) {
      return APP_ERROR_INVALID_ARG;
    }

    if (!end_of_options && strcmp(arg, "--") == 0) {
      end_of_options = true;
      continue;
    }

    if (!end_of_options && strncmp(arg, "--", 2) == 0 && arg[2] != '\0') {
      if (app_command_option_find(command, arg)) {
        continue;
      }
      snprintf(message, sizeof(message),
               "Error: Unknown option '%s' for command '%s'", arg,
               command->name);
      snprintf(hint, sizeof(hint), "Run '%s --help' for usage information",
               program_name ? program_name : APP_NAME);
      app_command_report_validation_error(config, message, hint);
      return APP_ERROR_UNKNOWN_OPTION;
    }

    positional_count++;
  }

  const int minimum = app_command_min_positionals(command);
  const int maximum = app_command_max_positionals(command);
  if (positional_count < minimum) {
    snprintf(message, sizeof(message),
             "Error: Command '%s' expects at least %d argument%s",
             command->name, minimum, minimum == 1 ? "" : "s");
    app_command_report_validation_error(config, message, NULL);
    return APP_ERROR_MISSING_ARG;
  }
  if (maximum != APP_ARG_ARITY_UNBOUNDED && positional_count > maximum) {
    snprintf(message, sizeof(message),
             "Error: Command '%s' expects at most %d argument%s", command->name,
             maximum, maximum == 1 ? "" : "s");
    app_command_report_validation_error(config, message, NULL);
    return APP_ERROR_INVALID_ARG;
  }

  return APP_SUCCESS;
}
