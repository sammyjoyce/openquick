/*
 * Unit tests for OSC 11 background detection: the pure response parser, a
 * self-contained PTY round-trip (a forked child runs the query on the slave fd
 * while the parent answers on the master fd), and the detection POLICY (env
 * gates and the caching contract). The PTY test uses posix_openpt so it needs
 * no -lutil and runs on Linux and macOS.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cli/style/cli_term.h"        // detection policy surface
#include "../src/cli/style/cli_term_osc11.h"  // low-level parse/query I/O
#include "../src/core/config.h"
#include "../src/style/color_math.h"
#include "unit_support.h"

static bool parse_to_rgb(const char *resp, app_rgb_t *rgb) {
  return app_cli_osc11_parse(resp, strlen(resp), rgb);
}

static bool test_parse_dark_st(void) {
  app_rgb_t rgb;
  bool ok = parse_to_rgb("\x1b]11;rgb:0000/0000/0000\x1b\\", &rgb);
  return ok && rgb.r == 0 && rgb.g == 0 && rgb.b == 0 &&
         !app_color_is_light(rgb);
}

static bool test_parse_light_bel(void) {
  app_rgb_t rgb;
  bool ok = parse_to_rgb("\x1b]11;rgb:ffff/ffff/ffff\a", &rgb);
  return ok && rgb.r == 255 && rgb.g == 255 && rgb.b == 255 &&
         app_color_is_light(rgb);
}

static bool test_parse_rgba_and_short_digits(void) {
  app_rgb_t rgb;
  // rgba: variant with 2-digit channels (max 0xff).
  bool ok = parse_to_rgb("\x1b]11;rgba:80/80/80/ff\x1b\\", &rgb);
  return ok && rgb.r == 128 && rgb.g == 128 && rgb.b == 128;
}

static bool test_parse_rejects_garbage(void) {
  app_rgb_t rgb;
  return !parse_to_rgb("not a response", &rgb) &&
         !parse_to_rgb("rgb:ffff/ffff/ffff\a", &rgb) &&
         !parse_to_rgb("\x1b]10;rgb:ffff/ffff/ffff\a", &rgb) &&
         !parse_to_rgb("\x1b]11;rgb:zz/00/00\x1b\\", &rgb) &&
         !parse_to_rgb("\x1b]11;rgb:00\x1b\\", &rgb) &&
         !parse_to_rgb("\x1b]11;rgb:00/00/00", &rgb);
}

#if defined(__APPLE__) || defined(__linux__)

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

// Run the real query_fd round-trip across a PTY, with the parent answering
// `response`. Returns the RGB the child parsed.
static bool osc11_pty_roundtrip(const char *response, app_rgb_t *out) {
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
    if (master >= 0) {
      close(master);
    }
    return false;
  }
  char *slave_name = ptsname(master);
  if (!slave_name) {
    close(master);
    return false;
  }
  int slave = open(slave_name, O_RDWR | O_NOCTTY);
  if (slave < 0) {
    close(master);
    return false;
  }
  int result_pipe[2];
  if (pipe(result_pipe) != 0) {
    close(master);
    close(slave);
    return false;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(master);
    close(slave);
    close(result_pipe[0]);
    close(result_pipe[1]);
    return false;
  }

  if (pid == 0) {
    // Child: the "program" querying its terminal (the slave).
    close(master);
    close(result_pipe[0]);
    app_rgb_t rgb = {0, 0, 0};
    bool ok = app_cli_osc11_query_fd(slave, 500, &rgb);
    unsigned char msg[4] = {(unsigned char)(ok ? 1 : 0), rgb.r, rgb.g, rgb.b};
    ssize_t w = write(result_pipe[1], msg, sizeof(msg));
    (void)w;
    _exit(0);
  }

  // Parent: the "terminal emulator" on the master side.
  close(slave);
  close(result_pipe[1]);

  struct pollfd pfd = {.fd = master, .events = POLLIN};
  if (poll(&pfd, 1, 500) > 0 && (pfd.revents & POLLIN)) {
    char query[64];
    ssize_t r = read(master, query, sizeof(query));
    (void)r;  // drain the child's query
  }
  ssize_t w = write(master, response, strlen(response));
  (void)w;

  unsigned char msg[4] = {0, 0, 0, 0};
  ssize_t n = read(result_pipe[0], msg, sizeof(msg));
  int status = 0;
  waitpid(pid, &status, 0);
  close(master);
  close(result_pipe[0]);

  if (n != (ssize_t)sizeof(msg) || msg[0] != 1) {
    return false;
  }
  out->r = msg[1];
  out->g = msg[2];
  out->b = msg[3];
  return true;
}

static bool test_pty_roundtrip_dark(void) {
  app_rgb_t rgb;
  bool ok = osc11_pty_roundtrip("\x1b]11;rgb:0000/0000/0000\x1b\\", &rgb);
  return ok && !app_color_is_light(rgb);
}

static bool test_pty_roundtrip_light(void) {
  app_rgb_t rgb;
  bool ok = osc11_pty_roundtrip("\x1b]11;rgb:ffff/ffff/ffff\x1b\\", &rgb);
  return ok && app_color_is_light(rgb);
}

#endif /* PTY-capable platforms */

