#include "terminal_vt_scenarios.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
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
  static const char title[] = "STARTER SHOWCASE";
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
    if (vt_expect_text(session, "STARTER SHOWCASE", 100, snapshot) &&
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

int run_tui_menu_test(test_stats_t *stats, const char *binary,
                      bool tui_enabled) {
  const char *name = "all TUI screens render through Ghostty VT";
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
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
       "STARTER SHOWCASE did not appear"},
      {TUI_STEP_EXPECT, "Overview", 0, 0, 0, 1000,
       "menu label did not appear: Overview"},
      {TUI_STEP_EXPECT, "System Information", 0, 0, 0, 1000,
       "menu label did not appear: System Information"},
      {TUI_STEP_EXPECT, "Input Dialog", 0, 0, 0, 1000,
       "menu label did not appear: Input Dialog"},
      {TUI_STEP_EXPECT, "Progress Pattern", 0, 0, 0, 1000,
       "menu label did not appear: Progress Pattern"},
      {TUI_STEP_EXPECT, "Layout Pattern", 0, 0, 0, 1000,
       "menu label did not appear: Layout Pattern"},
      {TUI_STEP_EXPECT, "Exit", 0, 0, 0, 1000,
       "menu label did not appear: Exit"},

      {TUI_STEP_SEND, "o", 0, 0, 0, 0, "failed to select Overview"},
      {TUI_STEP_EXPECT, "STARTER OVERVIEW", 0, 0, 0, PTY_TIMEOUT_MS,
       "Starter Overview did not appear"},
      {TUI_STEP_EXPECT, "C23 modules", 0, 0, 0, 1000,
       "overview body did not appear"},
      {TUI_STEP_SEND, "x", 0, 0, 0, 0, "failed to dismiss overview"},
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
       "menu did not reappear after overview"},

      {TUI_STEP_SEND, "s", 0, 0, 0, 0, "failed to select System Information"},
      {TUI_STEP_EXPECT, "SYSTEM INFORMATION", 0, 0, 0, PTY_TIMEOUT_MS,
       "System Information did not appear"},
      {TUI_STEP_EXPECT, "Application:", 0, 0, 0, 1000,
       "system information body was incomplete: Application"},
      {TUI_STEP_EXPECT, "Terminal Size:", 0, 0, 0, 1000,
       "system information body was incomplete: Terminal Size"},
      {TUI_STEP_EXPECT, "Colors Supported:", 0, 0, 0, 1000,
       "system information body was incomplete: Colors Supported"},
      {TUI_STEP_SEND, "x", 0, 0, 0, 0, "failed to dismiss system information"},
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
       "menu did not reappear after system info"},

      {TUI_STEP_SEND, "i", 0, 0, 0, 0, "failed to select Input Dialog"},
      {TUI_STEP_EXPECT, "INPUT DIALOG", 0, 0, 0, PTY_TIMEOUT_MS,
       "Input Dialog did not appear"},
      {TUI_STEP_EXPECT, "Enter a display name:", 0, 0, 0, 1000,
       "input prompt did not appear"},
      {TUI_STEP_SEND, "Ada Lovelace\r", 0, 0, 0, 0,
       "failed to submit input dialog text"},
      {TUI_STEP_EXPECT, "INPUT CAPTURED", 0, 0, 0, PTY_TIMEOUT_MS,
       "Input Captured did not appear"},
      {TUI_STEP_EXPECT, "Hello, Ada Lovelace.", 0, 0, 0, 1000,
       "captured input message did not appear"},
      {TUI_STEP_SEND, "x", 0, 0, 0, 0,
       "failed to dismiss input captured message"},
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
       "menu did not reappear after input flow"},

      {TUI_STEP_SEND, "p", 0, 0, 0, 0, "failed to select Progress Pattern"},
      {TUI_STEP_EXPECT, "PROGRESS COMPLETE", 0, 0, 0, PTY_TIMEOUT_MS,
       "Progress Complete did not appear"},
      {TUI_STEP_EXPECT, "window lifecycle", 0, 0, 0, 1000,
       "progress completion body did not appear"},
      {TUI_STEP_SEND, "x", 0, 0, 0, 0, "failed to dismiss progress completion"},
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
       "menu did not reappear after progress"},

      {TUI_STEP_SEND, "l", 0, 0, 0, 0, "failed to select Layout Pattern"},
      {TUI_STEP_EXPECT, "LAYOUT PATTERN", 0, 0, 0, PTY_TIMEOUT_MS,
       "Layout Pattern did not appear"},
      {TUI_STEP_EXPECT, "Composable terminal UI", 0, 0, 0, 1000,
       "layout screen body was incomplete: Composable terminal UI"},
      {TUI_STEP_EXPECT, "Enter/Esc closes this panel", 0, 0, 0, 1000,
       "layout screen body was incomplete: close hint"},
      {TUI_STEP_SEND, "\x1b", 0, 0, 0, 0, "failed to close layout screen"},
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
       "menu did not reappear after layout"},

      {TUI_STEP_SEND, "c", 0, 0, 0, 0, "failed to select Configuration"},
      {TUI_STEP_EXPECT, "CONFIGURATION", 0, 0, 0, PTY_TIMEOUT_MS,
       "Configuration menu did not appear"},
      {TUI_STEP_EXPECT, "Output mode", 0, 0, 0, 1000,
       "Configuration item did not appear: Output mode"},
      {TUI_STEP_SEND, "\r", 0, 0, 0, 0, "failed to select Output mode"},
      {TUI_STEP_EXPECT, "OUTPUT MODE", 0, 0, 0, PTY_TIMEOUT_MS,
       "Output Mode message did not appear"},
      {TUI_STEP_EXPECT, "Set via --json", 0, 0, 0, 1000,
       "Output Mode body did not appear"},
      {TUI_STEP_SEND, "x", 0, 0, 0, 0, "failed to dismiss Output Mode message"},
      {TUI_STEP_EXPECT, "CONFIGURATION", 0, 0, 0, PTY_TIMEOUT_MS,
       "Configuration menu did not reappear after handler modal"},
      {TUI_STEP_SEND, "b", 0, 0, 0, 0, "failed to return from Configuration"},
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
       "menu did not reappear after configuration"},

      {TUI_STEP_RESIZE, NULL, 0, 100, 28, 0,
       "failed to resize PTY/Ghostty terminal"},
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
       "STARTER SHOWCASE disappeared after resize"},
      {TUI_STEP_SEND, "q", 0, 0, 0, 0, "failed to send q"},
      {TUI_STEP_EXPECT, "Return to the shell?", 0, 0, 0, PTY_TIMEOUT_MS,
       "exit confirmation did not appear"},
      {TUI_STEP_SEND, "n", 0, 0, 0, 0, "failed to cancel exit confirmation"},
      {TUI_STEP_EXPECT, "STARTER SHOWCASE", 0, 0, 0, PTY_TIMEOUT_MS,
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
  if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS,
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
  // then reject the contradictory request (mirroring `myapp menu` rejecting
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
  setenv("APP_CONFIG_PATH", config_path, 1);

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
  if (!failed && contains_text(snapshot, "STARTER SHOWCASE")) {
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
  unsetenv("APP_CONFIG_PATH");
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
  if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS,
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
    if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS,
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
  if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed &&
      !vt_expect_text(&session, "Progress Pattern", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  /* Selection starts on Overview. j j j should advance past the separator
   * to Progress Pattern (item 4). */
  if (!failed && !vt_send(&session, "jjj"))
    failed = test_fail(stats, name, "failed to send navigation");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to confirm");
  if (!failed &&
      !vt_expect_text(&session, "PROGRESS COMPLETE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "expected to land on Progress Pattern");
  if (!failed && !vt_send(&session, "x"))
    failed = test_fail(stats, name, "failed to dismiss dialog");
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
  if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS, &snapshot))
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
  if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS, &snapshot))
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
  if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS, &snapshot))
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
  if (!failed &&
      !vt_expect_text(&session, "Overview", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "menu items did not render after shrink");
  if (!failed && !vt_send(&session, "o"))
    failed = test_fail(stats, name, "failed to open overview handler");
  if (!failed &&
      !vt_expect_text(&session, "STARTER OVERVIEW", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "overview handler did not open");
  if (!failed && !vt_resize(&session, 100, 30))
    failed = test_fail(stats, name, "failed to grow during handler");
  if (!failed &&
      !vt_expect_text(&session, "STARTER OVERVIEW", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "overview disappeared after grow");
  if (!failed && !vt_send(&session, "x"))
    failed = test_fail(stats, name, "failed to dismiss overview");
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
  if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed && !vt_expect_text(&session, "System Information", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  /* "&System Information" has unique mnemonic 's' - auto-confirms. */
  if (!failed && !vt_send(&session, "s"))
    failed = test_fail(stats, name, "failed to send 's'");
  if (!failed && !vt_expect_text(&session, "SYSTEM INFORMATION", PTY_TIMEOUT_MS,
                                 &snapshot))
    failed = test_fail(stats, name, "System Information dialog did not appear");
  if (!failed && !vt_expect_text(&session, "Application:", 1000, &snapshot))
    failed = test_fail(stats, name, "Application metadata missing");
  if (!failed && !vt_send(&session, "x"))
    failed = test_fail(stats, name, "failed to dismiss dialog");
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
  if (!vt_expect_text(&session, "STARTER SHOWCASE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "initial menu did not render");
  if (!failed &&
      !vt_expect_text(&session, "Progress Pattern", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "menu items did not finish rendering");
  if (!failed && !vt_send(&session, "/"))
    failed = test_fail(stats, name, "failed to enter search mode");
  if (!failed && !vt_expect_text(&session, "find:", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "search prompt did not appear");
  if (!failed && !vt_send(&session, "prog"))
    failed = test_fail(stats, name, "failed to type 'prog'");
  if (!failed &&
      !vt_expect_text(&session, "Progress Pattern", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "Progress Pattern not filtered in");
  if (!failed && !vt_send(&session, "\r"))
    failed = test_fail(stats, name, "failed to confirm");
  if (!failed &&
      !vt_expect_text(&session, "PROGRESS COMPLETE", PTY_TIMEOUT_MS, &snapshot))
    failed = test_fail(stats, name, "Progress dialog did not appear");
  if (!failed && !vt_send(&session, "x"))
    failed = test_fail(stats, name, "failed to dismiss progress");
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
