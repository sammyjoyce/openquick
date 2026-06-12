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
#include <sys/stat.h>
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
      cc_expect_stdout_contains(&result, "QUICK_LOG_LEVEL") &&
      // ENVIRONMENT is rendered from the single canonical table the
      // OpenCLI contract publishes, so the color env vars and
      // QUICK_CONFIG_PATH appear here just as they do in opencli.json.
      cc_expect_stdout_contains(&result, "FORCE_COLOR") &&
      cc_expect_stdout_contains(&result, "NO_COLOR") &&
      cc_expect_stdout_contains(&result, "QUICK_CONFIG_PATH");
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
    const char *args[] = {"--plain", "info"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "Application:") &&
         cc_expect_stdout_contains(&result, "Version:") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"--plain", "doctor"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "quick_version") && ok;
    cc_command_result_free(&result);
  }

  return ok;
}

static bool test_json_is_default_when_stdout_is_not_tty(test_context_t *ctx) {
  const char *args[] = {"info"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
      cc_expect_stdout_contains(&result, "\"app\":\"quick\"");
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
                  cc_expect_stdout_contains(&result, "quick_version");
  cc_command_result_free(&result);
  return ok;
}

static bool test_doctor_deep_option_exercises_runtime_probe(
    test_context_t *ctx) {
  const char *args[] = {"--json", "doctor", "--deep"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  bool ok = cc_expect_exit(&result, 0) &&
            cc_expect_stdout_contains(&result, "\"name\":\"deep_temp_deploy\"") &&
            cc_expect_stdout_contains(&result, "\"group\":\"edge/iap\"");
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
      cc_expect_stdout_contains(&result, "quick_version");
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
      cc_expect_stdout_contains(&result, "quick_version");
  cc_command_result_free(&result);
  return ok;
}

static bool test_command_arguments_are_not_global_config_flags(
    test_context_t *ctx) {
  const char *args[] = {"--plain", "deploy", "-c",
                        "/definitely/not/a/config.json", "--dry-run"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) && cc_expect_stdout_contains(&result, "-c");
  cc_command_result_free(&result);
  return ok;
}

static bool test_site_admin_help_is_registered(test_context_t *ctx) {
  bool ok = true;
  {
    const char *args[] = {"delete", "--help"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "delete") &&
         cc_expect_stdout_contains(&result, "--yes") && ok;
    cc_command_result_free(&result);
  }
  {
    const char *args[] = {"public", "--help"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "public") &&
         cc_expect_stdout_contains(&result, "on or off") && ok;
    cc_command_result_free(&result);
  }
  {
    const char *args[] = {"domain", "--help"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "domain") &&
         cc_expect_stdout_contains(&result, "add, remove, or list") && ok;
    cc_command_result_free(&result);
  }
  return ok;
}

static bool test_command_metadata_is_enforced(test_context_t *ctx) {
  bool ok = true;

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
    const char *args[] = {"--plain", "open", "--", "--help"};
    const env_var_t env[] = {{"QUICK_BASE_DOMAIN", "quick.example.com"}};
    command_result_t result =
        cc_run_cli(ctx, args, ARRAY_LEN(args), env, ARRAY_LEN(env));
    const bool printed_help =
        result.out != NULL && strstr(result.out, "USAGE") != NULL;
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "https://help.quick.example.com") &&
         !printed_help && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"init", "--help"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "USAGE") &&
         cc_expect_stdout_contains(&result, "init") && ok;
    cc_command_result_free(&result);
  }

  return ok;
}

