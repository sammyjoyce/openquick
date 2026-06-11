/*
 * Backend-agnostic terminal policy: detection, profile resolution, width, and
 * the public emit_* entry points. See cli_term.h.
 */

#include "cli_term.h"

#include <stdlib.h>
#include <string.h>

#include "../../utils/colors.h"
#include "cli_term_internal.h"
#include "cli_term_osc11.h"

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

static int app_cli_stream_fd(FILE *stream) {
#ifdef _WIN32
  return _fileno(stream);
#else
  return fileno(stream);
#endif
}

static bool app_cli_fd_is_tty(int fd) {
#ifdef _WIN32
  return _isatty(fd) != 0;
#else
  return isatty(fd) != 0;
#endif
}

static size_t app_cli_detect_width(int fd) {
#ifndef _WIN32
  struct winsize ws;
  if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
    return (size_t)ws.ws_col;
  }
#else
  (void)fd;
#endif
  const char *cols = getenv("COLUMNS");
  if (cols && *cols) {
    long n = strtol(cols, NULL, 10);
    if (n > 0) {
      return (size_t)n;
    }
  }
  return 0;
}

// Does the environment advertise 24-bit color?
static bool app_cli_env_truecolor(void) {
  const char *ct = getenv("COLORTERM");
  if (ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"))) {
    return true;
  }
  const char *term = getenv("TERM");
  if (term && (strstr(term, "truecolor") || strstr(term, "direct"))) {
    return true;
  }
  return false;
}

static app_cli_color_profile_id app_cli_parse_profile(const char *s) {
  if (!s) {
    return APP_CLI_COLOR_PROFILE_NONE;
  }
  if (strcmp(s, "truecolor") == 0) {
    return APP_CLI_COLOR_PROFILE_TRUECOLOR;
  }
  if (strcmp(s, "256") == 0) {
    return APP_CLI_COLOR_PROFILE_ANSI256;
  }
  if (strcmp(s, "16") == 0) {
    return APP_CLI_COLOR_PROFILE_ANSI16;
  }
  return APP_CLI_COLOR_PROFILE_NONE;
}

// Hard policy that disables styling regardless of any forced test profile:
// explicit config flags, NO_COLOR, or a dumb terminal. These always win so a
// user who sets NO_COLOR never sees escapes.
static bool app_cli_hard_disabled(const app_config_t *config) {
  if (config &&
      (app_config_is_plain_output(config) || app_config_is_no_color(config) ||
       app_config_is_json_output(config))) {
    return true;
  }
  if (app_flag_env_enabled(APP_FLAG_NO_COLOR)) {
    return true;
  }
  // FORCE_COLOR=0 / CLICOLOR=0 are explicit "disable color" signals; honor them
  // as a hard disable so they win over a forced test profile, like NO_COLOR.
  if (app_color_env_force() == APP_COLOR_FORCE_OFF) {
    return true;
  }
  const char *color = getenv("APP_CLI_COLOR");
  if (color && strcmp(color, "never") == 0) {
    return true;
  }
  const char *term = getenv("TERM");
  if (term && strcmp(term, "dumb") == 0) {
    return true;
  }
  return false;
}

bool app_cli_term_init(app_cli_term_t *term, FILE *stream,
                       const app_config_t *config,
                       const app_cli_term_opts_t *opts) {
  if (!term) {
    return false;
  }
  *term = (app_cli_term_t){0};
  term->stream = stream ? stream : stdout;
  term->fd = app_cli_stream_fd(term->stream);
  term->is_tty = app_cli_fd_is_tty(term->fd);
  term->profile = APP_CLI_COLOR_PROFILE_NONE;

  // Forced profile precedence: explicit opts (tests) > APP_CLI_TEST_PROFILE
  // (tests) > APP_CLI_COLOR (public). APP_CLI_COLOR=auto means "no force".
  const char *force_profile = opts && opts->force_profile
                                  ? opts->force_profile
                                  : getenv("APP_CLI_TEST_PROFILE");
  if (!force_profile) {
    // "never" is handled as a hard disable above; here only 16/256/truecolor
    // force a specific profile ("auto"/unset leaves detection to decide).
    const char *color = getenv("APP_CLI_COLOR");
    if (color && color[0] != '\0' && strcmp(color, "auto") != 0 &&
        strcmp(color, "never") != 0) {
      force_profile = color;
    }
  }

  // Width: explicit override, then env test hooks, then live detection.
  size_t width = opts ? opts->force_width : 0;
  if (width == 0) {
    const char *env_w = getenv("APP_CLI_TEST_WIDTH");
    if (!env_w) {
      env_w = getenv("__FANG_TEST_WIDTH");
    }
    if (env_w && *env_w) {
      long n = strtol(env_w, NULL, 10);
      if (n > 0) {
        width = (size_t)n;
      }
    }
  }
  if (width == 0) {
    width = app_cli_detect_width(term->fd);
  }
  term->width = width;

  bool style;
  if (app_cli_hard_disabled(config)) {
    // NO_COLOR / --plain / --no-color / --json / TERM=dumb always win.
    style = false;
  } else if (force_profile) {
    // A forced profile of "none" disables styling; any other forced profile
    // enables emission even off a TTY so golden tests can exercise it.
    style = app_cli_parse_profile(force_profile) != APP_CLI_COLOR_PROFILE_NONE;
  } else {
    // A force-on env (FORCE_COLOR / CLICOLOR_FORCE) enables emission even off a
    // TTY; force-off was already caught by the hard-disable above.
    style = app_color_env_force() == APP_COLOR_FORCE_ON ||
            app_cli_fd_is_tty(term->fd);
  }

  if (!style) {
    term->style_enabled = false;
    return false;
  }

  app_cli_backend_probe(term);

  // Resolve the color profile.
  app_cli_color_profile_id profile;
  if (force_profile) {
    profile = app_cli_parse_profile(force_profile);
  } else if (app_cli_env_truecolor()) {
    profile = APP_CLI_COLOR_PROFILE_TRUECOLOR;
  } else if (term->color_count >= 256) {
    profile = APP_CLI_COLOR_PROFILE_ANSI256;
  } else if (term->color_count >= 8) {
    profile = APP_CLI_COLOR_PROFILE_ANSI16;
  } else {
    profile = APP_CLI_COLOR_PROFILE_NONE;
  }

  term->profile = profile;
  term->style_enabled = profile != APP_CLI_COLOR_PROFILE_NONE;
  if (!term->style_enabled) {
    app_cli_backend_release(term);
  }
  return term->style_enabled;
}

