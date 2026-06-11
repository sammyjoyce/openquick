/*
 * Terminfo backend: capability probing and indexed-color/attribute emission via
 * the low-level terminfo API. Compiled only when APP_HAVE_TERMINFO=1.
 *
 * Uses setupterm() (which initializes terminfo WITHOUT entering curses screen
 * mode), tigetnum/tigetstr to read capabilities, tparm to instantiate
 * parameterized strings, and tputs to emit them with correct padding. It never
 * calls initscr()/start_color()/init_pair().
 */

#include <curses.h>
#include <stddef.h>
#include <term.h>

#include "cli_term_internal.h"

// tputs() emits through a callback that takes no context, so route it through a
// per-call static. CLI rendering is single-threaded and emits one stream at a
// time, so this is safe.
static FILE *g_tputs_stream = nullptr;

static int app_cli_tputs_putc(int ch) {
  if (g_tputs_stream) {
    return fputc(ch, g_tputs_stream);
  }
  return ch;
}

static void app_cli_emit_cap(app_cli_term_t *term, const char *cap) {
  if (!term || !cap) {
    return;
  }
  if (term->backend_term) {
    set_curterm((TERMINAL *)term->backend_term);
  }
  g_tputs_stream = term->stream;
  tputs(cap, 1, app_cli_tputs_putc);
  g_tputs_stream = nullptr;
}

// tigetstr returns (char *)-1 for an absent/cancelled capability and (char *)0
// for "not a string capability"; treat both as missing.
static const char *app_cli_str_cap(const char *name) {
  // macOS ncurses declares tigetstr(char *) (non-const); the cast keeps this
  // portable since the string is not modified. Linux ncursesw accepts char *.
  char *s = tigetstr((char *)name);
  if (s == (char *)-1 || s == nullptr) {
    return nullptr;
  }
  return s;
}

void app_cli_backend_probe(app_cli_term_t *term) {
  if (!term) {
    return;
  }
  int err = 0;
  if (setupterm(nullptr, term->fd, &err) != 0 || err <= 0) {
    // No usable terminfo entry: leave color_count at 0 so the policy layer
    // falls back to no styling.
    term->color_count = 0;
    return;
  }
  term->backend_term = (void *)cur_term;

  int colors = tigetnum("colors");
  term->color_count = colors > 0 ? colors : 0;

  term->cap_setaf = app_cli_str_cap("setaf");
  term->cap_setab = app_cli_str_cap("setab");
  term->cap_sgr0 = app_cli_str_cap("sgr0");
  term->cap_bold = app_cli_str_cap("bold");
  term->cap_dim = app_cli_str_cap("dim");
  term->cap_smul = app_cli_str_cap("smul");
  term->cap_sitm = app_cli_str_cap("sitm");

  term->supports_bold = term->cap_bold != nullptr;
  term->supports_dim = term->cap_dim != nullptr;
  term->supports_underline = term->cap_smul != nullptr;
  term->supports_italic = term->cap_sitm != nullptr;
}

void app_cli_backend_release(app_cli_term_t *term) {
  if (!term || !term->backend_term) {
    return;
  }
  del_curterm((TERMINAL *)term->backend_term);
  term->backend_term = nullptr;
}

void app_cli_backend_emit_attr(app_cli_term_t *term, app_cli_attr_bit attr) {
  switch (attr) {
  case APP_CLI_ATTR_BOLD:
    app_cli_emit_cap(term, term->cap_bold);
    break;
  case APP_CLI_ATTR_DIM:
    app_cli_emit_cap(term, term->cap_dim);
    break;
  case APP_CLI_ATTR_UNDERLINE:
    app_cli_emit_cap(term, term->cap_smul);
    break;
  case APP_CLI_ATTR_ITALIC:
    app_cli_emit_cap(term, term->cap_sitm);
    break;
  case APP_CLI_ATTR_NONE:
  default:
    break;
  }
}

void app_cli_backend_emit_indexed(app_cli_term_t *term, bool background,
                                  uint16_t index) {
  const char *cap = background ? term->cap_setab : term->cap_setaf;
  if (!cap) {
    return;
  }
  if (term->backend_term) {
    set_curterm((TERMINAL *)term->backend_term);
  }
  // tparm(char *) is non-const on macOS ncurses; cast for portability.
  char *seq = tparm((char *)cap, (int)index);
  if (seq) {
    app_cli_emit_cap(term, seq);
  }
}

void app_cli_backend_emit_reset(app_cli_term_t *term) {
  app_cli_emit_cap(term, term->cap_sgr0 ? term->cap_sgr0 : "\033[0m");
}