static bool test_explicit_config_file_failures_are_visible(
    test_context_t *ctx) {
  const char *args[] = {"--config", "/definitely/not/a/config.json", "info"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok =
      cc_expect_not_exit(&result, 0) &&
      cc_expect_stderr_contains(&result, "failed to load config") &&
      cc_expect_stderr_contains(&result, "/definitely/not/a/config.json");
  cc_command_result_free(&result);
  return ok;
}

static bool test_verbose_mode_emits_diagnostics_on_stderr(test_context_t *ctx) {
  const char *args[] = {"--plain", "--verbose", "info"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok = cc_expect_exit(&result, 0) &&
                  cc_expect_stdout_contains(&result, "Application:") &&
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

  const char *args[] = {"info"};
  const env_var_t env[] = {{"QUICK_CONFIG_PATH", config_path}};
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
  // flag (quiet here), so info succeeds with suppressed output.
  char *config_path = NULL;
  if (!cc_write_temp_config(
          "{\"ignored\":{\"a\":[1,{\"b\":2}]},\"quiet\":true}", &config_path)) {
    fprintf(stderr, "failed to write temporary config\n");
    return false;
  }

  const char *args[] = {"--config", config_path, "info"};
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

  const char *args[] = {"--config", config_path, "info"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok = cc_expect_exit(&result, 0) && cc_expect_stdout_empty(&result);
  cc_command_result_free(&result);
  (void)unlink(config_path);
  free(config_path);
  return ok;
}

#ifndef _WIN32
static bool write_text_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    return false;
  }
  const size_t len = strlen(content);
  const bool ok = fwrite(content, 1, len, f) == len;
  return fclose(f) == 0 && ok;
}

static bool write_executable_file(const char *path, const char *content) {
  if (!write_text_file(path, content)) {
    return false;
  }
  return chmod(path, 0755) == 0;
}

static bool make_site_dir(char *dir, const char *quick_json) {
  if (!mkdtemp(dir)) {
    return false;
  }
  char index_path[512];
  char quick_path[512];
  snprintf(index_path, sizeof(index_path), "%s/index.html", dir);
  snprintf(quick_path, sizeof(quick_path), "%s/quick.json", dir);
  return write_text_file(index_path, "<!doctype html><title>test</title>\n") &&
         write_text_file(quick_path, quick_json);
}
#endif

static bool test_deploy_bootstrap_remediation_path(test_context_t *ctx) {
#ifndef _WIN32
  char site_dir[] = "/tmp/openquick-bootstrap-site-XXXXXX";
  if (!make_site_dir(site_dir,
                     "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\",\"profile\":\"lab\"}")) {
    return false;
  }
  char bin_dir[] = "/tmp/openquick-bootstrap-bin-XXXXXX";
  if (!mkdtemp(bin_dir)) {
    return false;
  }
  char ssh_path[512];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  if (!write_executable_file(ssh_path,
                             "#!/bin/sh\n"
                             "echo 'ssh: quickd: command not found' >&2\n"
                             "exit 127\n")) {
    return false;
  }
  const char *args[] = {"deploy", site_dir};
  char *path_env = cc_format_string("%s", bin_dir);
  const env_var_t env[] = {{"PATH", path_env},
                           {"QUICK_REMOTE", "quick@box"},
                           {"QUICK_BASE_DOMAIN", "quick.example.com"}};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                       ARRAY_LEN(env));
  bool ok = cc_expect_not_exit(&result, 0) &&
            cc_expect_stderr_contains(
                &result,
                "quick serve install --profile lab --host quick@box") &&
            cc_expect_stderr_contains(&result, "--remote-root /srv/quick") &&
            cc_expect_stderr_contains(&result, "--domain quick.example.com");
  cc_command_result_free(&result);
  free(path_env);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_deploy_allow_unpublished_gating(test_context_t *ctx) {
#ifndef _WIN32
  char site_dir[] = "/tmp/openquick-unpublished-site-XXXXXX";
  if (!make_site_dir(site_dir,
                     "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\",\"profile\":\"lab\"}")) {
    return false;
  }
  char bin_dir[] = "/tmp/openquick-unpublished-bin-XXXXXX";
  if (!mkdtemp(bin_dir)) {
    return false;
  }
  char ssh_path[512];
  char rsync_path[512];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(rsync_path, sizeof(rsync_path), "%s/rsync", bin_dir);
  const char *ssh_script =
      "#!/bin/sh\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = doctor ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"ok\":true,\"checks\":[{\"name\":\"domain\",\"group\":\"edge/iap\",\"status\":\"warn\",\"detail\":\"missing\",\"remediation\":\"configure\"}]}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = prepare ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"site\":\"demo\",\"deploy_id\":\"20260612T000000Z-abcdef\",\"staging_path\":\"/tmp/openquick-stage\",\"link_dest\":null}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = activate ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"site\":\"demo\",\"release\":\"20260612T000000Z-abcdef\",\"url\":\"https://demo.quick.example.com\"}'\n"
      "  exit 0\n"
      "fi\n"
      "exit 1\n";
  if (!write_executable_file(ssh_path, ssh_script) ||
      !write_executable_file(rsync_path, "#!/bin/sh\nexit 0\n")) {
    return false;
  }
  char *path_env = cc_format_string("%s", bin_dir);
  const env_var_t env[] = {{"PATH", path_env},
                           {"QUICK_REMOTE", "quick@box"},
                           {"QUICK_BASE_DOMAIN", "quick.example.com"}};
  const char *blocked_args[] = {"deploy", site_dir};
  command_result_t blocked = cc_run_cli(ctx, blocked_args,
                                        ARRAY_LEN(blocked_args), env,
                                        ARRAY_LEN(env));
  bool ok = cc_expect_exit(&blocked, APP_ERROR_VALIDATION) &&
            cc_expect_stderr_contains(&blocked, "--allow-unpublished");
  cc_command_result_free(&blocked);

  const char *allowed_args[] = {"--json", "deploy", site_dir,
                                "--allow-unpublished"};
  command_result_t allowed = cc_run_cli(ctx, allowed_args,
                                        ARRAY_LEN(allowed_args), env,
                                        ARRAY_LEN(env));
  ok = cc_expect_exit(&allowed, 0) &&
       cc_expect_stdout_contains(&allowed, "\"release\":\"20260612T000000Z-abcdef\"") && ok;
  cc_command_result_free(&allowed);
  free(path_env);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_site_admin_commands_use_ssh_contract(test_context_t *ctx) {
#ifndef _WIN32
  char bin_dir[] = "/tmp/openquick-admin-bin-XXXXXX";
  if (!mkdtemp(bin_dir)) {
    return false;
  }
  char ssh_path[512];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  const char *ssh_script =
      "#!/bin/sh\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = sites ] && [ \"$4\" = get ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"name\":\"demo\",\"subdomain\":\"demo\",\"url\":\"https://demo.quick.example.com\",\"release\":\"rel1\",\"deployer\":\"alice\",\"public\":false}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = sites ] && [ \"$4\" = delete ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"site\":\"demo\",\"deleted\":true}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = sites ] && [ \"$4\" = public ]; then\n"
      "  if [ \"$6\" = --on ]; then printf '%s\\n' '{\"format_version\":\"1.0\",\"site\":\"demo\",\"public\":true}'; else printf '%s\\n' '{\"format_version\":\"1.0\",\"site\":\"demo\",\"public\":false}'; fi\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = domains ] && [ \"$4\" = list ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"domains\":[{\"domain\":\"app.example.com\",\"site\":\"demo\"}]}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = domains ] && [ \"$4\" = add ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"domain\":\"app.example.com\",\"site\":\"demo\"}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = domains ] && [ \"$4\" = remove ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"domain\":\"app.example.com\",\"removed\":true}'\n"
      "  exit 0\n"
      "fi\n"
      "exit 1\n";
  if (!write_executable_file(ssh_path, ssh_script)) {
    return false;
  }
  char *path_env = cc_format_string("%s", bin_dir);
  const env_var_t env[] = { {"PATH", path_env},
                            {"QUICK_REMOTE", "quick@box"},
                            {"QUICK_BASE_DOMAIN", "quick.example.com"} };
  bool ok = true;
  {
    const char *args[] = {"delete", "demo"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                         ARRAY_LEN(env));
    ok = cc_expect_exit(&result, APP_ERROR_VALIDATION) &&
         cc_expect_stderr_contains(&result, "requires typing the site name") && ok;
    cc_command_result_free(&result);
  }
  {
    const char *args[] = {"--json", "delete", "demo", "--yes"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                         ARRAY_LEN(env));
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "\"deleted\":true") && ok;
    cc_command_result_free(&result);
  }
  {
    const char *args[] = {"--json", "public", "demo"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                         ARRAY_LEN(env));
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "\"public\":false") && ok;
    cc_command_result_free(&result);
  }
  {
    const char *args[] = {"--json", "public", "demo", "on", "--yes"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                         ARRAY_LEN(env));
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "\"public\":true") && ok;
    cc_command_result_free(&result);
  }
  {
    const char *args[] = {"--json", "domain", "list"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                         ARRAY_LEN(env));
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "\"domains\"") && ok;
    cc_command_result_free(&result);
  }
  {
    const char *args[] = {"--json", "domain", "add", "app.example.com",
                          "--site", "demo"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                         ARRAY_LEN(env));
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "\"domain\":\"app.example.com\"") && ok;
    cc_command_result_free(&result);
  }
  free(path_env);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_deploy_overwrite_requires_yes_when_headless(test_context_t *ctx) {
#ifndef _WIN32
  char site_dir[] = "/tmp/openquick-overwrite-site-XXXXXX";
  if (!make_site_dir(site_dir,
                     "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\",\"profile\":\"lab\"}")) {
    return false;
  }
  char bin_dir[] = "/tmp/openquick-overwrite-bin-XXXXXX";
  if (!mkdtemp(bin_dir)) {
    return false;
  }
  char ssh_path[512];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  const char *ssh_script =
      "#!/bin/sh\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = doctor ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"ok\":true,\"checks\":[]}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = prepare ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"site\":\"demo\",\"deploy_id\":\"20260612T000000Z-abcdef\",\"staging_path\":\"/tmp/openquick-stage\",\"link_dest\":null,\"last_deployer\":\"bob\",\"last_release\":\"oldrel\",\"last_deployed_at\":\"2026-06-12T00:00:00Z\"}'\n"
      "  exit 0\n"
      "fi\n"
      "exit 1\n";
  if (!write_executable_file(ssh_path, ssh_script)) {
    return false;
  }
  char *path_env = cc_format_string("%s", bin_dir);
  const char *args[] = {"deploy", site_dir, "--site", "demo",
                        "--allow-unpublished", "--no-build"};
  const env_var_t env[] = { {"PATH", path_env},
                            {"QUICK_REMOTE", "quick@box"},
                            {"QUICK_BASE_DOMAIN", "quick.example.com"},
                            {"USER", "alice"} };
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                       ARRAY_LEN(env));
  bool ok = cc_expect_exit(&result, APP_ERROR_VALIDATION) &&
            cc_expect_stderr_contains(&result, "last deployed by bob") &&
            cc_expect_stderr_contains(&result, "--yes");
  cc_command_result_free(&result);
  free(path_env);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_zip_deploy_uses_scp_extract_activate(test_context_t *ctx) {
#ifndef _WIN32
  char root_dir[] = "/tmp/openquick-zip-root-XXXXXX";
  char bin_dir[] = "/tmp/openquick-zip-bin-XXXXXX";
  if (!mkdtemp(root_dir) || !mkdtemp(bin_dir)) {
    return false;
  }
  char zip_path[512];
  snprintf(zip_path, sizeof(zip_path), "%s/site.zip", root_dir);
  if (!write_text_file(zip_path, "fake zip bytes\n")) {
    return false;
  }
  char scp_log[512];
  snprintf(scp_log, sizeof(scp_log), "%s/scp.log", root_dir);
  char ssh_path[512], scp_path[512];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(scp_path, sizeof(scp_path), "%s/scp", bin_dir);
  const char *ssh_script =
      "#!/bin/sh\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = doctor ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"ok\":true,\"checks\":[]}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = prepare ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"site\":\"demo\",\"deploy_id\":\"ziprel\",\"staging_path\":\"/tmp/openquick-stage\",\"link_dest\":null,\"last_deployer\":null,\"last_release\":null,\"last_deployed_at\":null}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = extract-zip ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"ok\":true}'\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = activate ]; then\n"
      "  printf '%s\\n' '{\"format_version\":\"1.0\",\"site\":\"demo\",\"release\":\"ziprel\",\"url\":\"https://demo.quick.example.com\"}'\n"
      "  exit 0\n"
      "fi\n"
      "exit 1\n";
  char *scp_script = cc_format_string("#!/bin/sh\nprintf '%%s %%s\\n' \"$1\" \"$2\" > '%s'\nexit 0\n", scp_log);
  if (!scp_script || !write_executable_file(ssh_path, ssh_script) ||
      !write_executable_file(scp_path, scp_script)) {
    free(scp_script);
    return false;
  }
  free(scp_script);
  char *path_env = cc_format_string("%s", bin_dir);
  const char *args[] = {"--json", "deploy", zip_path, "--site", "demo",
                        "--yes", "--allow-unpublished", "--no-build"};
  const env_var_t env[] = { {"PATH", path_env},
                            {"QUICK_REMOTE", "quick@box"},
                            {"QUICK_BASE_DOMAIN", "quick.example.com"},
                            {"USER", "alice"} };
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                       ARRAY_LEN(env));
  char *scp_seen = cc_read_text_file(scp_log);
  bool ok = cc_expect_exit(&result, 0) &&
            cc_expect_stdout_contains(&result, "\"release\":\"ziprel\"") &&
            scp_seen && strstr(scp_seen, "upload.zip") != NULL;
  if (!scp_seen) {
    fprintf(stderr, "scp stub was not invoked\n");
  }
  free(scp_seen);
  cc_command_result_free(&result);
  free(path_env);
  unlink(zip_path);
  unlink(scp_log);
  unlink(ssh_path);
  unlink(scp_path);
  rmdir(bin_dir);
  rmdir(root_dir);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_doctor_deep_skip_without_remote(test_context_t *ctx) {
#ifndef _WIN32
  char cfg_home[] = "/tmp/openquick-doctor-config-XXXXXX";
  if (!mkdtemp(cfg_home)) {
    return false;
  }
  const char *args[] = {"--json", "doctor", "--deep"};
  const env_var_t env[] = {{"QUICK_REMOTE", NULL},
                           {"QUICK_BASE_DOMAIN", NULL},
                           {"QUICK_PROFILE", "lab"},
                           {"XDG_CONFIG_HOME", cfg_home}};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                       ARRAY_LEN(env));
  bool ok = cc_expect_exit(&result, 0) &&
            cc_expect_stdout_contains(&result, "\"name\":\"deep_temp_deploy\"") &&
            cc_expect_stdout_contains(&result, "\"status\":\"skip\"") &&
            cc_expect_stdout_contains(&result, "no SSH host resolved");
  cc_command_result_free(&result);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_open_copy_uses_clipboard_tool(test_context_t *ctx) {
#ifndef _WIN32
  char bin_dir[] = "/tmp/openquick-copy-bin-XXXXXX";
  if (!mkdtemp(bin_dir)) {
    return false;
  }
  char tool_path[512];
  char copied_path[512];
#ifdef __APPLE__
  snprintf(tool_path, sizeof(tool_path), "%s/pbcopy", bin_dir);
#else
  snprintf(tool_path, sizeof(tool_path), "%s/wl-copy", bin_dir);
#endif
  snprintf(copied_path, sizeof(copied_path), "%s/copied.txt", bin_dir);
  char *script = cc_format_string("#!/bin/sh\n/bin/cat > '%s'\n", copied_path);
  if (!script || !write_executable_file(tool_path, script)) {
    free(script);
    return false;
  }
  free(script);
  char *path_env = cc_format_string("%s", bin_dir);
  const char *args[] = {"open", "demo", "--plain", "--copy"};
  const env_var_t env[] = {{"PATH", path_env},
                           {"QUICK_BASE_DOMAIN", "quick.example.com"},
                           {"XDG_CONFIG_HOME", bin_dir}};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                       ARRAY_LEN(env));
  char *copied = cc_read_text_file(copied_path);
  bool ok = cc_expect_exit(&result, 0) &&
            cc_expect_stdout_contains(&result, "https://demo.quick.example.com") &&
            copied && strcmp(copied, "https://demo.quick.example.com") == 0;
  if (!copied) {
    fprintf(stderr, "clipboard stub did not receive URL\n");
  }
  free(copied);
  cc_command_result_free(&result);
  free(path_env);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_serve_dev_remote_api_mints_token_and_execs_quickd(
    test_context_t *ctx) {
#ifndef _WIN32
  char bin_dir[] = "/tmp/openquick-serve-remote-api-bin-XXXXXX";
  if (!mkdtemp(bin_dir)) {
    return false;
  }
  char ssh_path[512];
  char quickd_path[512];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(quickd_path, sizeof(quickd_path), "%s/quickd", bin_dir);
  const char *ssh_script =
      "#!/bin/sh\n"
      "if [ \"$1\" = quick@box ] && [ \"$2\" = quickd ] && "
      "[ \"$3\" = admin ] && [ \"$4\" = mint-dev-token ] && "
      "[ \"$5\" = --site ] && [ \"$6\" = demo ] && "
      "[ \"$7\" = --ttl ] && [ \"$8\" = 3600 ] && "
      "[ \"$9\" = --json ]; then\n"
      "  printf '%s\\n' '{\"token\":\"dev-token-123\",\"site\":\"demo\",\"expires_at\":\"2026-06-12T01:00:00Z\"}'\n"
      "  exit 0\n"
      "fi\n"
      "printf 'unexpected ssh args:' >&2\n"
      "for arg in \"$@\"; do printf ' [%s]' \"$arg\" >&2; done\n"
      "printf '\\n' >&2\n"
      "exit 1\n";
  const char *quickd_script =
      "#!/bin/sh\n"
      "printf 'quickd argv:'\n"
      "for arg in \"$@\"; do printf ' [%s]' \"$arg\"; done\n"
      "printf '\\n'\n"
      "exit 0\n";
  if (!write_executable_file(ssh_path, ssh_script) ||
      !write_executable_file(quickd_path, quickd_script)) {
    return false;
  }
  const char *args[] = {"serve", "--dev", "--remote-api", "lab",
                        "--port", "9456"};
  const env_var_t env[] = {{"PATH", bin_dir},
                           {"QUICK_QUICKD", quickd_path},
                           {"QUICK_REMOTE", "quick@box"},
                           {"QUICK_BASE_DOMAIN", "quick.example.com"},
                           {"QUICK_SITE", "demo"},
                           {"XDG_CONFIG_HOME", bin_dir}};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                       ARRAY_LEN(env));
  bool ok = cc_expect_exit(&result, 0) &&
            cc_expect_stdout_contains(&result, "[--remote-api]") &&
            cc_expect_stdout_contains(&result,
                                      "[https://demo.quick.example.com]") &&
            cc_expect_stdout_contains(&result, "[--remote-api-token]") &&
            cc_expect_stdout_contains(&result, "[dev-token-123]");
  cc_command_result_free(&result);
  unlink(ssh_path);
  unlink(quickd_path);
  rmdir(bin_dir);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_serve_install_guided_output(test_context_t *ctx) {
#ifndef _WIN32
  char cfg_home[] = "/tmp/openquick-serve-config-XXXXXX";
  if (!mkdtemp(cfg_home)) {
    return false;
  }
  const char *args[] = {"--plain", "serve", "install", "--profile", "lab",
                        "--host", "quick@box", "--remote-root", "/srv/quick",
                        "--domain", "quick.example.com", "--iap", "tailscale"};
  const env_var_t env[] = {{"XDG_CONFIG_HOME", cfg_home}};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), env,
                                       ARRAY_LEN(env));
  bool ok = cc_expect_exit(&result, 0) &&
            cc_expect_stdout_contains(&result, "OpenQuick host install plan") &&
            cc_expect_stdout_contains(&result, "create quick user") &&
            cc_expect_stdout_contains(&result, "write /etc/openquick/quickd.json") &&
            cc_expect_stdout_contains(&result, "quickd doctor --host --json");
  cc_command_result_free(&result);
  return ok;
