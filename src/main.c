/*
 * Generic CLI Application Template
 *
 * A modern C23 TUI + CLI starter using Zig as the build system and C
 * toolchain. main() handles bootstrap (logging, config, arg parsing) and
 * defers command behaviour to the table in src/cli/commands.c.
 */

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cli/args.h"
#include "cli/commands.h"
#include "cli/help.h"
#include "core/config.h"
#include "core/error.h"
#include "core/request_json.h"
#include "io/input.h"
#include "io/output.h"
#include "io/terminal.h"
#include "utils/logging.h"
#ifdef APP_ENABLE_CLI_STYLE
#include "cli/style/cli_error_render.h"
#endif

static int64_t app_now_millis(void) {
  struct timespec now;
  if (timespec_get(&now, TIME_UTC) != TIME_UTC) {
    return 0;
  }

  return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static app_error initialize_app(int argc, char *argv[], app_config_t **config) {
  app_error err = app_args_handle_immediate_exit(argc, argv);
  if (err != APP_SUCCESS) {
    return err;
  }

  err = app_config_create(config);
  if (err != APP_SUCCESS) {
    return err;
  }

  const char *program_name = (argc > 0 && argv && argv[0]) ? argv[0] : APP_NAME;
  err = app_config_set_program_name(*config, program_name);
  if (err != APP_SUCCESS) {
    app_config_destroy(*config);
    return err;
  }

  // Layered configuration: file -> env -> args. CLI flags always win.
  const char *explicit_config_path = NULL;
  err = app_args_find_config_path(argc, argv, &explicit_config_path);
  if (err != APP_SUCCESS) {
    app_config_destroy(*config);
    return err;
  }

  err = app_config_load_file(*config, explicit_config_path);
  if (err != APP_SUCCESS) {
    const char *config_label =
        explicit_config_path ? explicit_config_path : "discovered config";
    fprintf(stderr, "Error: failed to load config '%s': %s\n", config_label,
            app_strerror(err));
    app_config_destroy(*config);
    return err;
  }
  err = app_config_load_env(*config);
  if (err != APP_SUCCESS) {
    app_config_destroy(*config);
    return err;
  }

  if (argc > 1) {
    err = app_parse_args(argc, argv, *config);
    if (err != APP_SUCCESS) {
      app_config_destroy(*config);
      return err;
    }
  }

  err = app_config_apply_output_defaults(
      *config, app_terminal_stream_is_tty(APP_TERMINAL_STDOUT));
  if (err != APP_SUCCESS) {
    app_config_destroy(*config);
    return err;
  }

  if (app_config_is_quiet(*config)) {
    app_log_set_level(LOG_LEVEL_ERROR);
  } else if (app_config_is_debug(*config)) {
    app_log_set_level(LOG_LEVEL_DEBUG);
    LOG_DEBUG("Debug mode enabled");
  } else if (app_config_is_verbose(*config)) {
    app_log_set_level(LOG_LEVEL_INFO);
  }

  return APP_SUCCESS;
}

static bool app_is_blank_text(const char *text) {
  if (!text) {
    return true;
  }
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\f' &&
        *p != '\v') {
      return false;
    }
  }
  return true;
}

static app_error app_dispatch_configured_command(app_config_t *config,
                                                 int64_t start_ms) {
  const char *command = app_config_get_command(config);
  if (command == NULL) {
    app_print_concise_help_ex(app_config_get_program_name(config), config);
    return APP_ERROR_INVALID_ARG;
  }

  int cmd_argc = 0;
  char *const *cmd_argv = app_config_get_command_args(config, &cmd_argc);

  const app_command_t *entry = app_command_find(command);
  if (!entry) {
    const char *const program_name = app_config_get_program_name(config);
    const char *const suggestion = app_command_suggest(command);
#ifdef APP_ENABLE_CLI_STYLE
    if (!app_config_is_json_output(config)) {
      // Fold any suggestion into the styled error's detail so the "Did you
      // mean" hint shares the single ERROR block.
      char detail[256];
      if (suggestion) {
        snprintf(detail, sizeof(detail), "%s. Did you mean '%s'?", command,
                 suggestion);
      } else {
        snprintf(detail, sizeof(detail), "%s", command);
      }
      app_cli_render_error_code(config, stderr, program_name,
                                APP_ERROR_INVALID_COMMAND, detail,
                                APP_CLI_ERROR_KIND_USAGE);
      return APP_ERROR_INVALID_COMMAND;
    }
#endif
    // Single message so stderr stays one parseable JSON document in
    // --json/headless mode. (With APP_ENABLE_CLI_STYLE the styled human path
    // returned above; without it this also serves human output, where the
    // combined line reads fine.)
    if (suggestion) {
      app_output_format(config, true,
                        "Unknown command: %s. Did you mean '%s'? Run '%s "
                        "--help' for available commands",
                        command, suggestion, program_name);
    } else {
      app_output_format(config, true,
                        "Unknown command: %s. Run '%s --help' for available "
                        "commands",
                        command, program_name);
    }
    return APP_ERROR_INVALID_COMMAND;
  }

  // Scan for command-local --help/-h, but only before the first standalone
  // "--" delimiter. Tokens after "--" are positionals (matching
  // app_command_validate_invocation), so "myapp echo -- --help" must echo
  // "--help" rather than print help.
  for (int i = 0; i < cmd_argc; i++) {
    if (!cmd_argv[i]) {
      continue;
    }
    if (strcmp(cmd_argv[i], "--") == 0) {
      break;
    }
    if (strcmp(cmd_argv[i], "--help") == 0 || strcmp(cmd_argv[i], "-h") == 0) {
      app_print_command_help_ex(app_config_get_program_name(config), config,
                                entry);
      return APP_SUCCESS;
    }
  }

  if (entry->requires_terminal && !app_terminal_is_interactive()) {
    app_output_format(config, true,
                      "Command '%s' requires an interactive terminal", command);
    return APP_ERROR_IO;
  }

  app_error err = app_command_validate_invocation(
      entry, cmd_argc, cmd_argv, config, app_config_get_program_name(config));
  if (err != APP_SUCCESS) {
    return err;
  }

  // app_config_get_command_args returns char *const * and handlers take
  // char *const argv[] (read-only argv vector), so no const-stripping cast.
  err = entry->handler(config, cmd_argc, cmd_argv);

  int64_t elapsed_ms = app_now_millis() - start_ms;
  if (elapsed_ms < 0) {
    elapsed_ms = 0;
  }
  LOG_INFO("Command '%s' completed in %ld ms with status %d", command,
           (long)elapsed_ms, err);
  return err;
}

