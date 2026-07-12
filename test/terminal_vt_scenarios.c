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
      {TUI_STEP_EXPECT, "CLI: quick list --remote", 0, 0, 0, 1000,
       "CLI fallback hint did not appear on main menu"},

      {TUI_STEP_SEND, "h", 0, 0, 0, 0, "failed to select Help/About"},
      {TUI_STEP_EXPECT, "HELP/ABOUT", 0, 0, 0, PTY_TIMEOUT_MS,
       "Help/About menu did not appear"},
      {TUI_STEP_SEND, "k", 0, 0, 0, 0, "failed to select Key bindings"},
      {TUI_STEP_EXPECT, "KEY BINDINGS", 0, 0, 0, PTY_TIMEOUT_MS,
       "Key Bindings did not appear"},
      {TUI_STEP_EXPECT, "Up / Down", 0, 0, 0, 1000,
       "key binding body did not appear"},
      {TUI_STEP_EXPECT, "CLI equivalents", 0, 0, 0, 1000,
       "CLI equivalents did not appear in help"},
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
  const char *name =
      "returning user with a profile launches straight to the dashboard";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }

  char xdg[] = "/tmp/openquick-vt-xdg-bare-XXXXXX";
  if (!make_temp_dir_path(xdg)) {
    return test_fail(stats, name, "failed to create temp XDG dir");
  }
  if (!write_profile_config(xdg)) {
    char cfg[PATH_MAX];
    snprintf(cfg, sizeof(cfg), "%s/openquick/config.json", xdg);
    unlink(cfg);
    char od[PATH_MAX];
    snprintf(od, sizeof(od), "%s/openquick", xdg);
    rmdir(od);
    rmdir(xdg);
    return test_fail(stats, name, "failed to write profile config");
  }

  env_guard_t guard;
  env_guard_set(&guard, "XDG_CONFIG_HOME", xdg);
  vt_session_t session;
  bool started = vt_session_start(&session, binary, NULL, 0, 80, 24);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) {
    failed = test_fail(stats, name, "failed to start PTY session");
  }
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot)) {
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
  if (started) {
    vt_session_close(&session);
  }
  env_guard_restore(&guard);
  char cfg[PATH_MAX];
  snprintf(cfg, sizeof(cfg), "%s/openquick/config.json", xdg);
  unlink(cfg);
  char od[PATH_MAX];
  snprintf(od, sizeof(od), "%s/openquick", xdg);
  rmdir(od);
  rmdir(xdg);
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
  if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot)) {
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
    if (!vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot)) {
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
  if (!failed &&
      !vt_expect_text(&session, "Help/About", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  /* Selection starts on Sites. Eight j presses should advance past the separator
   * to Help/About. */
  if (!failed && !vt_send(&session, "jjjjjjjj"))
    failed = test_fail(stats, name, "failed to send navigation");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to confirm");
  if (!failed &&
      !vt_expect_text(&session, "HELP/ABOUT", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed && !vt_expect_text(&session, "Sites", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "menu items did not render after shrink");
  if (!failed && !vt_send(&session, "h"))
    failed = test_fail(stats, name, "failed to open Help/About handler");
  if (!failed &&
      !vt_expect_text(&session, "HELP/ABOUT", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "Help/About handler did not open");
  if (!failed && !vt_resize(&session, 100, 30))
    failed = test_fail(stats, name, "failed to grow during handler");
  if (!failed &&
      !vt_expect_text(&session, "HELP/ABOUT", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed &&
      !vt_expect_text(&session, "Help/About", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  if (!failed && !vt_send(&session, "h"))
    failed = test_fail(stats, name, "failed to send 'h'");
  if (!failed &&
      !vt_expect_text(&session, "HELP/ABOUT", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed && !vt_expect_text(&session, "Serve", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  if (!failed && !vt_send(&session, "/"))
    failed = test_fail(stats, name, "failed to enter search mode");
  if (!failed && !vt_expect_text(&session, "find:", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "search prompt did not appear");
  if (!failed && !vt_send(&session, "serv"))
    failed = test_fail(stats, name, "failed to type 'serv'");
  if (!failed && !vt_expect_text(&session, "Serve", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "Serve not filtered in");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to confirm");
  if (!failed && !vt_expect_text(&session, "SERVE", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
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
  if (started)
    vt_session_close(&session);
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
          "  printf '%s\\n' "
          "'{\"format_version\":\"1.0\",\"sites\":[{\"name\":\"demo\","
          "\"subdomain\":\"demo\",\"url\":\"https://"
          "demo.quick.example.com\",\"release\":\"rel1\",\"updated_at\":\"2026-"
          "06-12T00:00:00Z\",\"deployer\":\"alice\",\"public\":false}]}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n") ||
      chmod(ssh_path, 0755) != 0) {
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
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "s"))
    failed = test_fail(stats, name, "failed to open Sites");
  if (!failed &&
      !vt_expect_text(&session, "demo - https://demo.quick.example.com",
                      PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "remote site row did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to open site detail");
  if (!failed &&
      !vt_expect_text(&session, "x:delete", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
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
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed &&
      !vt_expect_text(&session, "Site name:", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "site name prompt did not render");
  if (!failed && !vt_send(&session, "pty-site\r"))
    failed = test_fail(stats, name, "failed to submit site name");
  if (!failed &&
      !vt_expect_text(&session, "NEW SITE TEMPLATE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "template menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to select blank template");
  if (!failed &&
      !vt_expect_text(&session, "SITE CREATED", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed &&
      (access(index_path, F_OK) != 0 || access(quick_path, F_OK) != 0)) {
    failed = test_fail(stats, name, "expected scaffold files on disk");
  }
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
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
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "c"))
    failed = test_fail(stats, name, "failed to open Doctor");
  if (!failed && !vt_expect_text(&session, "DOCTOR", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "doctor scope menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to select local scope");
  if (!failed &&
      !vt_expect_text(&session, "DOCTOR RESULTS", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "doctor results did not render");
  if (!failed &&
      !vt_expect_text(&session, "local/quick_version", 1000, &snapshot))
    failed = test_fail(stats, name, "local quick_version row missing");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to return from doctor");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
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
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "i"))
    failed = test_fail(stats, name, "failed to open Settings");
  if (!failed &&
      !vt_expect_text(&session, "SETTINGS", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "settings menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to open Profiles");
  if (!failed &&
      !vt_expect_text(&session, "PROFILES", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "profiles menu did not render");
  if (!failed && !vt_expect_text(&session, "lab (default)", 1000, &snapshot))
    failed = test_fail(stats, name, "lab profile was not loaded");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to open profile");
  if (!failed &&
      !vt_expect_text(&session, "PROFILE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "profile panel did not render");
  if (!failed && !vt_send(&session, "e"))
    failed = test_fail(stats, name, "failed to start profile edit");
  if (!failed && !vt_expect_text(&session, "EDIT PROFILE FIELD", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "profile field menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to choose ssh field");
  if (!failed &&
      !vt_expect_text(&session, "EDIT PROFILE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "profile edit dialog did not render");
  if (!failed && !vt_send(&session, "quick@changed\r"))
    failed = test_fail(stats, name, "failed to enter changed ssh");
  if (!failed &&
      !vt_expect_text(&session, "quick@changed", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "edited profile value did not render");
  if (!failed && !vt_expect_text(&session, "Unsaved changes", 1000, &snapshot))
    failed = test_fail(stats, name, "unsaved marker did not render");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to leave dirty profile");
  if (!failed && !vt_expect_text(&session, "UNSAVED PROFILE CHANGES",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "unsaved changes prompt did not render");
  if (!failed && !vt_send(&session, "c"))
    failed = test_fail(stats, name, "failed to cancel unsaved prompt");
  if (!failed &&
      !vt_expect_text(&session, "quick@changed", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "cancel did not return to edited profile");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to leave dirty profile again");
  if (!failed && !vt_expect_text(&session, "UNSAVED PROFILE CHANGES",
                                 PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "unsaved changes prompt did not reappear");
  if (!failed && !vt_send(&session, "d"))
    failed = test_fail(stats, name, "failed to discard unsaved changes");
  if (!failed &&
      !vt_expect_text(&session, "PROFILES", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "discard did not return to profiles");
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
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
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
                       "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\","
                       "\"profile\":\"lab\",\"subdomain\":\"demo\"}\n")) {
    return test_fail(stats, name, "failed to write quick.json");
  }
  env_guard_t guard;
  env_guard_set(&guard, "XDG_CONFIG_HOME", xdg);
  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 100, 30);
  char *snapshot = NULL;
  int failed = 0;
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed &&
      !vt_expect_text(&session, "PROFILE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "profile menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to select profile");
  if (!failed &&
      !vt_expect_text(&session, "Site name", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "site prompt did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to accept site");
  if (!failed &&
      !vt_expect_text(&session, "Subdomain", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "subdomain prompt did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to accept subdomain");
  if (!failed &&
      !vt_expect_text(&session, "DEPLOY PLAN", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
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

int run_tui_deploy_cancel_cleanup_status(test_stats_t *stats,
                                         const char *binary, bool tui_enabled) {
  const char *name = "deploy cancel reports cleanup and retry succeeds";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char site_dir[] = "/tmp/openquick-vt-deploy-cancel-XXXXXX";
  char xdg[] = "/tmp/openquick-vt-xdg-deploy-cancel-XXXXXX";
  char bin_dir[] = "/tmp/openquick-vt-bin-deploy-cancel-XXXXXX";
  char state_dir[] = "/tmp/openquick-vt-state-deploy-cancel-XXXXXX";
  if (!make_temp_dir_path(site_dir) || !make_temp_dir_path(xdg) ||
      !make_temp_dir_path(bin_dir) || !make_temp_dir_path(state_dir) ||
      !write_profile_config(xdg)) {
    return test_fail(stats, name, "failed to create temp inputs: %s",
                     strerror(errno));
  }
  char qpath[PATH_MAX];
  char index_path[PATH_MAX];
  snprintf(qpath, sizeof(qpath), "%s/quick.json", site_dir);
  snprintf(index_path, sizeof(index_path), "%s/index.html", site_dir);
  if (!write_text_file(qpath,
                       "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\","
                       "\"profile\":\"lab\",\"subdomain\":\"demo\"}\n") ||
      !write_text_file(index_path, "<!doctype html><title>demo</title>\n")) {
    return test_fail(stats, name, "failed to write site files");
  }
  char ssh_path[PATH_MAX];
  char rsync_path[PATH_MAX];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(rsync_path, sizeof(rsync_path), "%s/rsync", bin_dir);
  if (!write_text_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = doctor ]; then\n"
          "  printf '%s\\n' '{\"checks\":[{\"status\":\"ok\"}]}'\n"
          "  exit 0\n"
          "fi\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = prepare "
          "]; then\n"
          "  printf '%s\\n' "
          "'{\"deploy_id\":\"20260623T000000Z-deadbe\",\"staging_path\":\"/srv/"
          "quick/.incoming/20260623T000000Z-deadbe/"
          "files\",\"link_dest\":\"\"}'\n"
          "  exit 0\n"
          "fi\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = cleanup "
          "]; then\n"
          "  echo cleanup >> \"$OPENQUICK_TUI_FAKE_STATE/ssh.log\"\n"
          "  printf '%s\\n' '{\"cleaned\":true}'\n"
          "  exit 0\n"
          "fi\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = deploy ] && [ \"$4\" = "
          "activate ]; then\n"
          "  printf '%s\\n' "
          "'{\"release\":\"rel-ok\",\"url\":\"https://"
          "demo.quick.example.com\"}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n") ||
      chmod(ssh_path, 0755) != 0 ||
      !write_text_file(
          rsync_path,
          "#!/bin/sh\n"
          "if [ ! -f \"$OPENQUICK_TUI_FAKE_STATE/cancelled\" ]; then\n"
          "  : > \"$OPENQUICK_TUI_FAKE_STATE/cancelled\"\n"
          "  echo simulated transfer cancel >&2\n"
          "  exit 130\n"
          "fi\n"
          "printf '%s\\n' 'Number of regular files transferred: 1'\n"
          "printf '%s\\n' 'Number of regular files: 1'\n"
          "printf '%s\\n' 'Number of deleted files: 0'\n"
          "exit 0\n") ||
      chmod(rsync_path, 0755) != 0) {
    return test_fail(stats, name, "failed to write fake ssh/rsync");
  }

  char path_value[PATH_MAX * 2];
  const char *old_path = getenv("PATH");
  snprintf(path_value, sizeof(path_value), "%s%s%s", bin_dir,
           old_path && old_path[0] ? ":" : "",
           old_path && old_path[0] ? old_path : "");
  env_guard_t guards[6];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "PATH", path_value);
  env_guard_set(&guards[2], "OPENQUICK_TUI_FAKE_STATE", state_dir);
  env_guard_set(&guards[3], "QUICK_PROFILE", NULL);
  env_guard_set(&guards[4], "QUICK_REMOTE", NULL);
  env_guard_set(&guards[5], "QUICK_BASE_DOMAIN", NULL);

  const char *args[] = {"menu"};
  vt_session_t session;
  bool started = vt_session_start(&session, binary, args, 1, 100, 30);
  char *snapshot = NULL;
  int failed = 0;
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  for (int attempt = 0; !failed && attempt < 2; attempt++) {
    if (!vt_send(&session, "d")) {
      failed = test_fail(stats, name, "failed to open Deploy");
      break;
    }
    if (!vt_expect_text(&session, "Path to site directory", PTY_TIMEOUT_MS,
                        &snapshot)) {
      failed = test_fail(stats, name, "path prompt did not render");
      break;
    }
    char send_path[PATH_MAX + 4];
    snprintf(send_path, sizeof(send_path), "%s\r", site_dir);
    if (!vt_send(&session, send_path) ||
        !vt_expect_text(&session, "PROFILE", PTY_TIMEOUT_MS, &snapshot) ||
        !vt_send(&session, "\r") ||
        !vt_expect_text(&session, "Site name", PTY_TIMEOUT_MS, &snapshot) ||
        !vt_send(&session, "\r") ||
        !vt_expect_text(&session, "Subdomain", PTY_TIMEOUT_MS, &snapshot) ||
        !vt_send(&session, "\r") ||
        !vt_expect_text(&session, "DEPLOY PLAN", PTY_TIMEOUT_MS, &snapshot) ||
        !vt_send(&session, "\r")) {
      failed = test_fail(stats, name, "failed to complete deploy prompts");
      break;
    }
    if (attempt == 0) {
      if (!vt_expect_text(&session, "DEPLOY CANCELLED", PTY_TIMEOUT_MS,
                          &snapshot) ||
          !vt_expect_text(&session, "remote staging cleaned", PTY_TIMEOUT_MS,
                          &snapshot)) {
        failed = test_fail(stats, name, "cancel cleanup status did not render");
        break;
      }
      if (!vt_send(&session, "\r") ||
          !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot)) {
        failed =
            test_fail(stats, name, "failed to dismiss cancellation and return");
        break;
      }
    } else {
      if (!vt_expect_text(&session, "DEPLOY COMPLETE", PTY_TIMEOUT_MS,
                          &snapshot) ||
          !vt_expect_text(&session, "rel-ok", PTY_TIMEOUT_MS, &snapshot)) {
        failed = test_fail(stats, name, "follow-up deploy did not succeed");
        break;
      }
      if (!vt_send(&session, "\r") ||
          !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot)) {
        failed = test_fail(stats, name, "failed to dismiss success and return");
        break;
      }
    }
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
  if (started)
    vt_session_close(&session);
  restore_common_env(guards, sizeof(guards) / sizeof(guards[0]));
  unlink(qpath);
  unlink(index_path);
  rmdir(site_dir);
  unlink(ssh_path);
  unlink(rsync_path);
  rmdir(bin_dir);
  char cancelled_path[PATH_MAX], ssh_log_path[PATH_MAX];
  snprintf(cancelled_path, sizeof(cancelled_path), "%s/cancelled", state_dir);
  snprintf(ssh_log_path, sizeof(ssh_log_path), "%s/ssh.log", state_dir);
  unlink(cancelled_path);
  unlink(ssh_log_path);
  rmdir(state_dir);
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
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "OPENQUICK", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_send(&session, "v"))
    failed = test_fail(stats, name, "failed to open Serve");
  if (!failed && !vt_expect_text(&session, "SERVE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "serve menu did not render");
  if (!failed && !vt_send(&session, "h"))
    failed = test_fail(stats, name, "failed to open host guide");
  const char *prompts[] = {"Profile", "SSH host", "Remote root", "Base domain",
                           "IAP type"};
  for (size_t i = 0; !failed && i < sizeof(prompts) / sizeof(prompts[0]); i++) {
    if (!vt_expect_text(&session, prompts[i], PTY_TIMEOUT_MS, &snapshot)) {
      failed = test_fail(stats, name, "prompt did not render: %s", prompts[i]);
      break;
    }
    if (!vt_send(&session, "\r")) {
      failed =
          test_fail(stats, name, "failed to accept prompt: %s", prompts[i]);
      break;
    }
  }
  if (!failed && !vt_expect_text(&session, "HOST INSTALL GUIDE", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "install guide panel did not render");
  if (!failed && !vt_expect_text(&session, "quick serve install --profile lab",
                                 1000, &snapshot))
    failed = test_fail(stats, name, "install command missing");
  if (!failed &&
      !vt_expect_text(&session, "create quick user", 1000, &snapshot))
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
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
  env_guard_restore(&guard);
  char config_path[PATH_MAX], oq[PATH_MAX];
  snprintf(config_path, sizeof(config_path), "%s/openquick/config.json", xdg);
  snprintf(oq, sizeof(oq), "%s/openquick", xdg);
  unlink(config_path);
  rmdir(oq);
  rmdir(xdg);
  return failed;
}

int run_tui_onboarding_welcome_skip(test_stats_t *stats, const char *binary,
                                    bool tui_enabled) {
  const char *name =
      "fresh environment shows Get Started and can be skipped to the dashboard";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-welcome-XXXXXX";
  if (!make_temp_dir_path(xdg)) {
    return test_fail(stats, name, "failed to create temp XDG dir");
  }
  env_guard_t guards[2];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "QUICK_QUICKD", NULL);
  vt_session_t session;
  bool started = vt_session_start(&session, binary, NULL, 0, 80, 24);
  char *snapshot = NULL;
  int failed = 0;
  if (!started) {
    failed = test_fail(stats, name, "failed to start PTY session");
  }
  if (!failed &&
      !vt_expect_text(&session, "Try OpenQuick locally", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "welcome screen did not render");
  if (!failed &&
      !vt_expect_text(&session, "Connect a deployment host", 1500, &snapshot))
    failed = test_fail(stats, name, "welcome options did not render");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to dismiss welcome");
  if (!failed &&
      !vt_expect_text(&session, "Sites", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "dashboard did not render after skip");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed &&
      !vt_expect_text(&session, "Return to the shell?", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "exit confirm did not render");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
  env_guard_restore(&guards[0]);
  env_guard_restore(&guards[1]);
  rmdir(xdg);
  return failed;
}

static void ob_cleanup_project(const char *proj) {
  char p[PATH_MAX];
  const char *files[] = {"index.html", "quick.json", "AGENTS.md",
                         ".quickignore", "docs/openquick-api.md"};
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    snprintf(p, sizeof(p), "%s/%s", proj, files[i]);
    unlink(p);
  }
  snprintf(p, sizeof(p), "%s/docs", proj);
  rmdir(p);
  rmdir(proj);
}

int run_tui_onboarding_local_create(test_stats_t *stats, const char *binary,
                                    bool tui_enabled) {
  const char *name =
      "fresh local quickstart creates a site and previews it at the correct "
      "URL";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-lcreate-XXXXXX";
  char proj[] = "/tmp/openquick-vt-proj-XXXXXX";
  char bin_dir[] = "/tmp/openquick-vt-bin-XXXXXX";
  if (!make_temp_dir_path(xdg) || !make_temp_dir_path(proj) ||
      !make_temp_dir_path(bin_dir)) {
    return test_fail(stats, name, "failed to create temp dirs");
  }
  char quickd_path[PATH_MAX];
  snprintf(quickd_path, sizeof(quickd_path), "%s/quickd", bin_dir);
  if (!write_text_file(quickd_path, "#!/bin/sh\nexec sleep 120\n") ||
      chmod(quickd_path, 0755) != 0) {
    return test_fail(stats, name, "failed to write fake quickd");
  }
  env_guard_t guards[3];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "QUICK_QUICKD", quickd_path);
  env_guard_set(&guards[2], "QUICK_PROFILE", NULL);
  vt_session_t session;
  bool started = vt_session_start(&session, binary, NULL, 0, 90, 28);
  char *snapshot = NULL;
  int failed = 0;
  char send_dir[PATH_MAX + 4];
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "Try OpenQuick locally", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "welcome did not render");
  if (!failed && !vt_send(&session, "t"))
    failed = test_fail(stats, name, "failed to choose Try locally");
  if (!failed &&
      !vt_expect_text(&session, "Folder to create", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "folder prompt did not render");
  snprintf(send_dir, sizeof(send_dir), "%s\r", proj);
  if (!failed && !vt_send(&session, send_dir))
    failed = test_fail(stats, name, "failed to submit folder");
  if (!failed &&
      !vt_expect_text(&session, "Site name", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "name prompt did not render");
  if (!failed && !vt_send(&session, "pty-local\r"))
    failed = test_fail(stats, name, "failed to submit name");
  if (!failed &&
      !vt_expect_text(&session, "CHOOSE A STARTER", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "template menu did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to select template");
  if (!failed &&
      !vt_expect_text(&session, "Review: create this site", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "review panel did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to confirm review");
  if (!failed &&
      !vt_expect_text(&session, "Your site", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "preview panel did not render");
  if (!failed &&
      !vt_expect_text(&session, "http://localhost:9366/~/pty-local/", 3000,
                      &snapshot))
    failed = test_fail(stats, name, "local preview URL did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to close preview panel");
  if (!failed &&
      !vt_expect_text(&session, "Try OpenQuick locally", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "did not return to welcome");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to dismiss welcome");
  if (!failed &&
      !vt_expect_text(&session, "Sites", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "dashboard did not render");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed &&
      !vt_expect_text(&session, "Return to the shell?", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "exit confirm did not render");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  char idx[PATH_MAX];
  char qj[PATH_MAX];
  snprintf(idx, sizeof(idx), "%s/index.html", proj);
  snprintf(qj, sizeof(qj), "%s/quick.json", proj);
  if (!failed && (access(idx, F_OK) != 0 || access(qj, F_OK) != 0))
    failed =
        test_fail(stats, name, "scaffold files missing in the project dir");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
  env_guard_restore(&guards[0]);
  env_guard_restore(&guards[1]);
  env_guard_restore(&guards[2]);
  ob_cleanup_project(proj);
  unlink(quickd_path);
  rmdir(bin_dir);
  rmdir(xdg);
  return failed;
}

int run_tui_onboarding_adopt_no_overwrite(test_stats_t *stats,
                                          const char *binary, bool tui_enabled) {
  const char *name =
      "existing folder is adopted without overwriting user files";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-adopt-XXXXXX";
  char proj[] = "/tmp/openquick-vt-adopt-XXXXXX";
  char bin_dir[] = "/tmp/openquick-vt-abin-XXXXXX";
  if (!make_temp_dir_path(xdg) || !make_temp_dir_path(proj) ||
      !make_temp_dir_path(bin_dir)) {
    return test_fail(stats, name, "failed to create temp dirs");
  }
  char idx[PATH_MAX];
  snprintf(idx, sizeof(idx), "%s/index.html", proj);
  if (!write_text_file(idx, "<html>SENTINEL-ADOPT</html>")) {
    return test_fail(stats, name, "failed to seed index.html");
  }
  char quickd_path[PATH_MAX];
  snprintf(quickd_path, sizeof(quickd_path), "%s/quickd", bin_dir);
  if (!write_text_file(quickd_path, "#!/bin/sh\nexec sleep 120\n") ||
      chmod(quickd_path, 0755) != 0) {
    return test_fail(stats, name, "failed to write fake quickd");
  }
  env_guard_t guards[2];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "QUICK_QUICKD", quickd_path);
  vt_session_t session;
  bool started = vt_session_start(&session, binary, NULL, 0, 90, 28);
  char *snapshot = NULL;
  int failed = 0;
  char send_dir[PATH_MAX + 4];
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "Use an existing project", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "welcome did not render");
  if (!failed && !vt_send(&session, "u"))
    failed = test_fail(stats, name, "failed to choose Use existing");
  if (!failed &&
      !vt_expect_text(&session, "Folder to adopt", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "adopt folder prompt did not render");
  snprintf(send_dir, sizeof(send_dir), "%s\r", proj);
  if (!failed && !vt_send(&session, send_dir))
    failed = test_fail(stats, name, "failed to submit folder");
  if (!failed &&
      !vt_expect_text(&session, "Site name", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "name prompt did not render");
  if (!failed && !vt_send(&session, "adopted\r"))
    failed = test_fail(stats, name, "failed to submit name");
  if (!failed &&
      !vt_expect_text(&session, "Review: adopt this folder", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "adopt review did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to confirm adopt");
  if (!failed &&
      !vt_expect_text(&session, "Your site", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "preview panel did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to close preview panel");
  if (!failed &&
      !vt_expect_text(&session, "Use an existing project", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "did not return to welcome");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to dismiss welcome");
  if (!failed &&
      !vt_expect_text(&session, "Sites", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "dashboard did not render");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed &&
      !vt_expect_text(&session, "Return to the shell?", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "exit confirm did not render");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  char qj[PATH_MAX];
  snprintf(qj, sizeof(qj), "%s/quick.json", proj);
  char buf[256] = {0};
  FILE *f = fopen(idx, "rb");
  if (f) {
    (void)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
  }
  if (!failed && !strstr(buf, "SENTINEL-ADOPT"))
    failed = test_fail(stats, name, "adopt overwrote the existing index.html");
  if (!failed && access(qj, F_OK) != 0)
    failed = test_fail(stats, name, "adopt did not add quick.json");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
  env_guard_restore(&guards[0]);
  env_guard_restore(&guards[1]);
  ob_cleanup_project(proj);
  unlink(quickd_path);
  rmdir(bin_dir);
  rmdir(xdg);
  return failed;
}

int run_tui_onboarding_resize(test_stats_t *stats, const char *binary,
                              bool tui_enabled) {
  const char *name = "onboarding welcome screen stays usable after resize";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-resize-XXXXXX";
  if (!make_temp_dir_path(xdg)) {
    return test_fail(stats, name, "failed to create temp XDG dir");
  }
  env_guard_t guards[2];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "QUICK_QUICKD", NULL);
  vt_session_t session;
  bool started = vt_session_start(&session, binary, NULL, 0, 80, 24);
  char *snapshot = NULL;
  int failed = 0;
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "Try OpenQuick locally", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "welcome did not render");
  if (!failed && !vt_resize(&session, 100, 30))
    failed = test_fail(stats, name, "failed to grow the terminal");
  if (!failed &&
      !vt_expect_text(&session, "Try OpenQuick locally", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "welcome disappeared after growing");
  if (!failed && !vt_resize(&session, 80, 24))
    failed = test_fail(stats, name, "failed to shrink the terminal");
  if (!failed &&
      !vt_expect_text(&session, "Try OpenQuick locally", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "welcome disappeared after shrinking");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to dismiss welcome");
  if (!failed &&
      !vt_expect_text(&session, "Sites", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "dashboard did not render after resize");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed &&
      !vt_expect_text(&session, "Return to the shell?", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "exit confirm did not render");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
  env_guard_restore(&guards[0]);
  env_guard_restore(&guards[1]);
  rmdir(xdg);
  return failed;
}

int run_tui_onboarding_connect_invalid_retains(test_stats_t *stats,
                                               const char *binary,
                                               bool tui_enabled) {
  const char *name =
      "connect-host wizard keeps entered values on invalid input and writes no "
      "config on cancel";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-connect-XXXXXX";
  if (!make_temp_dir_path(xdg)) {
    return test_fail(stats, name, "failed to create temp XDG dir");
  }
  env_guard_t guards[2];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "QUICK_QUICKD", NULL);
  vt_session_t session;
  bool started = vt_session_start(&session, binary, NULL, 0, 90, 28);
  char *snapshot = NULL;
  int failed = 0;
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "Connect a deployment host", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "welcome did not render");
  if (!failed && !vt_send(&session, "c"))
    failed = test_fail(stats, name, "failed to choose connect host");
  if (!failed &&
      !vt_expect_text(&session, "saved host connection", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "connect intro did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to dismiss intro");
  if (!failed &&
      !vt_expect_text(&session, "Profile nickname", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "profile prompt did not render");
  if (!failed && !vt_send(&session, "lab\r"))
    failed = test_fail(stats, name, "failed to submit profile");
  if (!failed &&
      !vt_expect_text(&session, "SSH target", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "ssh prompt did not render");
  if (!failed && !vt_send(&session, "bad host\r"))
    failed = test_fail(stats, name, "failed to submit unsafe ssh target");
  if (!failed &&
      !vt_expect_text(&session, "Storage folder", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "storage prompt did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to keep storage default");
  if (!failed &&
      !vt_expect_text(&session, "Website address", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "website prompt did not render");
  if (!failed && !vt_send(&session, "example.com\r"))
    failed = test_fail(stats, name, "failed to submit initial domain");
  if (!failed &&
      !vt_expect_text(&session, "Cloudflare Access", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "iap menu did not render");
  if (!failed && !vt_send(&session, "t"))
    failed = test_fail(stats, name, "failed to choose tailscale");
  if (!failed &&
      !vt_expect_text(&session, "SSH target looks unsafe", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "unsafe ssh validation did not fire");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to dismiss ssh validation");
  if (!failed &&
      !vt_expect_text(&session, "[lab]", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name,
                       "profile value was not retained after ssh error");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to keep retained profile");
  if (!failed &&
      !vt_expect_text(&session, "[bad host]", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name,
                       "unsafe ssh value was not retained for correction");
  if (!failed && !vt_send(&session, "quick@box\r"))
    failed = test_fail(stats, name, "failed to correct ssh target");
  if (!failed &&
      !vt_expect_text(&session, "[/srv/quick]", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "storage value was not retained");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to keep retained storage");
  if (!failed &&
      !vt_expect_text(&session, "[example.com]", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "domain value was not retained");
  if (!failed && !vt_send(&session, "bad domain!\r"))
    failed = test_fail(stats, name, "failed to submit bad domain");
  if (!failed &&
      !vt_expect_text(&session, "Cloudflare Access", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "iap menu did not render on retry");
  if (!failed && !vt_send(&session, "t"))
    failed = test_fail(stats, name, "failed to keep tailscale");
  if (!failed &&
      !vt_expect_text(&session, "must be a plain DNS name", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "domain validation did not fire");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to dismiss domain validation");
  if (!failed &&
      !vt_expect_text(&session, "[lab]", PTY_TIMEOUT_MS, &snapshot))
    failed =
        test_fail(stats, name, "profile value was not retained after error");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to cancel wizard");
  if (!failed &&
      !vt_expect_text(&session, "Try OpenQuick locally", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "did not return to welcome after cancel");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to dismiss welcome");
  if (!failed &&
      !vt_expect_text(&session, "Sites", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "dashboard did not render");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed &&
      !vt_expect_text(&session, "Return to the shell?", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "exit confirm did not render");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  char cfg[PATH_MAX];
  snprintf(cfg, sizeof(cfg), "%s/openquick/config.json", xdg);
  if (!failed && access(cfg, F_OK) == 0)
    failed = test_fail(stats, name,
                       "cancelled wizard wrote a profile config file");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
  env_guard_restore(&guards[0]);
  env_guard_restore(&guards[1]);
  unlink(cfg);
  char od[PATH_MAX];
  snprintf(od, sizeof(od), "%s/openquick", xdg);
  rmdir(od);
  rmdir(xdg);
  return failed;
}

int run_tui_onboarding_newhost_review_no_mutation(test_stats_t *stats,
                                                  const char *binary,
                                                  bool tui_enabled) {
  const char *name =
      "new-host wizard shows a review and makes no changes before confirmation";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }
  char xdg[] = "/tmp/openquick-vt-xdg-newrev-XXXXXX";
  char bin_dir[] = "/tmp/openquick-vt-nbin-XXXXXX";
  if (!make_temp_dir_path(xdg) || !make_temp_dir_path(bin_dir)) {
    return test_fail(stats, name, "failed to create temp dirs");
  }
  char ssh_path[PATH_MAX];
  char ssh_log[PATH_MAX];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(ssh_log, sizeof(ssh_log), "%s/ssh.log", bin_dir);
  if (!write_text_file(ssh_path,
                       "#!/bin/sh\necho called >> \"$OQ_SSH_LOG\"\nexit 0\n") ||
      chmod(ssh_path, 0755) != 0) {
    return test_fail(stats, name, "failed to write fake ssh");
  }
  char path_value[PATH_MAX * 2];
  const char *old_path = getenv("PATH");
  snprintf(path_value, sizeof(path_value), "%s%s%s", bin_dir,
           old_path && old_path[0] ? ":" : "",
           old_path && old_path[0] ? old_path : "");
  env_guard_t guards[3];
  env_guard_set(&guards[0], "XDG_CONFIG_HOME", xdg);
  env_guard_set(&guards[1], "PATH", path_value);
  env_guard_set(&guards[2], "OQ_SSH_LOG", ssh_log);
  vt_session_t session;
  bool started = vt_session_start(&session, binary, NULL, 0, 90, 28);
  char *snapshot = NULL;
  int failed = 0;
  if (!started)
    failed = test_fail(stats, name, "failed to start PTY session");
  if (!failed &&
      !vt_expect_text(&session, "Set up a new host", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "welcome did not render");
  if (!failed && !vt_send(&session, "s"))
    failed = test_fail(stats, name, "failed to choose set up host");
  if (!failed &&
      !vt_expect_text(&session, "installs OpenQuick", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "requirements message did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to dismiss requirements");
  if (!failed &&
      !vt_expect_text(&session, "Profile nickname", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "profile prompt did not render");
  if (!failed && !vt_send(&session, "lab\r"))
    failed = test_fail(stats, name, "failed to submit profile");
  if (!failed &&
      !vt_expect_text(&session, "SSH target", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "ssh prompt did not render");
  if (!failed && !vt_send(&session, "quick@box\r"))
    failed = test_fail(stats, name, "failed to submit ssh target");
  if (!failed &&
      !vt_expect_text(&session, "Storage folder", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "storage prompt did not render");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to keep storage default");
  if (!failed &&
      !vt_expect_text(&session, "Website address", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "website prompt did not render");
  if (!failed && !vt_send(&session, "example.com\r"))
    failed = test_fail(stats, name, "failed to submit domain");
  if (!failed &&
      !vt_expect_text(&session, "Cloudflare Access", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "iap menu did not render");
  if (!failed && !vt_send(&session, "t"))
    failed = test_fail(stats, name, "failed to choose tailscale");
  if (!failed &&
      !vt_expect_text(&session, "Review install plan", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "install review did not render");
  if (!failed &&
      !vt_expect_text(&session, "nothing changed yet", 1500, &snapshot))
    failed = test_fail(stats, name, "review did not promise no mutation");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to go back from review");
  if (!failed &&
      !vt_expect_text(&session, "Profile nickname", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "did not return to editing after Back");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to cancel wizard");
  if (!failed &&
      !vt_expect_text(&session, "Try OpenQuick locally", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "did not return to welcome");
  if (!failed && !vt_send(&session, "\x1b"))
    failed = test_fail(stats, name, "failed to dismiss welcome");
  if (!failed &&
      !vt_expect_text(&session, "Sites", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "dashboard did not render");
  if (!failed && !vt_send(&session, "q"))
    failed = test_fail(stats, name, "failed to start exit");
  if (!failed &&
      !vt_expect_text(&session, "Return to the shell?", PTY_TIMEOUT_MS,
                      &snapshot))
    failed = test_fail(stats, name, "exit confirm did not render");
  if (!failed && !vt_send(&session, "y"))
    failed = test_fail(stats, name, "failed to confirm exit");
  if (!failed && vt_wait_for_exit(&session, PTY_TIMEOUT_MS) != 0)
    failed = test_fail(stats, name, "process did not exit cleanly");
  if (!failed && access(ssh_log, F_OK) == 0)
    failed = test_fail(stats, name,
                       "ssh ran before the plan was confirmed (mutation)");
  char cfg[PATH_MAX];
  snprintf(cfg, sizeof(cfg), "%s/openquick/config.json", xdg);
  if (!failed && access(cfg, F_OK) == 0)
    failed = test_fail(stats, name,
                       "profile was written before install succeeded");
  if (!failed)
    test_pass(stats, name);
  free(snapshot);
  if (started)
    vt_session_close(&session);
  env_guard_restore(&guards[0]);
  env_guard_restore(&guards[1]);
  env_guard_restore(&guards[2]);
  unlink(ssh_log);
  unlink(ssh_path);
  unlink(cfg);
  char od[PATH_MAX];
  snprintf(od, sizeof(od), "%s/openquick", xdg);
  rmdir(od);
  rmdir(bin_dir);
  rmdir(xdg);
  return failed;
}

static char *ob_read_text_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return NULL;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long size = ftell(f);
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  char *body = calloc((size_t)size + 1U, 1U);
  if (!body) {
    fclose(f);
    return NULL;
  }
  size_t read_size = fread(body, 1, (size_t)size, f);
  if (ferror(f)) {
    free(body);
    body = NULL;
  } else {
    body[read_size] = '\0';
  }
  fclose(f);
  return body;
}

static bool ob_file_contains(const char *path, const char *needle) {
  char *body = ob_read_text_file(path);
  bool found = body && strstr(body, needle) != NULL;
  free(body);
  return found;
}

static void ob_cleanup_xdg(const char *xdg) {
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/openquick/config.json", xdg);
  unlink(path);
  snprintf(path, sizeof(path), "%s/openquick", xdg);
  rmdir(path);
  rmdir(xdg);
}

static int ob_expect(test_stats_t *stats, const char *name,
                     vt_session_t *session, const char *needle, int timeout_ms,
                     char **snapshot, const char *failure) {
  if (vt_expect_text(session, needle, timeout_ms, snapshot)) {
    return 0;
  }
  print_tail(stderr, "screen:\n", *snapshot ? *snapshot : "",
             *snapshot ? strlen(*snapshot) : 0, 4000);
  print_tail(stderr, "transcript:\n", buffer_cstr(&session->transcript),
             session->transcript.len, 4000);
  return test_fail(stats, name, "%s", failure);
}

static bool ob_write_executable(const char *path, const char *body) {
  return write_text_file(path, body) && chmod(path, 0755) == 0;
}

static bool ob_write_fake_host_tools(const char *bin_dir) {
  static const char ssh_script[] =
      "#!/bin/sh\n"
      "{ printf 'ssh'; for arg in \"$@\"; do printf ' <%s>' \"$arg\"; done; "
      "printf '\\n'; } >> \"$OQ_SSH_LOG\"\n"
      "while [ \"$1\" = -o ]; do [ \"$#\" -ge 2 ] || exit 97; shift 2; "
      "done\n"
      "host=$1\n"
      "shift\n"
      "delay=${OQ_FAKE_DELAY:-0}\n"
      "[ \"$delay\" = 0 ] || sleep \"$delay\"\n"
      "if [ \"$1\" = sh ] && [ \"$2\" = -s ] && [ \"$3\" = -- ]; then\n"
      "  script=$(cat)\n"
      "  printf '%s\\n' \"$script\" >> \"$OQ_SCRIPT_LOG\"\n"
      "  case \"$script\" in\n"
      "    *'# openquick backup'*) echo backup >> \"$OQ_EVENTS_LOG\" ;;\n"
      "    *'# openquick rollback'*) echo rollback >> \"$OQ_EVENTS_LOG\" ;;\n"
      "    *'# openquick pre-mutation cleanup'*) echo cleanup >> "
      "\"$OQ_EVENTS_LOG\" ;;\n"
      "    *) echo unknown-script >> \"$OQ_EVENTS_LOG\"; exit 96 ;;\n"
      "  esac\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$1\" = sudo ] && [ \"$2\" = tee ]; then\n"
      "  cat >/dev/null\n"
      "  echo \"tee $3\" >> \"$OQ_EVENTS_LOG\"\n"
      "  exit 0\n"
      "fi\n"
      "case \"$*\" in\n"
      "  'uname -s') printf '%s\\n' Linux; exit 0 ;;\n"
      "  'mktemp -d /tmp/openquick-install.XXXXXX')\n"
      "    printf '%s\\n' /tmp/oq; exit 0 ;;\n"
      "  'id -un') printf '%s\\n' deployer; exit 0 ;;\n"
      "  'id -u quick') exit 1 ;;\n"
      "  'quickd doctor --host --json')\n"
      "    echo host-doctor >> \"$OQ_EVENTS_LOG\"\n"
      "    if [ \"${OQ_DOCTOR_FAIL:-0}\" = 1 ]; then\n"
      "      printf '%s\\n' "
      "'{\"status\":\"fail\",\"checks\":[{\"status\":\"fail\",\"name\":\"service\"}]}'\n"
      "      exit 1\n"
      "    fi\n"
      "    printf '%s\\n' "
      "'{\"status\":\"ok\",\"checks\":[{\"status\":\"ok\"}]}'\n"
      "    exit 0 ;;\n"
      "  'quickd admin stats --json')\n"
      "    printf '%s\\n' '{\"sites\":2,\"releases\":3}'; exit 0 ;;\n"
      "  'sudo groupadd --system --force quick-deploy')\n"
      "    echo groupadd >> \"$OQ_EVENTS_LOG\"; exit 0 ;;\n"
      "esac\n"
      "exit 0\n";
  static const char scp_script[] =
      "#!/bin/sh\n"
      "{ printf 'scp'; for arg in \"$@\"; do printf ' <%s>' \"$arg\"; done; "
      "printf '\\n'; } >> \"$OQ_SCP_LOG\"\n"
      "while [ \"$1\" = -o ]; do [ \"$#\" -ge 2 ] || exit 97; shift 2; "
      "done\n"
      "echo scp >> \"$OQ_EVENTS_LOG\"\n"
      "exit 0\n";
  static const char rsync_script[] = "#!/bin/sh\nexit 0\n";
  static const char curl_script[] =
      "#!/bin/sh\n"
      "{ printf 'curl'; for arg in \"$@\"; do printf ' <%s>' \"$arg\"; done; "
      "printf '\\n'; } >> \"$OQ_CURL_LOG\"\n"
      "url=\n"
      "for arg in \"$@\"; do url=$arg; done\n"
      "delay=${OQ_FAKE_DELAY:-0}\n"
      "[ \"$delay\" = 0 ] || sleep \"$delay\"\n"
      "case \"$url\" in\n"
      "  */_quick/identity) printf '%s\\n' "
      "'{\"authenticated\":true,\"provider\":\"tailscale\",\"subject\":\"user:test\"}' ;;\n"
      "  */_quick/health) printf '%s\\n' '{\"status\":\"ok\"}' ;;\n"
      "  *) printf '%s\\n' ok ;;\n"
      "esac\n"
      "exit 0\n";
  static const char quickd_script[] = "#!/bin/sh\nexit 0\n";

  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s/ssh", bin_dir);
  if (!ob_write_executable(path, ssh_script)) {
    return false;
  }
  snprintf(path, sizeof(path), "%s/scp", bin_dir);
  if (!ob_write_executable(path, scp_script)) {
    return false;
  }
  snprintf(path, sizeof(path), "%s/rsync", bin_dir);
  if (!ob_write_executable(path, rsync_script)) {
    return false;
  }
  snprintf(path, sizeof(path), "%s/curl", bin_dir);
  if (!ob_write_executable(path, curl_script)) {
    return false;
  }
  snprintf(path, sizeof(path), "%s/quickd", bin_dir);
  return ob_write_executable(path, quickd_script);
}

static void ob_cleanup_fake_host_tools(const char *bin_dir) {
  const char *names[] = {"ssh", "scp", "rsync", "curl", "quickd"};
  char path[PATH_MAX];
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    snprintf(path, sizeof(path), "%s/%s", bin_dir, names[i]);
    unlink(path);
  }
  rmdir(bin_dir);
}

static void ob_cleanup_fake_state(const char *state_dir) {
  const char *names[] = {"ssh.log", "scp.log", "scripts.log", "events.log",
                         "curl.log"};
  char path[PATH_MAX];
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    snprintf(path, sizeof(path), "%s/%s", state_dir, names[i]);
    unlink(path);
  }
  rmdir(state_dir);
}

static int ob_drive_host_to_review(test_stats_t *stats, const char *name,
                                   vt_session_t *session, bool install,
                                   char **snapshot) {
  const char *welcome =
      install ? "Set up a new host" : "Connect a deployment host";
  const char *intro = install ? "installs OpenQuick" : "saved host connection";
  const char *review =
      install ? "Review install plan" : "Review host connection";
  if (ob_expect(stats, name, session, welcome, PTY_TIMEOUT_MS, snapshot,
                "onboarding welcome choice did not render")) {
    return 1;
  }
  if (!vt_send(session, install ? "s" : "c")) {
    return test_fail(stats, name, "failed to choose host onboarding flow");
  }
  if (ob_expect(stats, name, session, intro, PTY_TIMEOUT_MS, snapshot,
                "host setup introduction did not render")) {
    return 1;
  }
  if (!vt_send(session, "\r")) {
    return test_fail(stats, name, "failed to dismiss host setup introduction");
  }
  if (ob_expect(stats, name, session, "Profile nickname", PTY_TIMEOUT_MS,
                snapshot, "profile prompt did not render")) {
    return 1;
  }
  if (!vt_send(session, "lab\r")) {
    return test_fail(stats, name, "failed to submit profile nickname");
  }
  if (ob_expect(stats, name, session, "SSH target", PTY_TIMEOUT_MS, snapshot,
                "ssh prompt did not render")) {
    return 1;
  }
  if (!vt_send(session, "quick@box\r")) {
    return test_fail(stats, name, "failed to submit ssh target");
  }
  if (ob_expect(stats, name, session, "Storage folder", PTY_TIMEOUT_MS,
                snapshot, "storage prompt did not render")) {
    return 1;
  }
  if (!vt_send(session, install ? "/q\r" : "\r")) {
    return test_fail(stats, name, "failed to submit storage folder");
  }
  if (ob_expect(stats, name, session, "Website address", PTY_TIMEOUT_MS,
                snapshot, "website prompt did not render")) {
    return 1;
  }
  if (!vt_send(session, "example.com\r")) {
    return test_fail(stats, name, "failed to submit website address");
  }
  if (ob_expect(stats, name, session, "Cloudflare Access", PTY_TIMEOUT_MS,
                snapshot, "access menu did not render")) {
    return 1;
  }
  if (!vt_send(session, "t")) {
    return test_fail(stats, name, "failed to choose Tailscale");
  }
  if (ob_expect(stats, name, session, review, PTY_TIMEOUT_MS, snapshot,
                "host review did not render")) {
    return 1;
  }
  return 0;
}

static int ob_exit_dashboard(test_stats_t *stats, const char *name,
                             vt_session_t *session, char **snapshot) {
  if (!vt_send(session, "q")) {
    return test_fail(stats, name, "failed to start dashboard exit");
  }
  if (ob_expect(stats, name, session, "Return to the shell?", PTY_TIMEOUT_MS,
                snapshot, "exit confirmation did not render")) {
    return 1;
  }
  if (!vt_send(session, "y")) {
    return test_fail(stats, name, "failed to confirm dashboard exit");
  }
  int exit_code = vt_wait_for_exit(session, PTY_TIMEOUT_MS);
  return exit_code == 0
             ? 0
             : test_fail(stats, name, "expected exit 0, got %d", exit_code);
}

int run_tui_onboarding_connect_success(test_stats_t *stats, const char *binary,
                                       bool tui_enabled) {
  const char *name =
      "connect-host onboarding verifies a bounded candidate before saving it";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }

  char xdg[] = "/tmp/openquick-vt-xdg-connect-ok-XXXXXX";
  char project[] = "/tmp/openquick-vt-project-connect-ok-XXXXXX";
  char bin_dir[] = "/tmp/openquick-vt-bin-connect-ok-XXXXXX";
  char state_dir[] = "/tmp/openquick-vt-state-connect-ok-XXXXXX";
  char original_cwd[PATH_MAX] = {0};
  char config_path[PATH_MAX] = {0};
  char quick_json[PATH_MAX] = {0}, index_path[PATH_MAX] = {0};
  char ignore_path[PATH_MAX] = {0};
  char quickd_path[PATH_MAX] = {0}, ssh_log[PATH_MAX] = {0};
  char scp_log[PATH_MAX] = {0}, script_log[PATH_MAX] = {0};
  char events_log[PATH_MAX] = {0}, curl_log[PATH_MAX] = {0};
  char path_value[PATH_MAX * 2] = {0};
  env_guard_t guards[14] = {0};
  size_t guard_count = 0;
  vt_session_t session;
  bool started = false;
  bool cwd_changed = false;
  char *snapshot = NULL;
  int failed = 0;

  if (!getcwd(original_cwd, sizeof(original_cwd)) ||
      !make_temp_dir_path(xdg) || !make_temp_dir_path(project) ||
      !make_temp_dir_path(bin_dir) || !make_temp_dir_path(state_dir)) {
    failed = test_fail(stats, name, "failed to create isolated test inputs: %s",
                       strerror(errno));
    goto cleanup;
  }
  snprintf(config_path, sizeof(config_path), "%s/openquick/config.json", xdg);
  snprintf(quick_json, sizeof(quick_json), "%s/quick.json", project);
  snprintf(index_path, sizeof(index_path), "%s/index.html", project);
  snprintf(ignore_path, sizeof(ignore_path), "%s/.quickignore", project);
  snprintf(quickd_path, sizeof(quickd_path), "%s/quickd", bin_dir);
  snprintf(ssh_log, sizeof(ssh_log), "%s/ssh.log", state_dir);
  snprintf(scp_log, sizeof(scp_log), "%s/scp.log", state_dir);
  snprintf(script_log, sizeof(script_log), "%s/scripts.log", state_dir);
  snprintf(events_log, sizeof(events_log), "%s/events.log", state_dir);
  snprintf(curl_log, sizeof(curl_log), "%s/curl.log", state_dir);

  if (!write_text_file(quick_json,
                       "{\"name\":\"candidate\",\"source\":\".\","
                       "\"output\":\".\",\"subdomain\":\"candidate\"}\n") ||
      !write_text_file(index_path, "<!doctype html><title>candidate</title>\n") ||
      !write_text_file(ignore_path, ".git/\n") ||
      !ob_write_fake_host_tools(bin_dir)) {
    failed = test_fail(stats, name, "failed to write project or fake tools");
    goto cleanup;
  }

  const char *old_path = getenv("PATH");
  snprintf(path_value, sizeof(path_value), "%s%s%s", bin_dir,
           old_path && old_path[0] ? ":" : "",
           old_path && old_path[0] ? old_path : "");
#define OB_SET_ENV(key, value)                                                   \
  env_guard_set(&guards[guard_count++], (key), (value))
  OB_SET_ENV("XDG_CONFIG_HOME", xdg);
  OB_SET_ENV("PATH", path_value);
  OB_SET_ENV("QUICK_QUICKD", quickd_path);
  OB_SET_ENV("OQ_SSH_LOG", ssh_log);
  OB_SET_ENV("OQ_SCP_LOG", scp_log);
  OB_SET_ENV("OQ_SCRIPT_LOG", script_log);
  OB_SET_ENV("OQ_EVENTS_LOG", events_log);
  OB_SET_ENV("OQ_CURL_LOG", curl_log);
  OB_SET_ENV("OQ_DOCTOR_FAIL", "0");
  OB_SET_ENV("OQ_FAKE_DELAY", "0.20");
  OB_SET_ENV("QUICK_PROFILE", NULL);
  OB_SET_ENV("QUICK_REMOTE", NULL);
  OB_SET_ENV("QUICK_BASE_DOMAIN", NULL);
  OB_SET_ENV("QUICK_CONFIG_PATH", NULL);
#undef OB_SET_ENV

  if (chdir(project) != 0) {
    failed = test_fail(stats, name, "failed to enter candidate project: %s",
                       strerror(errno));
    goto cleanup;
  }
  cwd_changed = true;
  started = vt_session_start(&session, binary, NULL, 0, 100, 30);
  if (chdir(original_cwd) != 0) {
    failed = test_fail(stats, name, "failed to restore parent cwd: %s",
                       strerror(errno));
    goto cleanup;
  }
  cwd_changed = false;
  if (!started) {
    failed = test_fail(stats, name, "failed to start PTY session");
    goto cleanup;
  }

  failed = ob_expect(stats, name, &session, "Get started", PTY_TIMEOUT_MS,
                     &snapshot,
                     "valid candidate project dashboard did not render");
  if (!failed && !vt_send(&session, "g")) {
    failed = test_fail(stats, name, "failed to open onboarding from project");
  }
  if (!failed) {
    failed = ob_drive_host_to_review(stats, name, &session, false, &snapshot);
  }
  if (!failed && access(config_path, F_OK) == 0) {
    failed = test_fail(stats, name, "profile was written before review");
  }
  if (!failed && !vt_send(&session, "\r")) {
    failed = test_fail(stats, name, "failed to confirm host review");
  }
  if (!failed) {
    failed = ob_expect(stats, name, &session, "Checking host", PTY_TIMEOUT_MS,
                       &snapshot, "host verification progress did not render");
  }
  if (!failed && access(config_path, F_OK) == 0) {
    failed = test_fail(stats, name,
                       "profile was written before verification completed");
  }
  if (!failed) {
    failed = ob_expect(stats, name, &session,
                       "Saved profile \"lab\" and verified the host.",
                       PTY_TIMEOUT_MS, &snapshot,
                       "verified connection success did not render");
  }
  if (!failed && access(config_path, F_OK) != 0) {
    failed = test_fail(stats, name, "verified profile config was not saved");
  }
  if (!failed &&
      (!ob_file_contains(config_path, "\"default_profile\": \"lab\"") ||
       !ob_file_contains(config_path, "\"ssh\": \"quick@box\""))) {
    failed = test_fail(stats, name,
                       "saved profile does not contain the candidate host");
  }
  if (!failed && !vt_send(&session, "\r")) {
    failed = test_fail(stats, name, "failed to dismiss connection success");
  }
  if (!failed) {
    failed = ob_expect(stats, name, &session, "Sites", PTY_TIMEOUT_MS,
                       &snapshot, "dashboard did not return after connection");
  }
  if (!failed) {
    failed = ob_exit_dashboard(stats, name, &session, &snapshot);
  }
  if (!failed &&
      (!ob_file_contains(ssh_log, "BatchMode=yes") ||
       !ob_file_contains(ssh_log, "ConnectTimeout=10") ||
       !ob_file_contains(ssh_log, "ConnectionAttempts=1") ||
       !ob_file_contains(ssh_log, "quick@box") ||
       !ob_file_contains(ssh_log, "<quickd>") ||
       !ob_file_contains(ssh_log, "<doctor>") ||
       !ob_file_contains(ssh_log, "<--host>") ||
       !ob_file_contains(ssh_log, "<admin>") ||
       !ob_file_contains(ssh_log, "<stats>"))) {
    failed = test_fail(stats, name,
                       "SSH verification did not use bounded candidate args");
  }
  if (!failed &&
      (!ob_file_contains(curl_log, "/_quick/health") ||
       !ob_file_contains(curl_log, "/_quick/identity"))) {
    failed = test_fail(stats, name,
                       "verification did not probe health and identity");
  }
  if (!failed) {
    test_pass(stats, name);
  }

cleanup:
  if (started) {
    vt_session_close(&session);
  }
  if (cwd_changed && original_cwd[0]) {
    (void)chdir(original_cwd);
  }
  free(snapshot);
  restore_common_env(guards, guard_count);
  if (quick_json[0])
    unlink(quick_json);
  if (index_path[0])
    unlink(index_path);
  if (ignore_path[0])
    unlink(ignore_path);
  rmdir(project);
  ob_cleanup_fake_host_tools(bin_dir);
  ob_cleanup_fake_state(state_dir);
  ob_cleanup_xdg(xdg);
  return failed;
}

static int ob_run_install_case(test_stats_t *stats, const char *binary,
                               bool tui_enabled, bool doctor_fail) {
  const char *name = doctor_fail
                         ? "new-host onboarding rolls back a failed host doctor"
                         : "new-host onboarding installs, verifies, then saves";
  if (!tui_enabled) {
    test_skip(stats, name, "rebuild with -Denable-tui=true");
    return 0;
  }

  char xdg[] = "/tmp/openquick-vt-xdg-install-XXXXXX";
  char bin_dir[] = "/tmp/openquick-vt-bin-install-XXXXXX";
  char state_dir[] = "/tmp/openquick-vt-state-install-XXXXXX";
  char config_path[PATH_MAX] = {0};
  char quickd_path[PATH_MAX] = {0};
  char ssh_log[PATH_MAX] = {0}, scp_log[PATH_MAX] = {0};
  char script_log[PATH_MAX] = {0}, events_log[PATH_MAX] = {0};
  char curl_log[PATH_MAX] = {0}, path_value[PATH_MAX * 2] = {0};
  env_guard_t guards[15] = {0};
  size_t guard_count = 0;
  vt_session_t session;
  bool started = false;
  char *snapshot = NULL;
  int failed = 0;

  if (!make_temp_dir_path(xdg) || !make_temp_dir_path(bin_dir) ||
      !make_temp_dir_path(state_dir)) {
    failed = test_fail(stats, name, "failed to create isolated temp dirs: %s",
                       strerror(errno));
    goto cleanup;
  }
  snprintf(config_path, sizeof(config_path), "%s/openquick/config.json", xdg);
  snprintf(quickd_path, sizeof(quickd_path), "%s/quickd", bin_dir);
  snprintf(ssh_log, sizeof(ssh_log), "%s/ssh.log", state_dir);
  snprintf(scp_log, sizeof(scp_log), "%s/scp.log", state_dir);
  snprintf(script_log, sizeof(script_log), "%s/scripts.log", state_dir);
  snprintf(events_log, sizeof(events_log), "%s/events.log", state_dir);
  snprintf(curl_log, sizeof(curl_log), "%s/curl.log", state_dir);
  if (!ob_write_fake_host_tools(bin_dir)) {
    failed = test_fail(stats, name, "failed to write fake host tools");
    goto cleanup;
  }

  const char *old_path = getenv("PATH");
  snprintf(path_value, sizeof(path_value), "%s%s%s", bin_dir,
           old_path && old_path[0] ? ":" : "",
           old_path && old_path[0] ? old_path : "");
#define OB_SET_ENV(key, value)                                                   \
  env_guard_set(&guards[guard_count++], (key), (value))
  OB_SET_ENV("XDG_CONFIG_HOME", xdg);
  OB_SET_ENV("PATH", path_value);
  OB_SET_ENV("QUICK_QUICKD", quickd_path);
  OB_SET_ENV("OQ_SSH_LOG", ssh_log);
  OB_SET_ENV("OQ_SCP_LOG", scp_log);
  OB_SET_ENV("OQ_SCRIPT_LOG", script_log);
  OB_SET_ENV("OQ_EVENTS_LOG", events_log);
  OB_SET_ENV("OQ_CURL_LOG", curl_log);
  OB_SET_ENV("OQ_DOCTOR_FAIL", doctor_fail ? "1" : "0");
  OB_SET_ENV("OQ_FAKE_DELAY", "0.03");
  OB_SET_ENV("QUICK_PROFILE", NULL);
  OB_SET_ENV("QUICK_REMOTE", NULL);
  OB_SET_ENV("QUICK_BASE_DOMAIN", NULL);
  OB_SET_ENV("QUICK_CONFIG_PATH", NULL);
  OB_SET_ENV("QUICK_INSTALL_DIR", NULL);
#undef OB_SET_ENV

  started = vt_session_start(&session, binary, NULL, 0, 110, 40);
  if (!started) {
    failed = test_fail(stats, name, "failed to start PTY session");
    goto cleanup;
  }
  failed = ob_drive_host_to_review(stats, name, &session, true, &snapshot);
  if (!failed) {
    failed = ob_expect(stats, name, &session, "nothing changed yet", 1500,
                       &snapshot, "review did not promise no mutation");
  }
  if (!failed && (access(ssh_log, F_OK) == 0 || access(scp_log, F_OK) == 0 ||
                  access(config_path, F_OK) == 0)) {
    failed = test_fail(stats, name,
                       "install mutated or saved before confirmation");
  }
  if (!failed && !vt_send(&session, "\r")) {
    failed = test_fail(stats, name, "failed to confirm install review");
  }
  if (!failed) {
    failed = ob_expect(stats, name, &session, "Setting up host", PTY_TIMEOUT_MS,
                       &snapshot, "install progress did not render");
  }
  if (!failed && access(config_path, F_OK) == 0) {
    failed = test_fail(stats, name,
                       "profile was saved while installation was in progress");
  }

  if (!doctor_fail) {
    if (!failed) {
      failed = ob_expect(stats, name, &session, "HOST READY", PTY_TIMEOUT_MS,
                         &snapshot, "install success panel did not render");
    }
    if (!failed) {
      failed = ob_expect(stats, name, &session, "Host doctor: healthy", 1500,
                         &snapshot, "healthy host doctor status was missing");
    }
    if (!failed) {
      failed = ob_expect(stats, name, &session,
                         "Backup kept: /tmp/oq/backup", 1500, &snapshot,
                         "backup path was missing on success");
    }
    if (!failed && access(config_path, F_OK) != 0) {
      failed = test_fail(stats, name,
                         "profile config was not saved after install success");
    }
    if (!failed &&
        (!ob_file_contains(config_path, "\"default_profile\": \"lab\"") ||
         !ob_file_contains(config_path, "\"ssh\": \"quick@box\""))) {
      failed = test_fail(stats, name,
                         "saved install profile is missing expected values");
    }
    if (!failed && !vt_send(&session, "\r")) {
      failed = test_fail(stats, name, "failed to dismiss install success");
    }
    if (!failed) {
      failed = ob_expect(stats, name, &session, "Sites", PTY_TIMEOUT_MS,
                         &snapshot, "dashboard did not return after install");
    }
    if (!failed) {
      failed = ob_exit_dashboard(stats, name, &session, &snapshot);
    }
  } else {
    if (!failed) {
      failed = ob_expect(stats, name, &session, "HOST SETUP FAILED",
                         PTY_TIMEOUT_MS, &snapshot,
                         "install failure panel did not render");
    }
    const char *failure_text[] = {
        "Setup failed during: host doctor",
        "Inspect quickd on the host",
        "/tmp/oq/backup",
        "Rollback restored the previous",
        "Cleanup residue remains",
        "quick user, quick-deploy group",
    };
    for (size_t i = 0; !failed &&
                       i < sizeof(failure_text) / sizeof(failure_text[0]);
         i++) {
      failed = ob_expect(stats, name, &session, failure_text[i], 1500,
                         &snapshot,
                         "doctor failure details/remediation were incomplete");
    }
    if (!failed && access(config_path, F_OK) == 0) {
      failed = test_fail(stats, name,
                         "failed install wrote a local profile config");
    }
    if (!failed && !vt_send(&session, "\r")) {
      failed = test_fail(stats, name, "failed to dismiss install failure");
    }
    if (!failed) {
      failed = ob_expect(stats, name, &session, "Try OpenQuick locally",
                         PTY_TIMEOUT_MS, &snapshot,
                         "welcome did not return after failed install");
    }
    if (!failed && !vt_send(&session, "\x1b")) {
      failed = test_fail(stats, name, "failed to dismiss returned welcome");
    }
    if (!failed) {
      failed = ob_expect(stats, name, &session, "Sites", PTY_TIMEOUT_MS,
                         &snapshot,
                         "dashboard did not render after failed install");
    }
    if (!failed) {
      failed = ob_exit_dashboard(stats, name, &session, &snapshot);
    }
  }

  if (!failed &&
      (!ob_file_contains(ssh_log, "BatchMode=yes") ||
       !ob_file_contains(ssh_log, "ConnectTimeout=10") ||
       !ob_file_contains(ssh_log, "ConnectionAttempts=1") ||
       !ob_file_contains(ssh_log, "quick@box"))) {
    failed = test_fail(stats, name, "SSH install calls were not bounded");
  }
  if (!failed &&
      (!ob_file_contains(scp_log, "BatchMode=yes") ||
       !ob_file_contains(scp_log, "ConnectTimeout=10") ||
       !ob_file_contains(scp_log, "ConnectionAttempts=1") ||
       !ob_file_contains(scp_log, "quick@box:"))) {
    failed = test_fail(stats, name, "scp install call was not bounded");
  }
  char *events = !failed ? ob_read_text_file(events_log) : NULL;
  if (!failed) {
    char *backup = events ? strstr(events, "backup") : NULL;
    char *groupadd = events ? strstr(events, "groupadd") : NULL;
    if (!backup || !groupadd || backup >= groupadd) {
      failed = test_fail(stats, name,
                         "backup event did not precede user/group mutation");
    }
  }
  if (!failed && doctor_fail &&
      (!ob_file_contains(events_log, "rollback") ||
       !ob_file_contains(script_log, "# openquick rollback") ||
       !ob_file_contains(script_log, "sudo cp -p"))) {
    failed = test_fail(stats, name,
                       "rollback/restore script was not executed and logged");
  }
  if (!failed && !doctor_fail && ob_file_contains(events_log, "rollback")) {
    failed = test_fail(stats, name, "successful install unexpectedly rolled back");
  }
  free(events);
  if (!failed) {
    test_pass(stats, name);
  }

cleanup:
  if (started) {
    vt_session_close(&session);
  }
  free(snapshot);
  restore_common_env(guards, guard_count);
  ob_cleanup_fake_host_tools(bin_dir);
  ob_cleanup_fake_state(state_dir);
  ob_cleanup_xdg(xdg);
  return failed;
}

int run_tui_onboarding_install_success(test_stats_t *stats, const char *binary,
                                       bool tui_enabled) {
  return ob_run_install_case(stats, binary, tui_enabled, false);
}

int run_tui_onboarding_install_failure_rollback(test_stats_t *stats,
                                                const char *binary,
                                                bool tui_enabled) {
  return ob_run_install_case(stats, binary, tui_enabled, true);
}