#else
  (void)ctx;
  return true;
#endif
}

static bool test_openquick_site_workflow_contracts(test_context_t *ctx) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-cli-site-XXXXXX";
  if (!mkdtemp(dir)) {
    fprintf(stderr, "failed to create temp site dir\n");
    return false;
  }
  char quick_json[512];
  char agents_md[512];
  char api_md[512];
  snprintf(quick_json, sizeof(quick_json), "%s/quick.json", dir);
  snprintf(agents_md, sizeof(agents_md), "%s/AGENTS.md", dir);
  snprintf(api_md, sizeof(api_md), "%s/docs/openquick-api.md", dir);
  bool ok = true;

  {
    const char *args[] = {"--plain", "init", dir, "--name", "Lunch Vote",
                          "--profile", "lab"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    char *agents_text = cc_read_text_file(agents_md);
    char *api_text = cc_read_text_file(api_md);
    const bool scaffold_ok =
        agents_text && api_text &&
        strstr(agents_text, "OpenQuick site agent guide") &&
        strstr(agents_text, "quick deploy --dry-run") &&
        strstr(api_text, "quick.warehouse.query(name, params)");
    if (!scaffold_ok) {
      fprintf(stderr, "generated scaffold docs missing expected guidance\n");
    }
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "Initialized OpenQuick site") &&
         cc_file_exists(quick_json) && scaffold_ok && ok;
    free(agents_text);
    free(api_text);
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"--json", "deploy", dir, "--dry-run", "--site",
                          "lunch-vote"};
    const env_var_t env[] = {{"QUICK_REMOTE", "quick@box"},
                             {"QUICK_BASE_DOMAIN", "quick.example.com"}};
    command_result_t result =
        cc_run_cli(ctx, args, ARRAY_LEN(args), env, ARRAY_LEN(env));
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
         cc_expect_stdout_contains(&result, "\"site\":\"lunch-vote\"") &&
         cc_expect_stdout_contains(&result, "rsync") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"open", "lunch-vote", "--plain"};
    const env_var_t env[] = {{"QUICK_BASE_DOMAIN", "quick.example.com"}};
    command_result_t result =
        cc_run_cli(ctx, args, ARRAY_LEN(args), env, ARRAY_LEN(env));
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result,
                                   "https://lunch-vote.quick.example.com") &&
         cc_expect_stdout_contains(&result, "not yet deployed") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"list", "--json"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
         cc_expect_stdout_contains(&result, "\"sites\":[]") && ok;
    cc_command_result_free(&result);
  }

  {
    const char *args[] = {"doctor", "--json"};
    command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
    ok = cc_expect_exit(&result, 0) &&
         cc_expect_stdout_contains(&result, "\"checks\"") &&
         cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
         ok;
    cc_command_result_free(&result);
  }

  return ok;
