#include "terminal_vt_scenarios.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "terminal_vt_session.h"

typedef enum {
  TUI_STEP_EXPECT,
  TUI_STEP_SEND,
  TUI_STEP_RESIZE,
  TUI_STEP_WAIT_EXIT,
} tui_step_kind_t;

typedef struct {
  tui_step_kind_t kind;
  const char *value;
  int menu_index;
  uint16_t cols;
  uint16_t rows;
  int timeout_ms;
  const char *failure;
} tui_step_t;

static int utf8_columns_until(const char *text, size_t len) {
  int cols = 0;
  for (size_t i = 0; i < len;) {
    const unsigned char ch = (unsigned char)text[i];
    if (ch < 0x80) {
      i++;
    } else {
      i++;
      while (i < len && (((unsigned char)text[i] & 0xC0) == 0x80)) {
        i++;
      }
    }
    cols++;
  }
  return cols;
}

static const char *find_line_containing(const char *text, const char *needle,
                                        size_t *line_len) {
  const char *line = text;
  while (line && *line) {
    const char *next = strchr(line, '\n');
    const size_t len = next ? (size_t)(next - line) : strlen(line);
    char *copy = calloc(len + 1, 1);
    if (!copy) {
      return NULL;
    }
    memcpy(copy, line, len);
    const bool found = strstr(copy, needle) != NULL;
    free(copy);
    if (found) {
      *line_len = len;
      return line;
    }
    line = next ? next + 1 : NULL;
  }
  return NULL;
}

/* The menu is borderless (dawn-style): the title is centered text rather than
 * a label on a box edge. Verify the title is horizontally centered within a
 * frame of `expected_width` positioned at `expected_left` - this still proves
 * the frame recentered/resized correctly. */
static bool snapshot_has_menu_frame(const char *snapshot, int expected_left,
                                    int expected_width) {
  static const char title[] = "OPENQUICK";
  size_t line_len = 0;
  const char *line = find_line_containing(snapshot, title, &line_len);
  if (!line) {
    return false;
  }

  const char *title_at = strstr(line, title);
  if (!title_at || title_at >= line + line_len) {
    return false;
  }
  const int title_col = utf8_columns_until(line, (size_t)(title_at - line));
  const int expected_title_col =
      expected_left + (expected_width - (int)strlen(title)) / 2;
  return title_col == expected_title_col;
}

static bool vt_expect_menu_frame(vt_session_t *session, int expected_left,
                                 int expected_width, int timeout_ms,
                                 char **snapshot) {
  const int64_t deadline = monotonic_ms() + timeout_ms;
  while (monotonic_ms() <= deadline) {
    if (vt_expect_text(session, "OPENQUICK", 100, snapshot) &&
        snapshot_has_menu_frame(*snapshot, expected_left, expected_width)) {
      return true;
    }
  }
  return false;
}

static int run_tui_step(test_stats_t *stats, const char *test_name,
                        vt_session_t *session, const tui_step_t *step,
                        char **snapshot) {
  switch (step->kind) {
  case TUI_STEP_EXPECT:
    if (vt_expect_text(session, step->value, step->timeout_ms, snapshot)) {
      return 0;
    }
    print_tail(stderr, "screen:\n", *snapshot ? *snapshot : "",
               *snapshot ? strlen(*snapshot) : 0, 4000);
    print_tail(stderr, "transcript:\n", buffer_cstr(&session->transcript),
               session->transcript.len, 4000);
    return test_fail(stats, test_name, "%s", step->failure);
  case TUI_STEP_SEND:
    return vt_send(session, step->value)
               ? 0
               : test_fail(stats, test_name, "%s", step->failure);
  case TUI_STEP_RESIZE:
    return vt_resize(session, step->cols, step->rows)
               ? 0
               : test_fail(stats, test_name, "%s", step->failure);
  case TUI_STEP_WAIT_EXIT: {
    const int exit_code = vt_wait_for_exit(session, step->timeout_ms);
    return exit_code == 0 ? 0
                          : test_fail(stats, test_name,
                                      "expected exit 0, got %d", exit_code);
  }
  }
  return test_fail(stats, test_name, "unknown TUI step");
}

typedef struct {
  const char *name;
  char *old_value;
  bool had_value;
} env_guard_t;

static void env_guard_set(env_guard_t *guard, const char *name,
                          const char *value) {
  guard->name = name;
  const char *old = getenv(name);
  guard->had_value = old != NULL;
  guard->old_value = old ? strdup(old) : NULL;
  if (value) {
    setenv(name, value, 1);
  } else {
    unsetenv(name);
  }
}

static void env_guard_restore(env_guard_t *guard) {
  if (!guard || !guard->name) {
    return;
  }
  if (guard->had_value) {
    setenv(guard->name, guard->old_value ? guard->old_value : "", 1);
  } else {
    unsetenv(guard->name);
  }
  free(guard->old_value);
  *guard = (env_guard_t){0};
}

