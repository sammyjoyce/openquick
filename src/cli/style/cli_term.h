/*
 * CLI terminal capability model and escape emission.
 *
 * This is the boundary between the styling layer and the terminal. It detects
 * whether styling is appropriate (TTY, NO_COLOR, plain/json config, TERM),
 * resolves a color profile (truecolor/256/16/none), measures width, and emits
 * SGR sequences.
 *
 * Capability detection and indexed-color emission go through ncurses *terminfo*
 * (setupterm/tigetnum/tigetstr/tparm/tputs) when the terminfo backend is
 * compiled in (APP_HAVE_TERMINFO=1); otherwise a small env-based ANSI fallback
 * backend is used. Either way this code never enters curses screen mode.
 *
 * Truecolor (24-bit) has no portable terminfo capability, so it is always
 * emitted as raw SGR 38;2 / 48;2 and is only selected when the terminal
 * advertises truecolor support.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../../core/config.h"
#include "../../style/color_math.h"

// Text attributes. Stored as a mask on a compiled style.
typedef uint32_t app_cli_attr_mask_t;
typedef enum app_cli_attr_bit {
  APP_CLI_ATTR_NONE = 0,
  APP_CLI_ATTR_BOLD = 1u << 0,
  APP_CLI_ATTR_DIM = 1u << 1,
  APP_CLI_ATTR_UNDERLINE = 1u << 2,
  APP_CLI_ATTR_ITALIC = 1u << 3,
} app_cli_attr_bit;

// Caller-supplied detection hints/overrides (mostly for tests).
typedef struct app_cli_term_opts {
  bool is_error;              // styling stderr instead of stdout
  bool allow_ansi_fallback;   // permit ANSI emission when terminfo absent
  const char *force_profile;  // "none"|"16"|"256"|"truecolor" or NULL
  size_t force_width;         // 0 = auto-detect
} app_cli_term_opts_t;

typedef struct app_cli_term {
  FILE *stream;
  int fd;

  bool is_tty;
  bool style_enabled;  // false => emit_* are no-ops, callers print plain

  app_cli_color_profile_id profile;
  int color_count;  // from terminfo tigetnum("colors"), or env heuristic

  size_t width;  // detected terminal columns (unclamped); 0 if unknown

  bool supports_bold;
  bool supports_dim;
  bool supports_underline;
  bool supports_italic;

  // Backend-private capability handles (terminfo cap strings / TERMINAL*).
  // Stored unconditionally so the struct layout does not depend on the build.
  void *backend_term;
  const char *cap_setaf;
  const char *cap_setab;
  const char *cap_sgr0;
  const char *cap_bold;
  const char *cap_dim;
  const char *cap_smul;
  const char *cap_sitm;
} app_cli_term_t;

// Initialize a terminal for styled output on `stream`. Returns the resolved
// value of `style_enabled` (true if styling will be emitted). Always safe to
// call; on any uncertainty it degrades to plain output.
bool app_cli_term_init(app_cli_term_t *term, FILE *stream,
                       const app_config_t *config,
                       const app_cli_term_opts_t *opts);

void app_cli_term_deinit(app_cli_term_t *term);

// Raw passthrough writes (never styled). Safe with NULL term.
void app_cli_term_write(app_cli_term_t *term, const char *s, size_t n);
void app_cli_term_puts(app_cli_term_t *term, const char *s);

// Style emission. No-ops when !style_enabled.
void app_cli_term_emit_attr(app_cli_term_t *term, app_cli_attr_bit attr);
void app_cli_term_emit_indexed(app_cli_term_t *term, bool background,
                               uint16_t index);
void app_cli_term_emit_truecolor(app_cli_term_t *term, bool background,
                                 app_rgb_t rgb);
void app_cli_term_emit_reset(app_cli_term_t *term);

// ---- Background detection (OSC 11) ---------------------------------------

typedef enum app_cli_bg_kind_id {
  APP_CLI_BG_UNKNOWN = 0,
  APP_CLI_BG_DARK,
  APP_CLI_BG_LIGHT,
} app_cli_bg_kind_id;

// The low-level OSC 11 response parser (app_cli_osc11_parse) and query
// round-trip (app_cli_osc11_query_fd) are deliberately NOT re-exported here.
// They live in cli_term_osc11.h; only the .c files (and tests) that drive the
// raw I/O include that header directly.

// Detect the terminal background by querying the controlling terminal
// (/dev/tty), which is both gated and queried (the render stream's fd is not
// consulted, so a piped stdout with an interactive controlling terminal still
// detects). The probe result is cached per process only after a real /dev/tty
// round-trip; benign/contextual skips do not freeze the cache. Honors skip
// conditions (NO_COLOR, plain/json config, TERM=dumb, no usable /dev/tty, CI
// unless APP_CLI_OSC11=1, APP_CLI_OSC11=0). Returns APP_CLI_BG_UNKNOWN when
// detection is skipped or fails. APP_CLI_TEST_BG=light|dark is a test hook that
// stands in for the /dev/tty round-trip (it sits after every skip gate) so the
// caching contract can be exercised without a real terminal.
app_cli_bg_kind_id app_cli_term_detect_background(const app_cli_term_t *term,
                                                  const app_config_t *config);