#else
  (void)ctx;
  return true;
#endif
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
  const char *args[] = {"ope"};
  command_result_t result = cc_run_cli(ctx, args, ARRAY_LEN(args), NULL, 0);
  const bool ok = cc_expect_not_exit(&result, 0) &&
                  cc_expect_stderr_contains(&result, "Unknown command: ope") &&
                  cc_expect_stderr_contains(&result, "Did you mean 'open'?");
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
      ctx, NULL, 0, "{\"command\":\"info\",\"args\":[]}", NULL, 0);
  const bool ok =
      cc_expect_exit(&result, 0) &&
      cc_expect_stdout_contains(&result, "\"format_version\":\"1.0\"") &&
      cc_expect_stdout_contains(&result, "\"app\":\"quick\"");
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
    normalized_expected = strcmp(binary_name, "quick") == 0
                              ? cc_copy_string(expected)
                              : cc_replace_all(expected, "quick", binary_name);
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
    {"site admin help is registered", test_site_admin_help_is_registered},
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
    {"deploy bootstrap remediation path",
     test_deploy_bootstrap_remediation_path},
    {"deploy --allow-unpublished gates publication warnings",
     test_deploy_allow_unpublished_gating},
    {"site admin commands use ssh contract",
     test_site_admin_commands_use_ssh_contract},
    {"deploy overwrite requires yes when headless",
     test_deploy_overwrite_requires_yes_when_headless},
    {"zip deploy uses scp extract activate",
     test_zip_deploy_uses_scp_extract_activate},
    {"doctor --deep skips clearly without remote",
     test_doctor_deep_skip_without_remote},
    {"open --copy uses clipboard tool",
     test_open_copy_uses_clipboard_tool},
    {"serve --dev remote API mints token and execs quickd",
     test_serve_dev_remote_api_mints_token_and_execs_quickd},
    {"serve install guided output",
     test_serve_install_guided_output},
    {"OpenQuick init/deploy/open/list/doctor contracts",
     test_openquick_site_workflow_contracts},
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