static bool make_temp_dir_path(char *tmpl) {
  return mkdtemp(tmpl) != NULL;
}

static bool write_text_file(const char *path, const char *body) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    return false;
  }
  const size_t len = strlen(body);
  bool ok = fwrite(body, 1, len, f) == len;
  return fclose(f) == 0 && ok;
}

static bool ensure_dir(const char *path) {
  return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static bool write_profile_config(const char *xdg_dir) {
  char openquick[PATH_MAX];
  char config[PATH_MAX];
  snprintf(openquick, sizeof(openquick), "%s/openquick", xdg_dir);
  if (!ensure_dir(openquick)) {
    return false;
  }
  snprintf(config, sizeof(config), "%s/config.json", openquick);
  return write_text_file(
      config,
      "{\n"
      "  \"default_profile\": \"lab\",\n"
      "  \"profiles\": {\n"
      "    \"lab\": {\n"
      "      \"ssh\": \"quick@box\",\n"
      "      \"remote_root\": \"/srv/quick\",\n"
      "      \"base_domain\": \"quick.example.com\",\n"
      "      \"base_url\": \"https://quick.example.com\",\n"
      "      \"iap\": {\"type\": \"tailscale\", \"mode\": \"localapi\"},\n"
      "      \"deploy\": {\"delete\": true, \"open_after_deploy\": false}\n"
      "    }\n"
      "  }\n"
      "}\n");
}

static void restore_common_env(env_guard_t guards[], size_t count) {
  for (size_t i = 0; i < count; i++) {
    env_guard_restore(&guards[i]);
  }
}

int run_tui_menu_test(test_stats_t *stats, const char *binary,
                      bool tui_enabled) {
  const char *name = "product TUI main menu and help render through Ghostty VT";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }

  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 80, 24)) {
    return test_fail(stats, name, "failed to start PTY session");
  }

  const tui_step_t steps[] = {
      {TUI_STEP_EXPECT, "OPENQUICK", 0, 0, 0, PTY_TIMEOUT_MS,
       "OPENQUICK did not appear"},
      {TUI_STEP_EXPECT, "Sites", 0, 0, 0, 1000,
       "menu label did not appear: Sites"},
      {TUI_STEP_EXPECT, "Deploy", 0, 0, 0, 1000,
       "menu label did not appear: Deploy"},
      {TUI_STEP_EXPECT, "New site", 0, 0, 0, 1000,
       "menu label did not appear: New site"},
      {TUI_STEP_EXPECT, "Doctor", 0, 0, 0, 1000,
       "menu label did not appear: Doctor"},
      {TUI_STEP_EXPECT, "Serve", 0, 0, 0, 1000,
       "menu label did not appear: Serve"},
      {TUI_STEP_EXPECT, "Settings", 0, 0, 0, 1000,
       "menu label did not appear: Settings"},
      {TUI_STEP_EXPECT, "Help/About", 0, 0, 0, 1000,
       "menu label did not appear: Help/About"},
      {TUI_STEP_EXPECT, "Exit", 0, 0, 0, 1000,
       "menu label did not appear: Exit"},

      {TUI_STEP_SEND, "h", 0, 0, 0, 0, "failed to select Help/About"},
      {TUI_STEP_EXPECT, "HELP/ABOUT", 0, 0, 0, PTY_TIMEOUT_MS,
       "Help/About menu did not appear"},
      {TUI_STEP_SEND, "k", 0, 0, 0, 0, "failed to select Key bindings"},
      {TUI_STEP_EXPECT, "KEY BINDINGS", 0, 0, 0, PTY_TIMEOUT_MS,
       "Key Bindings did not appear"},
      {TUI_STEP_EXPECT, "Up / Down", 0, 0, 0, 1000,
       "key binding body did not appear"},
      {TUI_STEP_SEND, "x", 0, 0, 0, 0, "failed to dismiss key bindings"},
      {TUI_STEP_EXPECT, "HELP/ABOUT", 0, 0, 0, PTY_TIMEOUT_MS,
       "Help/About did not reappear"},
      {TUI_STEP_SEND, "a", 0, 0, 0, 0, "failed to select About"},
      {TUI_STEP_EXPECT, "ABOUT OPENQUICK", 0, 0, 0, PTY_TIMEOUT_MS,
       "About OpenQuick did not appear"},
      {TUI_STEP_EXPECT, "SSH+rsync", 0, 0, 0, 1000,
       "about body did not appear"},
      {TUI_STEP_SEND, "x", 0, 0, 0, 0, "failed to dismiss about"},
      {TUI_STEP_EXPECT, "HELP/ABOUT", 0, 0, 0, PTY_TIMEOUT_MS,
       "Help/About did not reappear after about"},
      {TUI_STEP_SEND, "q", 0, 0, 0, 0, "failed to leave Help/About"},
      {TUI_STEP_EXPECT, "OPENQUICK", 0, 0, 0, PTY_TIMEOUT_MS,
       "menu did not reappear after Help/About"},

      {TUI_STEP_RESIZE, NULL, 0, 100, 28, 0,
       "failed to resize PTY/Ghostty terminal"},
      {TUI_STEP_EXPECT, "OPENQUICK", 0, 0, 0, PTY_TIMEOUT_MS,
       "OPENQUICK disappeared after resize"},
      {TUI_STEP_SEND, "q", 0, 0, 0, 0, "failed to send q"},
      {TUI_STEP_EXPECT, "Return to the shell?", 0, 0, 0, PTY_TIMEOUT_MS,
       "exit confirmation did not appear"},
      {TUI_STEP_SEND, "n", 0, 0, 0, 0, "failed to cancel exit confirmation"},
      {TUI_STEP_EXPECT, "OPENQUICK", 0, 0, 0, PTY_TIMEOUT_MS,
       "menu did not reappear after exit cancel"},
      {TUI_STEP_SEND, "x", 0, 0, 0, 0, "failed to select Exit"},
      {TUI_STEP_EXPECT, "Return to the shell?", 0, 0, 0, PTY_TIMEOUT_MS,
       "exit menu confirmation did not appear"},
      {TUI_STEP_SEND, "y", 0, 0, 0, 0, "failed to confirm exit"},
      {TUI_STEP_WAIT_EXIT, NULL, 0, 0, 0, PTY_TIMEOUT_MS, NULL},
  };

  char *snapshot = NULL;
  int failed = 0;
  for (size_t i = 0; !failed && i < sizeof(steps) / sizeof(steps[0]); i++) {
    failed = run_tui_step(stats, name, &session, &steps[i], &snapshot);
  }

  if (!failed) {
    test_pass(stats, name);
  }
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_bare_invocation(test_stats_t *stats, const char *binary,
                            bool tui_enabled) {
  const char *name = "bare TTY invocation launches the TUI menu";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }

  vt_session_t session;
  if (!vt_session_start(&session, binary, NULL, 0, 80, 24)) {
    return test_fail(stats, name, "failed to start PTY session");
  }

  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                      &snapshot)) {
    failed = test_fail(stats, name, "bare invocation did not render the menu");
  }
  if (!failed && (!vt_send(&session, "q") ||
                  !vt_expect_text(&session, "Return to the shell?",
                                  PTY_TIMEOUT_MS, &snapshot) ||
                  !vt_send(&session, "y"))) {
    failed = test_fail(stats, name, "failed to drive clean TUI exit");
  }
  if (!failed) {
    const int exit_code = vt_wait_for_exit(&session, PTY_TIMEOUT_MS);
    if (exit_code != 0) {
      failed = test_fail(stats, name, "expected exit 0, got %d", exit_code);
    }
  }

  if (!failed) {
    test_pass(stats, name);
  }
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_bare_invocation_json(test_stats_t *stats, const char *binary,
                                 bool tui_enabled) {
  const char *name =
      "bare TTY invocation honors json_output instead of launching the TUI";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }

  // Force JSON output through a config file. A bare invocation on a TTY must
  // then reject the contradictory request (mirroring `openquick menu` rejecting
  // --json) with clear guidance rather than launching the TUI or blocking on
  // interactive stdin.
  const char *tmpdir = getenv("TMPDIR");
  if (!tmpdir || !*tmpdir) {
    tmpdir = "/tmp";
  }
  char config_path[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%s/vt-json-config-XXXXXX",
           tmpdir);
  const int fd = mkstemp(config_path);
  if (fd < 0) {
    return test_fail(stats, name, "failed to create temp config: %s",
                     strerror(errno));
  }
  static const char config_json[] = "{\"json_output\": true}\n";
  const ssize_t want = (ssize_t)(sizeof(config_json) - 1);
  const bool wrote = write(fd, config_json, (size_t)want) == want;
  close(fd);
  if (!wrote) {
    unlink(config_path);
    return test_fail(stats, name, "failed to write temp config");
  }
  setenv("QUICK_CONFIG_PATH", config_path, 1);

  // A wide row keeps the single-line JSON response from wrapping, so the
  // expected substring stays contiguous in the rendered snapshot.
  vt_session_t session;
  const bool started = vt_session_start(&session, binary, NULL, 0, 200, 24);

  char *snapshot = NULL;
  int failed = 0;
  if (!started) {
    failed = test_fail(stats, name, "failed to start PTY session");
  }
  if (!failed &&
      !vt_expect_text(&session,
                      "JSON output is incompatible with the interactive TUI",
                      PTY_TIMEOUT_MS, &snapshot)) {
    failed = test_fail(
        stats, name,
        "json_output conflict message did not appear (TUI may have launched)");
  }
  if (!failed && contains_text(snapshot, "OPENQUICK")) {
    failed = test_fail(stats, name, "TUI launched despite json_output=true");
  }
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) == 0) {
    failed = test_fail(stats, name, "expected a non-zero exit");
  }
  if (!failed) {
    test_pass(stats, name);
  }

  free(snapshot);
  if (started) {
    vt_session_close(&session);
  }
  unsetenv("QUICK_CONFIG_PATH");
  unlink(config_path);
  return failed;
}

