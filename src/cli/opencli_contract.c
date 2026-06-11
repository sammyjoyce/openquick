/*
 * OpenCLI contract metadata tables.
 */

#include "opencli_contract.h"

static const app_command_arg_t root_arguments[] = {
    {.name = "command",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = 1,
     .description = "The command to execute"},
};

static const char *const extra_examples[] = {
    APP_NAME " --help",
    APP_NAME " --version",
};

static const app_opencli_metadata_field_t environment_fields[] = {
    {.name = "APP_LOG_LEVEL",
     .description =
         "Set logging verbosity: ERROR, WARNING, INFO, DEBUG (default: ERROR)"},
    {.name = "APP_CONFIG_PATH",
     .description = "Override the default config file lookup path"},
    {.name = "NO_COLOR", .description = "Disable colored output when set"},
    {.name = "FORCE_COLOR",
     .description = "Force colored output on; set 0 (or false) to force off"},
    {.name = "CLICOLOR_FORCE",
     .description = "Set to a non-zero value to force colored output on"},
    {.name = "CLICOLOR", .description = "Set 0 to disable colored output"},
    {.name = "APP_CLI_THEME",
     .description = "Terminal UI theme for styled CLI and generated TUI: auto, "
                    "dark, or light"},
    {.name = "APP_CLI_COLOR",
     .description =
         "Terminal UI color profile: auto, never, 16, 256, truecolor"},
    {.name = "APP_CLI_OSC11",
     .description = "Set 0 to disable terminal background detection"},
    {.name = "APP_CLI_ACCENT",
     .description = "Override terminal UI accent (#rrggbb or palette index)"},
};

static const app_opencli_metadata_field_t configuration_fields[] = {
    {.name = "location", .description = "~/.config/" APP_NAME "/config.json"},
    {.name = "format",
     .description =
         "Flat JSON object with boolean debug, quiet, verbose, no_color, "
         "json_output, and plain_output keys"},
    {.name = "precedence",
     .description = "CLI args > Environment > Config file > Defaults"},
};

static const app_opencli_metadata_field_t build_fields[] = {
    {.name = "system", .description = "Zig build system"},
    {.name = "compiler", .description = "Zig cc (Clang/LLVM)"},
    {.name = "standard", .description = "C23"},
};

static const app_opencli_metadata_group_t metadata_groups[] = {
    {.name = "environment",
     .fields = environment_fields,
     .field_count = sizeof(environment_fields) / sizeof(environment_fields[0])},
    {.name = "configuration",
     .fields = configuration_fields,
     .field_count =
         sizeof(configuration_fields) / sizeof(configuration_fields[0])},
    {.name = "build",
     .fields = build_fields,
     .field_count = sizeof(build_fields) / sizeof(build_fields[0])},
};

static const app_opencli_contract_t g_opencli_contract = {
    .opencli_version = "0.1",
    .info =
        {
            .title = APP_TITLE,
            .description = APP_DESCRIPTION,
            .version = APP_VERSION,
            .contact =
                {
                    .name = "Your Name",
                    .url = "https://github.com/yourusername/yourproject",
                },
            .license =
                {
                    .name = "MIT License",
                    .identifier = "MIT",
                },
        },
    .conventions =
        {
            .group_options = false,
            .option_argument_separator = " ",
        },
    .root_arguments = root_arguments,
    .root_argument_count = sizeof(root_arguments) / sizeof(root_arguments[0]),
    .extra_examples = extra_examples,
    .extra_example_count = sizeof(extra_examples) / sizeof(extra_examples[0]),
    .interactive = false,
    .metadata = metadata_groups,
    .metadata_count = sizeof(metadata_groups) / sizeof(metadata_groups[0]),
};

const app_opencli_contract_t *app_opencli_contract(void) {
  return &g_opencli_contract;
}

const app_opencli_metadata_field_t *app_opencli_environment_docs(
    size_t *count) {
  // The same table the OpenCLI `metadata.environment` group publishes. The
  // human help renderers (plain and styled) iterate this so the documented
  // environment stays congruent with the machine contract from one source.
  if (count) {
    *count = sizeof(environment_fields) / sizeof(environment_fields[0]);
  }
  return environment_fields;
}