// ---- Detection POLICY: env gates + caching contract ----------------------
//
// app_cli_term_detect_background caches a process-global result, but ONLY after
// a completed probe; contextual skips must not freeze the cache. The cache is a
// function-local static, so these checks share one ordered sequence: every skip
// assertion runs BEFORE the single committing probe, and the final assertions
// confirm the frozen cache. Determinism comes from the APP_CLI_TEST_BG hook,
// which stands in for the /dev/tty round-trip after every skip gate, so no
// controlling terminal is required.

static char *copy_env(const char *name) {
  const char *value = getenv(name);
  return value ? strdup(value) : NULL;
}

static void restore_env(const char *name, char *saved) {
  if (saved) {
    setenv(name, saved, 1);
    free(saved);
  } else {
    unsetenv(name);
  }
}

// Build a config with NO_COLOR forced on (a hard-disable => contextual skip).
static app_config_t *make_no_color_config(void) {
  app_config_t *config = NULL;
  if (app_config_create(&config) != APP_SUCCESS) {
    return NULL;
  }
  if (app_config_set_no_color(config, true) != APP_SUCCESS) {
    app_config_destroy(config);
    return NULL;
  }
  return config;
}

static bool run_detection_policy_contract(void) {
  char *saved_osc = copy_env("APP_CLI_OSC11");
  char *saved_ci = copy_env("CI");
  char *saved_bg = copy_env("APP_CLI_TEST_BG");
  char *saved_nc = copy_env("NO_COLOR");
  char *saved_color = copy_env("APP_CLI_COLOR");
  char *saved_term = copy_env("TERM");

  // Clean slate for the env gates we drive explicitly.
  unsetenv("APP_CLI_OSC11");
  unsetenv("CI");
  unsetenv("NO_COLOR");
  unsetenv("APP_CLI_COLOR");
  setenv("TERM", "xterm-256color", 1);

  app_config_t *enabled = NULL;
  app_config_t *disabled = make_no_color_config();
  bool ok = disabled != NULL && app_config_create(&enabled) == APP_SUCCESS;

  // Throughout the skip phase, a successful "probe" is *available* via the test
  // hook; if any skip wrongly committed the cache, a later call would return
  // this LIGHT instead of re-evaluating.
  setenv("APP_CLI_TEST_BG", "light", 1);

  // (a) Hard-disabled config (NO_COLOR) => UNKNOWN, must not freeze the cache.
  ok = ok &&
       app_cli_term_detect_background(NULL, disabled) == APP_CLI_BG_UNKNOWN;

  // (b) Env precedence: APP_CLI_OSC11=0 overrides an otherwise-enabled config
  //     and the available probe => UNKNOWN, still no commit.
  setenv("APP_CLI_OSC11", "0", 1);
  ok =
      ok && app_cli_term_detect_background(NULL, enabled) == APP_CLI_BG_UNKNOWN;

  // (c) Env precedence: CI set (without APP_CLI_OSC11=1) => skip => UNKNOWN.
  unsetenv("APP_CLI_OSC11");
  setenv("CI", "1", 1);
  ok =
      ok && app_cli_term_detect_background(NULL, enabled) == APP_CLI_BG_UNKNOWN;

  // (d) The cache was NOT poisoned by the three skips above: now that the gates
  //     pass, the (hooked) probe runs and returns the real result. This is the
  //     core of the fixed bug — a prior skip would otherwise have frozen
  //     UNKNOWN.
  unsetenv("CI");
  ok = ok && app_cli_term_detect_background(NULL, enabled) == APP_CLI_BG_LIGHT;

  // (e) The probe committed: subsequent calls are served from the cache even
  //     under conditions that would otherwise skip (NO_COLOR /
  //     APP_CLI_OSC11=0).
  setenv("APP_CLI_TEST_BG", "dark", 1);  // would change result if re-probed
  ok = ok && app_cli_term_detect_background(NULL, enabled) == APP_CLI_BG_LIGHT;
  ok = ok && app_cli_term_detect_background(NULL, disabled) == APP_CLI_BG_LIGHT;

  app_config_destroy(enabled);
  app_config_destroy(disabled);

  restore_env("APP_CLI_OSC11", saved_osc);
  restore_env("CI", saved_ci);
  restore_env("APP_CLI_TEST_BG", saved_bg);
  restore_env("NO_COLOR", saved_nc);
  restore_env("APP_CLI_COLOR", saved_color);
  restore_env("TERM", saved_term);
  return ok;
}

void run_cli_osc11_unit_tests(unit_stats_t *stats) {
  unit_record(stats, test_parse_dark_st(), "osc11 parse dark (ESC-backslash)");
  unit_record(stats, test_parse_light_bel(), "osc11 parse light (BEL)");
  unit_record(stats, test_parse_rgba_and_short_digits(),
              "osc11 parse rgba + short digits");
  unit_record(stats, test_parse_rejects_garbage(), "osc11 rejects garbage");
#if defined(__APPLE__) || defined(__linux__)
  unit_record(stats, test_pty_roundtrip_dark(),
              "osc11 PTY round-trip detects dark");
  unit_record(stats, test_pty_roundtrip_light(),
              "osc11 PTY round-trip detects light");
#endif
  unit_record(stats, run_detection_policy_contract(),
              "osc11 detect_background: skips do not poison cache; env "
              "precedence and probe caching honored");
}