int run_tui_stress_smoke(test_stats_t *stats, const char *binary,
                         bool tui_enabled) {
  const char *name = "tui deterministic input and resize smoke";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }

  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 80, 24)) {
    return test_fail(stats, name, "failed to start PTY session");
  }

  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                      &snapshot)) {
    failed = test_fail(stats, name, "initial menu did not render");
  }

  /* With TERM=xterm-256color and keypad() enabled, ncurses asks the terminal
   * for application cursor mode; Ghostty-compatible VT input should use the
   * matching terminfo kcuu1/kcud1 sequences.
   */
  const char *safe_inputs[] = {
      "\x1bOB", "\x1bOA", "\t", "\x1bOB", "\x1bOA", "\t",
  };
  const uint16_t sizes[][2] = {{72, 20}, {100, 28}, {80, 24}};
  for (size_t i = 0;
       !failed && i < sizeof(safe_inputs) / sizeof(safe_inputs[0]); i++) {
    if (!vt_send(&session, safe_inputs[i])) {
      failed = test_fail(stats, name, "failed to send generated input %zu", i);
      break;
    }
    if (i < sizeof(sizes) / sizeof(sizes[0])) {
      if (!vt_resize(&session, sizes[i][0], sizes[i][1])) {
        failed = test_fail(stats, name, "failed to apply resize %zu", i);
        break;
      }
    }
    if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                        &snapshot)) {
      print_tail(stderr, "screen:\n", snapshot ? snapshot : "",
                 snapshot ? strlen(snapshot) : 0, 4000);
      print_tail(stderr, "transcript:\n", buffer_cstr(&session.transcript),
                 session.transcript.len, 4000);
      failed = test_fail(stats, name,
                         "menu invariant failed after generated action %zu", i);
      break;
    }
  }

  if (!failed && (!vt_send(&session, "q") ||
                  !vt_expect_text(&session, "Return to the shell?",
                                  PTY_TIMEOUT_MS, &snapshot) ||
                  !vt_send(&session, "y"))) {
    failed = test_fail(stats, name, "failed to drive clean exit");
  }
  if (!failed) {
    const int exit_code = vt_wait_for_exit(&session, PTY_TIMEOUT_MS);
    if (exit_code != 0) {
      failed = test_fail(stats, name, "expected exit 0, got %d", exit_code);
    }
  }

  if (!failed) {
    test_pass(stats, name);
  }
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_menu_separator(test_stats_t *stats, const char *binary,
                           bool tui_enabled) {
  const char *name = "tui menu navigation skips separator";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 80, 24)) {
    return test_fail(stats, name, "failed to start PTY session");
  }
  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_expect_text(&session, "Help/About", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  /* Selection starts on Sites. Six j presses should advance past the separator
   * to Help/About. */
  if (!failed && !vt_send(&session, "jjjjjj"))
    failed = test_fail(stats, name, "failed to send navigation");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to confirm");
  if (!failed && !vt_expect_text(&session, "HELP/ABOUT", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "expected to land on Help/About");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to return to main menu");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_menu_sigint(test_stats_t *stats, const char *binary,
                        bool tui_enabled) {
  const char *name = "tui menu SIGINT cleanly exits";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 80, 24)) {
    return test_fail(stats, name, "failed to start PTY session");
  }
  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  /* Send Ctrl-C through the PTY. The shell/terminal converts \x03 to SIGINT. */
  if (!failed && !vt_send(&session, "\x03"))
    failed = test_fail(stats, name, "failed to send Ctrl-C");
  if (!failed) {
    /* Ctrl-C is a user cancellation, so the process must exit with the
     * conventional interrupt status 130 (the shell's 128 + SIGINT): not 0,
     * which would let `app && next` proceed, and not a generic failure code.
     * vt_wait_for_exit reports 130 whether the TUI handler returns
     * APP_ERROR_INTERRUPTED or the process is killed by the signal. */
    const int code = vt_wait_for_exit(&session, PTY_TIMEOUT_MS);
    if (code != 130)
      failed =
          test_fail(stats, name, "expected interrupt exit 130, got %d", code);
  }
  if (!failed && contains_text(buffer_cstr(&session.transcript),
                               "TUI failed: Signal handling error")) {
    failed = test_fail(stats, name,
                       "SIGINT leaked a misleading TUI failure diagnostic");
  }
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_menu_sigterm(test_stats_t *stats, const char *binary,
                         bool tui_enabled) {
  const char *name = "tui menu SIGTERM cleanly exits";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 80, 24)) {
    return test_fail(stats, name, "failed to start PTY session");
  }

  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && kill(session.pid, SIGTERM) != 0)
    failed =
        test_fail(stats, name, "failed to send SIGTERM: %s", strerror(errno));
  if (!failed) {
    const int code = vt_wait_for_exit(&session, PTY_TIMEOUT_MS);
    if (code != 143)
      failed =
          test_fail(stats, name, "expected terminate exit 143, got %d", code);
  }
  if (!failed && contains_text(buffer_cstr(&session.transcript),
                               "TUI failed: Signal handling error")) {
    failed = test_fail(stats, name,
                       "SIGTERM leaked a misleading TUI failure diagnostic");
  }
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_menu_resize(test_stats_t *stats, const char *binary,
                        bool tui_enabled) {
  const char *name = "tui menu survives shrink-then-grow resize";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 100, 30)) {
    return test_fail(stats, name, "failed to start PTY session");
  }
  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  /* Shrink: above minimum but smaller than initial frame_width=72. */
  if (!failed && !vt_resize(&session, 60, 16))
    failed = test_fail(stats, name, "failed to shrink");
  if (!failed &&
      !vt_expect_menu_frame(&session, 0, 60, PTY_TIMEOUT_MS, &snapshot)) {
    print_tail(stderr, "screen:\n", snapshot ? snapshot : "",
               snapshot ? strlen(snapshot) : 0, 4000);
    failed = test_fail(stats, name,
                       "menu frame was not left-aligned at width 60 after "
                       "shrink");
  }
  /* Grow back. */
  if (!failed && !vt_resize(&session, 100, 30))
    failed = test_fail(stats, name, "failed to grow");
  if (!failed &&
      !vt_expect_menu_frame(&session, 14, 72, PTY_TIMEOUT_MS, &snapshot)) {
    print_tail(stderr, "screen:\n", snapshot ? snapshot : "",
               snapshot ? strlen(snapshot) : 0, 4000);
    failed = test_fail(stats, name,
                       "menu frame was not centered at width 72 after grow");
  }
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_menu_handler_resize(test_stats_t *stats, const char *binary,
                                bool tui_enabled) {
  const char *name = "tui menu restores frame after handler resize";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 100, 30)) {
    return test_fail(stats, name, "failed to start PTY session");
  }
  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_menu_frame(&session, 14, 72, PTY_TIMEOUT_MS, &snapshot)) {
    print_tail(stderr, "screen:\n", snapshot ? snapshot : "",
               snapshot ? strlen(snapshot) : 0, 4000);
    failed = test_fail(stats, name, "initial menu frame was not 72 wide");
  }
  if (!failed && !vt_resize(&session, 60, 16))
    failed = test_fail(stats, name, "failed to shrink before handler");
  if (!failed &&
      !vt_expect_menu_frame(&session, 0, 60, PTY_TIMEOUT_MS, &snapshot)) {
    print_tail(stderr, "screen:\n", snapshot ? snapshot : "",
               snapshot ? strlen(snapshot) : 0, 4000);
    failed = test_fail(stats, name, "menu frame did not shrink to terminal");
  }
  if (!failed && !vt_expect_text(&session, "Sites", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "menu items did not render after shrink");
  if (!failed && !vt_send(&session, "h"))
    failed = test_fail(stats, name, "failed to open Help/About handler");
  if (!failed && !vt_expect_text(&session, "HELP/ABOUT", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "Help/About handler did not open");
  if (!failed && !vt_resize(&session, 100, 30))
    failed = test_fail(stats, name, "failed to grow during handler");
  if (!failed && !vt_expect_text(&session, "HELP/ABOUT", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "Help/About disappeared after grow");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to dismiss Help/About");
  if (!failed &&
      !vt_expect_menu_frame(&session, 14, 72, PTY_TIMEOUT_MS, &snapshot)) {
    print_tail(stderr, "screen:\n", snapshot ? snapshot : "",
               snapshot ? strlen(snapshot) : 0, 4000);
    failed = test_fail(stats, name,
                       "menu frame did not restore to 72 columns after "
                       "handler resize");
  }
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_menu_mnemonic(test_stats_t *stats, const char *binary,
                          bool tui_enabled) {
  const char *name = "tui menu mnemonic auto-confirms";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 80, 24)) {
    return test_fail(stats, name, "failed to start PTY session");
  }
  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_expect_text(&session, "Help/About", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  if (!failed && !vt_send(&session, "h"))
    failed = test_fail(stats, name, "failed to send 'h'");
  if (!failed && !vt_expect_text(&session, "HELP/ABOUT", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "Help/About menu did not appear");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to dismiss Help/About");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_menu_search(test_stats_t *stats, const char *binary,
                        bool tui_enabled) {
  const char *name = "tui menu search filter narrows and confirms";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  const char *args[] = {"menu"};
  vt_session_t session;
  if (!vt_session_start(&session, binary, args, 1, 80, 24)) {
    return test_fail(stats, name, "failed to start PTY session");
  }
  char *snapshot = NULL;
  int failed = 0;
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_expect_text(&session, "Serve", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  if (!failed && !vt_send(&session, "/"))
    failed = test_fail(stats, name, "failed to enter search mode");
  if (!failed && !vt_expect_text(&session, "find:", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "search prompt did not appear");
  if (!failed && !vt_send(&session, "serv"))
    failed = test_fail(stats, name, "failed to type 'serv'");
  if (!failed && !vt_expect_text(&session, "Serve", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "Serve not filtered in");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to confirm");
  if (!failed && !vt_expect_text(&session, "SERVE", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "Serve screen did not appear");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to dismiss serve screen");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_expect_text(&session, "Return to the shell?",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "exit confirm did not appear");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  vt_session_close(&session);
  return failed;
}

int run_tui_sites_empty_state(test_stats_t *stats, const char *binary,
                              bool tui_enabled) {
  const char *name = "sites screen renders empty state";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-sites-XXXXXX";
  if (!make_temp_dir_path(xdg)) {
    return test_fail(stats, name, "failed to create temp XDG dir: %s",
                     strerror(errno));
  }
  env_guard_t guards[5];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "QUICK_PROFILE", NULL);
  env_guard_set(&guards[2], "QUICK_SITE", NULL);
  env_guard_set(&guards[3], "QUICK_REMOTE", NULL);
  env_guard_set(&guards[4], "QUICK_BASE_DOMAIN", NULL);

  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 80, 24);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) {
    failed = test_fail(stats, name, "failed to start PTY session");
  }
  if (!failed && !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "s"))
    failed = test_fail(stats, name, "failed to open Sites");
  if (!failed && !vt_expect_text(&session, "No deployed sites are known yet",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "sites empty state did not render");
  if (!failed && !vt_send(&session, "x"))
    failed = test_fail(stats, name, "failed to dismiss empty state");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started) vt_session_close(&session);
  restore_common_env(guards, sizeof(guards) / sizeof(guards[0]));
  rmdir(xdg);
  return failed;
}

int run_tui_sites_detail_actions(test_stats_t *stats, const char *binary,
                                 bool tui_enabled) {
  const char *name = "sites detail panel shows delete and public hints";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-site-actions-XXXXXX";
  char bin_dir[] = "/tmp/openquick-vt-bin-site-actions-XXXXXX";
  if (!make_temp_dir_path(xdg) || !make_temp_dir_path(bin_dir) ||
      !write_profile_config(xdg)) {
    return test_fail(stats, name, "failed to create temp inputs: %s",
                     strerror(errno));
  }
  char ssh_path[PATH_MAX];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  if (!write_text_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = list ]; then\n"
          "  printf '%s\\n' '{\"format_version\":\"1.0\",\"sites\":[{\"name\":\"demo\",\"subdomain\":\"demo\",\"url\":\"https://demo.quick.example.com\",\"release\":\"rel1\",\"updated_at\":\"2026-06-12T00:00:00Z\",\"deployer\":\"alice\",\"public\":false}]}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n") || chmod(ssh_path, 0755) != 0) {
    return test_fail(stats, name, "failed to write ssh stub");
  }

  char path_value[PATH_MAX];
  const char *old_path = getenv("PATH");
  snprintf(path_value, sizeof(path_value), "%s%s%s", bin_dir,
           old_path && old_path[0] ? ":" : "",
           old_path && old_path[0] ? old_path : "");
  env_guard_t guards[5];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "PATH", path_value);
  env_guard_set(&guards[2], "QUICK_PROFILE", NULL);
  env_guard_set(&guards[3], "QUICK_REMOTE", NULL);
  env_guard_set(&guards[4], "QUICK_BASE_DOMAIN", NULL);

  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 100, 30);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed && !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "s"))
    failed = test_fail(stats, name, "failed to open Sites");
  if (!failed && !vt_expect_text(&session, "demo - https://demo.quick.example.com",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "remote site row did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to open site detail");
  if (!failed && !vt_expect_text(&session, "x:delete", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "delete key hint missing");
  if (!failed && !vt_expect_text(&session, "p:public", 1000, &snapshot))
    failed = test_fail(stats, name, "public key hint missing");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to dismiss site detail");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to leave sites");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed) test_pass(stats, name);
  free(snapshot);
  if (started) vt_session_close(&session);
  restore_common_env(guards, sizeof(guards) / sizeof(guards[0]));
  char config_path[PATH_MAX], oq[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%s/openquick/config.json", xdg);
  snprintf(oq, sizeof(oq), "%s/openquick", xdg);
  unlink(config_path);
  rmdir(oq);
  unlink(ssh_path);
  rmdir(bin_dir);
  rmdir(xdg);
  return failed;
}

