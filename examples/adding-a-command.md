# Example: Adding a New Command

This example adds a `greet` command that greets one or more people. The same five steps work for any command, because dispatch, help, and the OpenCLI contract are all driven by one command table.

## 1. Add the command handler

Create a command translation unit, for example `src/cli/commands_greet.c`. Every handler has the same signature: it takes the resolved config and the command's arguments, and returns an `app_error`.

```c
app_error app_cmd_greet(const app_config_t *config, int argc,
                        char *const argv[]) {
    if (argc == 0) {
        fprintf(stderr, "Error: greet command requires at least one name\n");
        return APP_ERROR_MISSING_ARG;
    }

    app_output("Greetings to:", config, false);
    for (int i = 0; i < argc; i++) {
        app_output_format(config, false, "  - %s", argv[i]);
    }
    app_output("", config, false);
    app_output("Have a great day!", config, false);
    return APP_SUCCESS;
}
```

Use `app_output` for plain strings and `app_output_format` for printf-style output (see
`src/cli/commands_basic.c` for the real `hello` and `echo` handlers). Both honour
`--json`, `--quiet`, and color flags; `fprintf(stderr, …)` is fine for the error path.

Add the new file to the `base_sources` array in `build.zig` so it is compiled (see [ZIG_PRIMER.md](../docs/ZIG_PRIMER.md#adding-a-c-file)).

## 2. Register command metadata

Add the forward declaration, argument table, examples, and a command row to
`g_app_commands` in `src/cli/commands.c`. This one table feeds dispatch, help
text, and `myapp opencli`. `APP_COUNTOF` (from `src/cli/commands.h`, already
included there) yields the element count.

```c
app_error app_cmd_greet(const app_config_t *config, int argc,
                        char *const argv[]);

static const app_command_arg_t greet_args[] = {
    {.name = "names",
     .required = true,
     .arity_minimum = 1,
     .arity_maximum = APP_ARG_ARITY_UNBOUNDED,
     .description = "Names of people to greet"},
};

static const char *const greet_examples[] = {
    "myapp greet Alice",
    "myapp greet Alice Bob Charlie",
};

static const app_command_t g_app_commands[] = {
    /* existing commands... */
    {.name = "greet",
     .summary = "Greet multiple people.",
     .handler = app_cmd_greet,
     .arguments = greet_args,
     .argument_count = APP_COUNTOF(greet_args),
     .examples = greet_examples,
     .example_count = APP_COUNTOF(greet_examples),
     .requires_terminal = false},
};
```

## 3. Update opencli.json

The contract is checked in, and `zig build test` fails if it drifts from the binary. Regenerate it from the live command:

```bash
zig build run -- opencli > opencli.json
```

Your command should now appear in the root command's `commands` array:

```json
{
  "name": "greet",
  "description": "Greet multiple people.",
  "options": [],
  "arguments": [
    {
      "name": "names",
      "required": true,
      "arity": {
        "minimum": 1,
        "maximum": null
      },
      "description": "Names of people to greet"
    }
  ],
  "examples": [
    "myapp greet Alice",
    "myapp greet Alice Bob Charlie"
  ]
}
```

(`APP_ARG_ARITY_UNBOUNDED` serializes as JSON `null`.)

## 4. Add tests

Add a case to `test/cli_contract_cases.c` that covers the happy path and the error path:

```c
static bool test_greet_command(test_context_t *ctx) {
  bool ok = true;

  {
    const char *args[] = {"greet", "Alice"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "Alice") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"greet", "Alice", "Bob"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "Alice") &&
         cc_expect_stdout_contains(&result, "Bob") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"greet"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_not_exit(&result, 0) &&
         cc_expect_stderr_contains(&result, "requires at least one name") && ok;
    cc_command_result_free(&result);
  }

  return ok;
}
```

Register it in the `tests` array:

```c
{"greet command handles names", test_greet_command},
```

See [TESTING.md](../docs/TESTING.md) for the difference between contract tests and unit tests.

## 5. Build and test

```bash
zig build                                  # build
zig build test                             # run the suite (fails if opencli.json drifted)
./zig-out/bin/myapp greet Alice Bob Charlie
```

## What you get

The command now:

- accepts multiple arguments and validates that at least one is present
- appears in `--help` and in `myapp opencli`
- returns a meaningful exit code on error (`APP_ERROR_MISSING_ARG`)
- is covered by a contract test

Repeat the same pattern for any command. Commands that open the TUI set `.requires_terminal = true`. See [custom-tui.md](custom-tui.md).