void app_cli_term_deinit(app_cli_term_t *term) {
  if (!term) {
    return;
  }
  app_cli_backend_release(term);
  *term = (app_cli_term_t){0};
}

void app_cli_term_raw(app_cli_term_t *term, const char *s, size_t n) {
  if (!term || !term->stream || !s || n == 0) {
    return;
  }
  fwrite(s, 1, n, term->stream);
}

void app_cli_term_write(app_cli_term_t *term, const char *s, size_t n) {
  app_cli_term_raw(term, s, n);
}

void app_cli_term_puts(app_cli_term_t *term, const char *s) {
  if (s) {
    app_cli_term_raw(term, s, strlen(s));
  }
}

void app_cli_term_emit_attr(app_cli_term_t *term, app_cli_attr_bit attr) {
  if (!term || !term->style_enabled) {
    return;
  }
  app_cli_backend_emit_attr(term, attr);
}

void app_cli_term_emit_indexed(app_cli_term_t *term, bool background,
                               uint16_t index) {
  if (!term || !term->style_enabled) {
    return;
  }
  app_cli_backend_emit_indexed(term, background, index);
}

void app_cli_term_emit_truecolor(app_cli_term_t *term, bool background,
                                 app_rgb_t rgb) {
  if (!term || !term->style_enabled) {
    return;
  }
  // 24-bit SGR is backend-independent: no portable terminfo cap expresses it.
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "\033[%d;2;%u;%u;%um",
                   background ? 48 : 38, rgb.r, rgb.g, rgb.b);
  if (n > 0) {
    app_cli_term_raw(term, buf, (size_t)n);
  }
}

void app_cli_term_emit_reset(app_cli_term_t *term) {
  if (!term || !term->style_enabled) {
    return;
  }
  app_cli_backend_emit_reset(term);
}

// ---- Background detection (OSC 11) ---------------------------------------

// TTY model: we query and gate on the *controlling terminal* (/dev/tty), not on
// the render stream's fd. The OSC 11 round-trip both writes to and reads from
// /dev/tty, so gating on the render fd (term->is_tty) would wrongly skip a
// piped stdout that still has an interactive controlling terminal. The `term`
// argument is kept for API symmetry but its fd is intentionally not the probe
// target.
//
// Caching is honest: we commit a process-global result ONLY after a real probe
// completes (a definitive success or failure of the /dev/tty round-trip). A
// transient or contextual skip (NO_COLOR/plain config, APP_CLI_OSC11=0, CI, no
// controlling terminal) returns UNKNOWN WITHOUT freezing the cache, so a later
// call made under different conditions can still probe.
app_cli_bg_kind_id app_cli_term_detect_background(const app_cli_term_t *term,
                                                  const app_config_t *config) {
  (void)term;
  static bool probed = false;
  static app_cli_bg_kind_id cached = APP_CLI_BG_UNKNOWN;
  if (probed) {
    return cached;
  }

  if (app_cli_hard_disabled(config)) {
    return APP_CLI_BG_UNKNOWN;
  }
  const char *osc = getenv("APP_CLI_OSC11");
  if (osc && strcmp(osc, "0") == 0) {
    return APP_CLI_BG_UNKNOWN;
  }
  // CI environments rarely have a responsive terminal; skip unless asked.
  if (getenv("CI") != NULL && !(osc && strcmp(osc, "1") == 0)) {
    return APP_CLI_BG_UNKNOWN;
  }

  // Test hook: stand in for the /dev/tty round-trip with a deterministic
  // result so the caching contract can be exercised without a real terminal.
  // It sits AFTER every contextual gate above, so it models a completed probe:
  // it commits to the cache exactly as a real probe does (never reached when a
  // gate skips, proving skips do not freeze the cache).
  const char *test_bg = getenv("APP_CLI_TEST_BG");
  if (test_bg) {
    if (strcmp(test_bg, "light") == 0) {
      cached = APP_CLI_BG_LIGHT;
    } else if (strcmp(test_bg, "dark") == 0) {
      cached = APP_CLI_BG_DARK;
    }
    probed = true;
    return cached;
  }

#ifndef _WIN32
  // Gate on the same fd we query: the controlling terminal. If there is no
  // usable /dev/tty this is a contextual skip, not a probe result.
  int tty = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (tty < 0) {
    return APP_CLI_BG_UNKNOWN;
  }
  // A real probe runs now: commit its outcome (success or failure) to the
  // cache.
  app_rgb_t bg;
  if (app_cli_osc11_query_fd(tty, 100, &bg)) {
    cached = app_color_is_light(bg) ? APP_CLI_BG_LIGHT : APP_CLI_BG_DARK;
  }
  close(tty);
  probed = true;
#endif
  return cached;
}