int run_tui_new_site_scaffold(test_stats_t *stats, const char *binary,
                              bool tui_enabled) {
  const char *name = "new-site flow scaffolds into a temp dir under PTY";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char site_dir[] = "/tmp/openquick-vt-site-XXXXXX";
  char xdg[] = "/tmp/openquick-vt-xdg-init-XXXXXX";
  if (!make_temp_dir_path(site_dir) || !make_temp_dir_path(xdg)) {
    return test_fail(stats, name, "failed to create temp dirs: %s",
                     strerror(errno));
  }
  env_guard_t guards[5];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "QUICK_PROFILE", NULL);
  env_guard_set(&guards[2], "QUICK_SITE", NULL);
  env_guard_set(&guards[3], "QUICK_REMOTE", NULL);
  env_guard_set(&guards[4], "QUICK_BASE_DOMAIN", NULL);

  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 100, 30);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) {
    failed = test_fail(stats, name, "failed to start PTY session");
  }
  if (!failed && !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "n"))
    failed = test_fail(stats, name, "failed to open New site");
  if (!failed && !vt_expect_text(&session, "Directory to create",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "directory prompt did not render");
  char send_buf[PATH_MAX + 4];
  snprintf(send_buf, sizeof(send_buf), "%s\r", site_dir);
  if (!failed && !vt_send(&session, send_buf))
    failed = test_fail(stats, name, "failed to submit directory");
  if (!failed && !vt_expect_text(&session, "Site name:", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "site name prompt did not render");
  if (!failed && !vt_send(&session, "pty-site\r"))
    failed = test_fail(stats, name, "failed to submit site name");
  if (!failed && !vt_expect_text(&session, "NEW SITE TEMPLATE",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "template menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to select blank template");
  if (!failed && !vt_expect_text(&session, "SITE CREATED", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "success panel did not render");
  if (!failed && !vt_expect_text(&session, "quick.json", 1000, &snapshot))
    failed = test_fail(stats, name, "created files list did not render");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to dismiss success panel");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");

  char index_path[PATH_MAX];
  char quick_path[PATH_MAX];
  snprintf(index_path, sizeof(index_path), "%s/index.html", site_dir);
  snprintf(quick_path, sizeof(quick_path), "%s/quick.json", site_dir);
  if (!failed && (access(index_path, F_OK) != 0 ||
                  access(quick_path, F_OK) != 0)) {
    failed = test_fail(stats, name, "expected scaffold files on disk");
  }
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started) vt_session_close(&session);
  restore_common_env(guards, sizeof(guards) / sizeof(guards[0]));
  char agents[PATH_MAX], ignore[PATH_MAX], docs[PATH_MAX], api[PATH_MAX];
  snprintf(agents, sizeof(agents), "%s/AGENTS.md", site_dir);
  snprintf(ignore, sizeof(ignore), "%s/.quickignore", site_dir);
  snprintf(docs, sizeof(docs), "%s/docs", site_dir);
  snprintf(api, sizeof(api), "%s/docs/openquick-api.md", site_dir);
  unlink(index_path);
  unlink(quick_path);
  unlink(agents);
  unlink(ignore);
  unlink(api);
  rmdir(docs);
  rmdir(site_dir);
  rmdir(xdg);
  return failed;
}

int run_tui_doctor_local_results(test_stats_t *stats, const char *binary,
                                 bool tui_enabled) {
  const char *name = "doctor local run renders grouped results";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-doctor-XXXXXX";
  if (!make_temp_dir_path(xdg)) {
    return test_fail(stats, name, "failed to create temp XDG dir: %s",
                     strerror(errno));
  }
  env_guard_t guard;
  env_guard_set(&guard, "XDG_CONFIG_HOME", xdg);
  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 100, 30);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed && !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "c"))
    failed = test_fail(stats, name, "failed to open Doctor");
  if (!failed && !vt_expect_text(&session, "DOCTOR", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "doctor scope menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to select local scope");
  if (!failed && !vt_expect_text(&session, "DOCTOR RESULTS", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "doctor results did not render");
  if (!failed && !vt_expect_text(&session, "local/quick_version", 1000,
                                 &snapshot))
    failed = test_fail(stats, name, "local quick_version row missing");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to return from doctor");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed) test_pass(stats, name);
  free(snapshot);
  if (started) vt_session_close(&session);
  env_guard_restore(&guard);
  rmdir(xdg);
  return failed;
}

int run_tui_settings_profiles_from_xdg(test_stats_t *stats, const char *binary,
                                       bool tui_enabled) {
  const char *name = "settings shows profiles loaded from temp XDG config";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-settings-XXXXXX";
  if (!make_temp_dir_path(xdg) || !write_profile_config(xdg)) {
    return test_fail(stats, name, "failed to create profile config: %s",
                     strerror(errno));
  }
  env_guard_t guard;
  env_guard_set(&guard, "XDG_CONFIG_HOME", xdg);
  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 100, 30);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed && !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "i"))
    failed = test_fail(stats, name, "failed to open Settings");
  if (!failed && !vt_expect_text(&session, "SETTINGS", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "settings menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to open Profiles");
  if (!failed && !vt_expect_text(&session, "PROFILES", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "profiles menu did not render");
  if (!failed && !vt_expect_text(&session, "lab (default)", 1000,
                                 &snapshot))
    failed = test_fail(stats, name, "lab profile was not loaded");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to leave profiles");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to leave settings");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed) test_pass(stats, name);
  free(snapshot);
  if (started) vt_session_close(&session);
  env_guard_restore(&guard);
  char config_path[PATH_MAX], oq[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%s/openquick/config.json", xdg);
  snprintf(oq, sizeof(oq), "%s/openquick", xdg);
  unlink(config_path);
  rmdir(oq);
  rmdir(xdg);
  return failed;
}

