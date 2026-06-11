/*
 * CLI contract test cases. Each function returns true on pass, false on fail
 * and prints a TAP-friendly diagnostic on failure.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/error.h"
#include "cli_contract.h"

#ifdef _WIN32
#include <io.h>
#define unlink _unlink
#else
#include <unistd.h>
#endif

static bool test_installed_binary_starts(test_context_t *ctx) {
  if (!cc_file_exists(ctx->binary)) {
    fprintf(stderr, "binary does not exist: %s\n", ctx->binary);
    return false;
  }

  const char *args[] = {"--version"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) && result.out && result.out[0] != '\0';
  if (!ok && result.out && result.out[0] == '\0') {
    fprintf(stderr, "expected version output\n");
  }
  cc_command_result_free(&result);
  return ok;
}

static bool test_help_is_human_readable(test_context_t *ctx) {
  const char *args[] = {"--help"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "USAGE") &&
      cc_expect_stdout_contains(&result, "COMMANDS") &&
      cc_expect_stdout_contains(&result, "doctor") &&
      cc_expect_stdout_contains(&result,
                                "Enable debug output (DEBUG level logs)") &&
      cc_expect_stdout_contains(&result, "(env: APP_LOG_LEVEL)") &&
      // ENVIRONMENT is rendered from the single canonical table the
      // OpenCLI contract publishes, so the color env vars and
      // APP_CONFIG_PATH appear here just as they do in opencli.json.
      cc_expect_stdout_contains(&result, "FORCE_COLOR") &&
      cc_expect_stdout_contains(&result, "NO_COLOR") &&
      cc_expect_stdout_contains(&result, "APP_CONFIG_PATH");
  if (ok && result.out && strstr(result.out, "  menu") != NULL) {
    fprintf(stderr, "root help must not list the interactive menu command\n");
    fprintf(stderr, "stdout:\n%s\n", result.out);
    cc_command_result_free(&result);
    return false;
  }
  cc_command_result_free(&result);
  return ok;
}

static bool test_builtins_render_expected_output(test_context_t *ctx) {
  bool ok = true;

  {
    const char *args[] = {"--plain", "hello"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "Hello, World!") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"--plain", "hello", "Alice"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "Hello, Alice!") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"--plain", "echo", "test", "message"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "test message") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"--plain", "info"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "Application:") &&
         cc_expect_stdout_contains(&result, "Version:") && ok;
    cc_command_result_free(&result);
  }

  return ok;
}

static bool test_json_is_default_when_stdout_is_not_tty(test_context_t *ctx) {
  const char *args[] = {"hello"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
      cc_expect_stdout_contains(&result, "\"message\":\"Hello, World!\"");
  cc_command_result_free(&result);
  return ok;
}

static bool test_json_info_is_versioned_machine_output(test_context_t *ctx) {
  const char *args[] = {"--json", "info"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
      cc_expect_stdout_contains(&result, "\"features\"") &&
      cc_expect_stdout_contains(&result, "\"tui\"");
  cc_command_result_free(&result);
  return ok;
}

static bool test_quiet_json_commands_suppress_stdout(test_context_t *ctx) {
  bool ok = true;

  {
    const char *args[] = {"--quiet", "--json", "info"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) && cc_expect_stdout_empty(&result) && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"--quiet", "--json", "doctor"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) && cc_expect_stdout_empty(&result) && ok;
    cc_command_result_free(&result);
  }

  return ok;
}

static bool test_doctor_reports_binary_state(test_context_t *ctx) {
  const char *args[] = {"--plain", "doctor"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok = cc_expect_exit(&result, 0) &&
                  cc_expect_stdout_contains(&result, "doctor") &&
                  cc_expect_stdout_contains(&result, "binary");
  cc_command_result_free(&result);
  return ok;
}

static bool test_doctor_deep_option_exercises_runtime_probe(
    test_context_t *ctx) {
  const char *args[] = {"--json", "doctor", "--deep"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  bool ok = cc_expect_exit(&result, 0) &&
            cc_expect_stdout_contains(&result, "\"name\":\"tui_runtime\"");
  if (ok) {
    const bool deep_detail =
        result.out &&
        (strstr(result.out, "TUI support not compiled") ||
         strstr(result.out, "runtime smoke skipped without a TTY") ||
         strstr(result.out, "ncurses initialized successfully") ||
         strstr(result.out, "terminal is too small"));
    if (!deep_detail) {
      fprintf(stderr, "expected doctor --deep runtime probe detail\n");
      ok = false;
    }
  }
  cc_command_result_free(&result);
  return ok;
}

static bool test_plain_mode_disables_forced_color(test_context_t *ctx) {
  const char *args[] = {"--plain", "doctor"};
  const env_var_t env[] = {{"FORCE_COLOR", "1"}};
  command_result_t result =
      cc_run_cli(ctx, args, ARRAY_LEN(args), env, ARRAY_LEN(env));
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "color_output") &&
      cc_expect_stdout_contains(&result, "disabled for this output");
  cc_command_result_free(&result);
  return ok;
}

static bool test_force_color_zero_disables_color(test_context_t *ctx) {
  // FORCE_COLOR=0 is the de-facto "force color off" signal; it must disable
  // color rather than (as the old getenv != NULL check did) enable it.
  const char *args[] = {"doctor"};
  const env_var_t env[] = {{"FORCE_COLOR", "0"}};
  command_result_t result =
      cc_run_cli(ctx, args, ARRAY_LEN(args), env, ARRAY_LEN(env));
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "color_output") &&
      cc_expect_stdout_contains(&result, "disabled for this output");
  cc_command_result_free(&result);
  return ok;
}

static bool test_command_arguments_are_not_global_config_flags(
    test_context_t *ctx) {
  const char *args[] = {"--plain", "echo", "-c",
                        "/definitely/not/a/config.json"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "-c /definitely/not/a/config.json");
  cc_command_result_free(&result);
  return ok;
}

static bool test_command_metadata_is_enforced(test_context_t *ctx) {
  bool ok = true;

  {
    const char *args[] = {"hello", "Alice", "Bob"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, APP_ERROR_INVALID_ARG) &&
         cc_expect_stderr_contains(&result, "expects at most 1 argument") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"doctor", "--not-real"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    // The message and its hint must arrive as one JSON envelope (exactly one
    // "format_version") so stderr stays a single parseable document.
    ok = cc_expect_exit(&result, APP_ERROR_UNKNOWN_OPTION) &&
         cc_expect_stderr_contains(&result, "Unknown option '--not-real'") &&
         cc_expect_stderr_contains(&result, "usage information") &&
         cc_expect_stderr_occurs_once(&result, "\"format_version\"") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"--plain", "echo", "--", "--version"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "--version") && ok;
    cc_command_result_free(&result);
  }

  {
    // Tokens after "--" are positionals: echo must print "--help", not help.
    const char *args[] = {"--plain", "echo", "--", "--help"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    const bool printed_help =
        result.out != NULL && strstr(result.out, "Usage:") != NULL;
    if (printed_help) {
      fprintf(stderr, "echo -- --help must echo, not print help\n");
    }
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "--help") && !printed_help && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"hello", "--help"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "USAGE") &&
         cc_expect_stdout_contains(&result, "hello") && ok;
    cc_command_result_free(&result);
  }

  return ok;
}

static bool test_explicit_config_file_failures_are_visible(
    test_context_t *ctx) {
  const char *args[] = {"--config", "/definitely/not/a/config.json", "hello"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_not_exit(&result, 0) &&
      cc_expect_stderr_contains(&result, "failed to load config") &&
      cc_expect_stderr_contains(&result, "/definitely/not/a/config.json");
  cc_command_result_free(&result);
  return ok;
}

static bool test_verbose_mode_emits_diagnostics_on_stderr(test_context_t *ctx) {
  const char *args[] = {"--plain", "--verbose", "hello"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok = cc_expect_exit(&result, 0) &&
                  cc_expect_stdout_contains(&result, "Hello, World!") &&
                  cc_expect_stderr_contains(&result, "[INFO]");
  cc_command_result_free(&result);
  return ok;
}

static bool test_invalid_env_config_fails_without_partial_settings(
    test_context_t *ctx) {
  // A known flag (quiet) is staged before a malformed sibling value (debug must
  // be boolean) forces the whole load to fail. The staged flag must not leak:
  // the load aborts atomically rather than applying quiet partially.
  char *config_path = NULL;
  if (!cc_write_temp_config("{\"quiet\":true,\"debug\":42}", &config_path)) {
    fprintf(stderr, "failed to write temporary config\n");
    return false;
  }

  const char *args[] = {"hello"};
  const env_var_t env[] = {{"APP_CONFIG_PATH", config_path}};
  command_result_t result =
      cc_run_cli(ctx, args, ARRAY_LEN(args), env, ARRAY_LEN(env));
  const bool ok = cc_expect_not_exit(&result, 0) &&
                  cc_expect_stderr_contains(&result, "failed to load config") &&
                  cc_expect_stdout_empty(&result);
  cc_command_result_free(&result);
  (void)unlink(config_path);
  free(config_path);
  return ok;
}

static bool test_valid_config_skips_nested_unknown_keys(test_context_t *ctx) {
  // A forward-compatible config may carry unknown keys whose values are nested
  // objects/arrays. The loader skips them and still applies the known sibling
  // flag (quiet here), so hello succeeds with suppressed output.
  char *config_path = NULL;
  if (!cc_write_temp_config(
          "{\"ignored\":{\"a\":[1,{\"b\":2}]},\"quiet\":true}", &config_path)) {
    fprintf(stderr, "failed to write temporary config\n");
    return false;
  }

  const char *args[] = {"--config", config_path, "hello"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok = cc_expect_exit(&result, 0) && cc_expect_stdout_empty(&result);
  cc_command_result_free(&result);
  (void)unlink(config_path);
  free(config_path);
  return ok;
}

static bool test_valid_flat_config_skips_unknown_scalar_keys(
    test_context_t *ctx) {
  char *config_path = NULL;
  if (!cc_write_temp_config("{\"ignored\":\"debug\",\"quiet\":true}",
                            &config_path)) {
    fprintf(stderr, "failed to write temporary config\n");
    return false;
  }

  const char *args[] = {"--config", config_path, "hello"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok = cc_expect_exit(&result, 0) && cc_expect_stdout_empty(&result);
  cc_command_result_free(&result);
  (void)unlink(config_path);
  free(config_path);
  return ok;
}

static bool test_unknown_command_reports_actionable_error(test_context_t *ctx) {
  const char *args[] = {"not-a-command"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_not_exit(&result, 0) &&
      cc_expect_stderr_contains(&result, "Unknown command: not-a-command") &&
      cc_expect_stderr_contains(&result, "--help");
  cc_command_result_free(&result);
  return ok;
}

static bool test_unknown_command_suggests_closest(test_context_t *ctx) {
  const char *args[] = {"helo"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok = cc_expect_not_exit(&result, 0) &&
                  cc_expect_stderr_contains(&result, "Unknown command: helo") &&
                  cc_expect_stderr_contains(&result, "Did you mean 'hello'?");
  cc_command_result_free(&result);
  return ok;
}

static bool test_unknown_command_far_token_has_no_suggestion(
    test_context_t *ctx) {
  const char *args[] = {"zzzzzz"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool no_hint =
      !result.err || strstr(result.err, "Did you mean") == NULL;
  const bool ok =
      cc_expect_not_exit(&result, 0) &&
      cc_expect_stderr_contains(&result, "Unknown command: zzzzzz") && no_hint;
  if (!no_hint) {
    fprintf(stderr, "expected no suggestion for a far-off token\n");
  }
  cc_command_result_free(&result);
  return ok;
}

static bool test_unknown_command_does_not_suggest_hidden(test_context_t *ctx) {
  // "men" is one edit from the hidden `menu` command and far from every visible
  // command, so no suggestion should appear: hidden commands are never offered.
  const char *args[] = {"men"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool no_hint =
      !result.err || strstr(result.err, "Did you mean") == NULL;
  const bool ok = cc_expect_not_exit(&result, 0) &&
                  cc_expect_stderr_contains(&result, "Unknown command: men") &&
                  no_hint;
  if (!no_hint) {
    fprintf(stderr, "expected hidden command 'menu' to never be suggested\n");
  }
  cc_command_result_free(&result);
  return ok;
}

static bool test_headless_json_request_dispatches_command(test_context_t *ctx) {
  command_result_t result = cc_run_cli_with_stdin(
      ctx, NULL, 0, "{\"command\":\"hello\",\"args\":[\"Alice\"]}", NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
      cc_expect_stdout_contains(&result, "\"message\":\"Hello, Alice!\"");
  cc_command_result_free(&result);
  return ok;
}

static bool test_headless_json_rejects_empty_stdin(test_context_t *ctx) {
  command_result_t result = cc_run_cli_with_stdin(ctx, NULL, 0, "", NULL, 0);
  const bool ok =
      cc_expect_exit(&result, APP_ERROR_MISSING_ARG) &&
      cc_expect_stderr_contains(&result, "Headless mode expects") &&
      cc_expect_stderr_contains(&result, "\"format_version\":\"1.0\"");
  cc_command_result_free(&result);
  return ok;
}

static bool test_terminal_command_requires_tty(test_context_t *ctx) {
  const char *args[] = {"menu"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_exit(&result, APP_ERROR_IO) &&
      cc_expect_stderr_contains(&result, "requires an interactive terminal");
  cc_command_result_free(&result);
  return ok;
}

static bool test_opencli_contract_matches_checked_in_spec(test_context_t *ctx) {
  const char *args[] = {"opencli"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  char *expected = cc_read_text_file("opencli.json");
  char *binary_name = cc_binary_name(ctx->binary);
  char *normalized_expected = NULL;

  bool ok = cc_expect_exit(&result, 0) &&
            cc_expect_stdout_contains(&result, "\"opencli\": \"0.1\"") &&
            cc_expect_stdout_contains(&result, "\"command\": {") &&
            cc_expect_stdout_contains(&result, "\"commands\": [") &&
            cc_expect_stdout_contains(&result, "\"name\": \"opencli\"");
  if (ok && result.out && strstr(result.out, "\"ordinal\"") != NULL) {
    fprintf(stderr, "opencli output must not contain stale ordinal fields\n");
    ok = false;
  }
  if (!expected) {
    fprintf(stderr, "failed to read opencli.json\n");
    ok = false;
  } else if (!binary_name) {
    fprintf(stderr, "failed to determine binary name\n");
    ok = false;
  } else {
    normalized_expected = strcmp(binary_name, "myapp") == 0
                              ? cc_copy_string(expected)
                              : cc_replace_all(expected, "myapp", binary_name);
    if (!normalized_expected) {
      fprintf(stderr, "failed to normalize opencli.json\n");
      ok = false;
    }
  }

  if (ok) {
    cc_strip_carriage_returns(result.out);
    cc_strip_carriage_returns(normalized_expected);
  }
  if (ok && strcmp(result.out ? result.out : "", normalized_expected) != 0) {
    fprintf(stderr, "opencli command output does not match opencli.json\n");
    ok = false;
  }

  free(normalized_expected);
  free(binary_name);
  free(expected);
  cc_command_result_free(&result);

  {
    const char *json_args[] = {"--json", "opencli"};
    command_result_t json_result =
        cc_run_cli(ctx, json_args, ARRAY_LEN(json_args), NULL, 0);
    ok = cc_expect_exit(&json_result, 0) &&
         cc_expect_stdout_contains(&json_result, "\"command\": {") && ok;
    cc_command_result_free(&json_result);
  }

  return ok;
}

const test_case_t cli_contract_cases[] = {
    {"installed binary starts", test_installed_binary_starts},
    {"help is human readable", test_help_is_human_readable},
    {"builtins render expected output", test_builtins_render_expected_output},
    {"json is default when stdout is not a tty",
     test_json_is_default_when_stdout_is_not_tty},
    {"json info is versioned machine output",
     test_json_info_is_versioned_machine_output},
    {"quiet json commands suppress stdout",
     test_quiet_json_commands_suppress_stdout},
    {"doctor reports binary state", test_doctor_reports_binary_state},
    {"doctor --deep exercises runtime probe",
     test_doctor_deep_option_exercises_runtime_probe},
    {"plain mode disables forced color", test_plain_mode_disables_forced_color},
    {"force color zero disables color", test_force_color_zero_disables_color},
    {"command arguments are not global config flags",
     test_command_arguments_are_not_global_config_flags},
    {"command metadata is enforced", test_command_metadata_is_enforced},
    {"explicit config file failures are visible",
     test_explicit_config_file_failures_are_visible},
    {"verbose mode emits diagnostics on stderr",
     test_verbose_mode_emits_diagnostics_on_stderr},
    {"invalid env config fails without partial settings",
     test_invalid_env_config_fails_without_partial_settings},
    {"valid flat config skips unknown scalar keys",
     test_valid_flat_config_skips_unknown_scalar_keys},
    {"valid config skips nested unknown keys",
     test_valid_config_skips_nested_unknown_keys},
    {"unknown command reports actionable error",
     test_unknown_command_reports_actionable_error},
    {"unknown command suggests the closest match",
     test_unknown_command_suggests_closest},
    {"unknown command far token has no suggestion",
     test_unknown_command_far_token_has_no_suggestion},
    {"unknown command does not suggest hidden commands",
     test_unknown_command_does_not_suggest_hidden},
    {"terminal commands require a tty", test_terminal_command_requires_tty},
    {"headless json request dispatches command",
     test_headless_json_request_dispatches_command},
    {"headless json rejects empty stdin",
     test_headless_json_rejects_empty_stdin},
    {"opencli contract matches checked-in spec",
     test_opencli_contract_matches_checked_in_spec},
};

const size_t cli_contract_cases_count = ARRAY_LEN(cli_contract_cases);