static app_error app_run_headless_json(app_config_t *config, int64_t start_ms) {
  // The headless transport envelope is JSON by definition, so force JSON output
  // before anything can emit a diagnostic. docs/CONTRACTS.md requires parse and
  // dispatch errors on stderr as JSON; without this an error reached before the
  // request parses (interactive-stdin guard, blank stdin, malformed JSON) would
  // print human text whenever stdout is a TTY but stdin is piped (e.g.
  // `echo bad | myapp`), since app_config_apply_output_defaults only enables
  // JSON when stdout is not a terminal. The re-apply after parsing additionally
  // stops a request body from downgrading the transport.
  (void)app_config_set_plain_output(config, false);
  (void)app_config_set_json_output(config, true);

  // Reading from a TTY would block forever waiting for input the user has no
  // cue to provide. This path is reached on a bare invocation whenever stdout
  // is not a terminal (e.g. `myapp > out.txt` selects machine output) even
  // though stdin is still the keyboard. Fail fast instead of hanging.
  if (app_terminal_stream_is_tty(APP_TERMINAL_STDIN)) {
    app_output("Headless mode expects a JSON request object on stdin", config,
               true);
    return APP_ERROR_MISSING_ARG;
  }

  char *content = app_read_input_from_stdin();
  if (!content) {
    app_output("Failed to read headless JSON request from stdin", config, true);
    return APP_ERROR_IO;
  }

  if (app_is_blank_text(content)) {
    free(content);
    app_output("Headless mode expects a JSON request object on stdin", config,
               true);
    return APP_ERROR_MISSING_ARG;
  }

  app_request_t request;
  app_request_init(&request);

  app_error err = app_request_parse_json(&request, content);
  if (err != APP_SUCCESS) {
    app_output_format(config, true, "Invalid headless JSON request: %s",
                      app_strerror(err));
    app_request_destroy(&request);
    free(content);
    return err;
  }

  err = app_request_apply_to_config(&request, config);
  if (err != APP_SUCCESS) {
    app_output_format(config, true, "Invalid headless JSON request: %s",
                      app_strerror(err));
    app_request_destroy(&request);
    free(content);
    return err;
  }

  // The transport envelope is JSON by definition. Keep it JSON even if the
  // request includes plain_output for compatibility with normal CLI commands.
  (void)app_config_set_plain_output(config, false);
  (void)app_config_set_json_output(config, true);

  err = app_dispatch_configured_command(config, start_ms);
  app_request_destroy(&request);
  free(content);
  return err;
}

int main(int argc, char *argv[]) {
  /* Initialize the locale from the environment once at startup so multibyte
   * (UTF-8) text layout works on both the pure-CLI and TUI paths. mbrtowc and
   * wcwidth in src/ui/text_layout.c require this; without it the default "C"
   * locale silently degrades to byte counting. The TUI path re-applies the
   * same call in tui_init(), which is an idempotent no-op. */
  setlocale(LC_ALL, "");

  const int64_t start_ms = app_now_millis();

  app_log_init();

  app_config_t *config = NULL;
  app_error err = initialize_app(argc, argv, &config);
  if (err != APP_SUCCESS) {
    return err;
  }

  if (argc == 1) {
    // A bare invocation launches the TUI on an interactive terminal. JSON
    // output (set via config or env, since argc == 1 means no flags) targets
    // machine consumers and conflicts with the interactive TUI. app_run_tui()
    // owns that precondition now and rejects it with the same message `menu`
    // gives, so both entry points stay byte-identical; routing to the headless
    // path here instead would misdescribe the problem as missing stdin input.
    if (app_terminal_is_interactive()) {
      err = app_run_tui(config);
    } else {
      err = app_run_headless_json(config, start_ms);
    }
    app_config_destroy(config);
    return err;
  }

  err = app_dispatch_configured_command(config, start_ms);

  app_config_destroy(config);

  return err;
}