int run_tui_deploy_plan_panel(test_stats_t *stats, const char *binary,
                              bool tui_enabled) {
  const char *name = "deploy plan panel renders for temp site and fake profile";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char site_dir[] = "/tmp/openquick-vt-deploy-XXXXXX";
  char xdg[] = "/tmp/openquick-vt-xdg-deploy-XXXXXX";
  if (!make_temp_dir_path(site_dir) || !make_temp_dir_path(xdg) ||
      !write_profile_config(xdg)) {
    return test_fail(stats, name, "failed to create temp inputs: %s",
                     strerror(errno));
  }
  char qpath[PATH_MAX];
  snprintf(qpath, sizeof(qpath), "%s/quick.json", site_dir);
  if (!write_text_file(qpath,
                       "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\",\"profile\":\"lab\",\"subdomain\":\"demo\"}\n")) {
    return test_fail(stats, name, "failed to write quick.json");
  }
  env_guard_t guard;
  env_guard_set(&guard, "XDG_CONFIG_HOME", xdg);
  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 100, 30);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed && !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "d"))
    failed = test_fail(stats, name, "failed to open Deploy");
  if (!failed && !vt_expect_text(&session, "Path to site directory",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "path prompt did not render");
  char send_path[PATH_MAX + 4];
  snprintf(send_path, sizeof(send_path), "%s\r", site_dir);
  if (!failed && !vt_send(&session, send_path))
    failed = test_fail(stats, name, "failed to submit path");
  if (!failed && !vt_expect_text(&session, "PROFILE", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "profile menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to select profile");
  if (!failed && !vt_expect_text(&session, "Site name", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "site prompt did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to accept site");
  if (!failed && !vt_expect_text(&session, "Subdomain", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "subdomain prompt did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to accept subdomain");
  if (!failed && !vt_expect_text(&session, "DEPLOY PLAN", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "deploy plan panel did not render");
  if (!failed && !vt_expect_text(&session, "https://demo.quick.example.com",
                                 1000, &snapshot))
    failed = test_fail(stats, name, "plan URL missing");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to cancel plan");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed) test_pass(stats, name);
  free(snapshot);
  if (started) vt_session_close(&session);
  env_guard_restore(&guard);
  unlink(qpath);
  rmdir(site_dir);
  char config_path[PATH_MAX], oq[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%s/openquick/config.json", xdg);
  snprintf(oq, sizeof(oq), "%s/openquick", xdg);
  unlink(config_path);
  rmdir(oq);
  rmdir(xdg);
  return failed;
}

int run_tui_serve_install_guide(test_stats_t *stats, const char *binary,
                                bool tui_enabled) {
  const char *name = "serve install guide renders steps";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-serve-XXXXXX";
  if (!make_temp_dir_path(xdg) || !write_profile_config(xdg)) {
    return test_fail(stats, name, "failed to create profile config: %s",
                     strerror(errno));
  }
  env_guard_t guard;
  env_guard_set(&guard, "XDG_CONFIG_HOME", xdg);
  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 110, 32);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed && !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "v"))
    failed = test_fail(stats, name, "failed to open Serve");
  if (!failed && !vt_expect_text(&session, "SERVE", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "serve menu did not render");
  if (!failed && !vt_send(&session, "h"))
    failed = test_fail(stats, name, "failed to open host guide");
  const char *prompts[] = {"Profile", "SSH host", "Remote root",
                           "Base domain", "IAP type"};
  for (size_t i = 0; !failed && i < sizeof(prompts) / sizeof(prompts[0]); i++) {
    if (!vt_expect_text(&session, prompts[i], PTY_TIMEOUT_MS, &snapshot)) {
      failed = test_fail(stats, name, "prompt did not render: %s", prompts[i]);
      break;
    }
    if (!vt_send(&session, "\r")) {
      failed = test_fail(stats, name, "failed to accept prompt: %s", prompts[i]);
      break;
    }
  }
  if (!failed && !vt_expect_text(&session, "HOST INSTALL GUIDE",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "install guide panel did not render");
  if (!failed && !vt_expect_text(&session, "quick serve install --profile lab",
                                 1000, &snapshot))
    failed = test_fail(stats, name, "install command missing");
  if (!failed && !vt_expect_text(&session, "create quick user", 1000,
                                 &snapshot))
    failed = test_fail(stats, name, "install steps missing");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to dismiss guide");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to leave serve");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed) test_pass(stats, name);
  free(snapshot);
  if (started) vt_session_close(&session);
  env_guard_restore(&guard);
  char config_path[PATH_MAX], oq[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%s/openquick/config.json", xdg);
  snprintf(oq, sizeof(oq), "%s/openquick", xdg);
  unlink(config_path);
  rmdir(oq);
  rmdir(xdg);
  return failed;
}
