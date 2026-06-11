/*
 * ANSI fallback backend: used when terminfo is unavailable (APP_HAVE_TERMINFO
 * unset/0). Emits standard SGR sequences directly and derives color_count from
 * the environment. Less precise than terminfo for exotic terminals, but covers
 * the common xterm-family case and keeps the styling layer functional in builds
 * that do not link ncurses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../utils/colors.h"
#include "cli_term_internal.h"

void app_cli_backend_probe(app_cli_term_t *term) {
  if (!term) {
    return;
  }
  int colors = 0;
  const char *term_env = getenv("TERM");
  if (getenv("COLORTERM")) {
    colors = 256;
  } else if (term_env) {
    if (strstr(term_env, "256color") || strstr(term_env, "direct")) {
      colors = 256;
    } else if (strstr(term_env, "color") || strstr(term_env, "xterm") ||
               strstr(term_env, "screen") || strstr(term_env, "tmux") ||
               strstr(term_env, "vt100") || strstr(term_env, "linux") ||
               strstr(term_env, "ansi")) {
      colors = 16;
    }
  }
  // A force-on env with no TERM color hint still needs a usable color count so
  // a profile resolves; FORCE_COLOR=0 must not enable color here.
  if (colors == 0 && app_color_env_force() == APP_COLOR_FORCE_ON) {
    colors = 16;
  }
  term->color_count = colors;

  // The xterm-family terminals this fallback targets support these attributes.
  term->supports_bold = colors > 0;
  term->supports_dim = colors > 0;
  term->supports_underline = colors > 0;
  term->supports_italic = colors > 0;
}

void app_cli_backend_release(app_cli_term_t *term) {
  (void)term;
}

void app_cli_backend_emit_attr(app_cli_term_t *term, app_cli_attr_bit attr) {
  const char *seq = nullptr;
  switch (attr) {
  case APP_CLI_ATTR_BOLD:
    seq = "\033[1m";
    break;
  case APP_CLI_ATTR_DIM:
    seq = "\033[2m";
    break;
  case APP_CLI_ATTR_UNDERLINE:
    seq = "\033[4m";
    break;
  case APP_CLI_ATTR_ITALIC:
    seq = "\033[3m";
    break;
  case APP_CLI_ATTR_NONE:
  default:
    return;
  }
  app_cli_term_raw(term, seq, strlen(seq));
}

void app_cli_backend_emit_indexed(app_cli_term_t *term, bool background,
                                  uint16_t index) {
  char buf[24];
  int n;
  if (term->profile == APP_CLI_COLOR_PROFILE_ANSI256) {
    n = snprintf(buf, sizeof(buf), "\033[%d;5;%um", background ? 48 : 38,
                 index);
  } else {
    // ANSI16: 30-37 / 40-47 for the base 8, 90-97 / 100-107 for bright.
    int base;
    if (index < 8) {
      base = (background ? 40 : 30) + (int)index;
    } else {
      base = (background ? 100 : 90) + (int)index - 8;
    }
    n = snprintf(buf, sizeof(buf), "\033[%dm", base);
  }
  if (n > 0) {
    app_cli_term_raw(term, buf, (size_t)n);
  }
}

void app_cli_backend_emit_reset(app_cli_term_t *term) {
  app_cli_term_raw(term, "\033[0m", 4);
}
