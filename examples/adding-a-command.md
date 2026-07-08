# Example: Adding an OpenQuick Command

OpenQuick commands are registered in one table (`src/cli/commands.c`). That table drives dispatch, help, and `quick opencli`, so a new command needs
a handler, metadata, build wiring, a regenerated contract, and tests.

This example adds a real product-shaped command: `quick status [site]`, which resolves the current OpenQuick target and prints the URL/profile without contacting the host.

## 1. Add the command handler

Create `src/cli/commands_status.c`:

```c
#include "../core/deploy_plan.h"
#include "../core/profile_config.h"
#include "../io/output.h"
#include "commands.h"
#include "commands_openquick.h"

app_error app_cmd_status(const app_config_t *config, int argc,
                         char *const argv[]) {
  const char *value_opts[] = {"--profile"};
  const char *site = quick_cmd_first_positional(argc, argv, value_opts,
                                                APP_COUNTOF(value_opts));

  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) return err;

  quick_plan_overrides_t overrides = {
      .site = site,
      .profile = quick_cmd_value(argc, argv, "--profile"),
  };
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  err = quick_deploy_plan_resolve(&overrides, &profiles, &plan);
  quick_profile_config_destroy(&profiles);
  if (err != APP_SUCCESS) return err;

  if (app_config_is_json_output(config)) {
    bool comma = false;
    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "format_version", "1.0", &comma);
    app_json_write_string_field(stdout, "site", plan.site, &comma);
    app_json_write_string_field(stdout, "profile", plan.profile, &comma);
    app_json_write_string_field(stdout, "url", plan.url, &comma);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
  } else {
    app_output_format(config, false, "site    %s", plan.site);
    app_output_format(config, false, "profile %s", plan.profile);
    app_output_format(config, false, "url     %s", plan.url);
  }

  quick_deploy_plan_destroy(&plan);
  return APP_SUCCESS;
}
```

Add the file to `base_sources` in `build.zig`.

## 2. Register command metadata

In `src/cli/commands.c`, add the forward declaration, options, arguments/examples, and a row in `g_app_commands`:

```c
app_error app_cmd_status(const app_config_t *config, int argc,
                         char *const argv[]);

static const app_command_option_t status_options[] = {
    {.id = APP_COMMAND_OPTION_UNKNOWN,
     .name = "profile",
     .description = "Profile used for target resolution"},
};

static const app_command_arg_t status_args[] = {
    {.name = "site",
     .required = false,
     .arity_minimum = 0,
     .arity_maximum = APP_ARG_ARITY_UNBOUNDED,
     .description = "Optional site name and option values"},
};

static const char *const status_examples[] = {
    "quick status",
    "quick status lunch-vote --profile lab",
};

/* inside g_app_commands */
{.name = "status",
 .summary = "Print the resolved OpenQuick target.",
 .handler = app_cmd_status,
 .options = status_options,
 .option_count = APP_COUNTOF(status_options),
 .arguments = status_args,
 .argument_count = APP_COUNTOF(status_args),
 .examples = status_examples,
 .example_count = APP_COUNTOF(status_examples),
 .requires_terminal = false},
```

## 3. Regenerate `opencli.json`

```bash
zig build run -- opencli > opencli.json
```

The command should appear under `.command.commands[]` with its options, arguments, and examples.

## 4. Add contract tests

Add subprocess coverage in `test/cli_contract_cases.c`:

```c
static bool test_status_command(test_context_t *ctx) {
  const char *args[] = {"--json", "status", "lunch-vote"};
  const env_var_t env[] = {{"QUICK_BASE_DOMAIN", "quick.example.com"}};
  command_result_t result =
      cc_run_cli(ctx, args, ARRAY_LEN(args), env, ARRAY_LEN(env));
  const bool ok = cc_expect_exit(&result, 0) &&
                  cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
                  cc_expect_stdout_contains(&result, "https://lunch-vote.quick.example.com");
  cc_command_result_free(&result);
  return ok;
}
```

Register it in `cli_contract_cases[]`, then run `zig build test`.

## Notes

- Keep command handlers small and delegate target/config work to `src/core/` helpers.
- Use `execvp`/argv arrays for child processes; do not interpolate site names or paths into shell snippets.
- Every machine-readable command output should include `"format_version":"1.0"`.
