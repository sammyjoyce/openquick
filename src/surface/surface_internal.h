/*
 * Private surface internals shared by the backends (surface.c stream backend
 * and surface_curses.c curses backend). Not a public header.
 *
 * The backends are decoupled through a small ops vtable so surface.c never
 * names a curses symbol: the curses backend lives in its own translation unit
 * that is only linked into TUI builds. This mirrors the cli_term terminfo/ANSI
 * backend split.
 *
 * Every translation unit that includes this header within a single build target
 * is compiled with the same feature macros, so the struct layout is consistent;
 * the APP_ENABLE_CLI_STYLE-gated field is therefore safe.
 */

#pragma once

#include "surface.h"

#ifdef APP_ENABLE_CLI_STYLE
#include "../cli/style/cli_term.h"
#endif

typedef struct cs_surface_ops {
  void (*set_color)(cs_surface_t *s, cs_role_t role, bool bg);
  void (*set_attr)(cs_surface_t *s, cs_attr_t attrs);
  void (*reset)(cs_surface_t *s);
  void (*write_n)(cs_surface_t *s, const char *text, size_t n);
  void (*newline)(cs_surface_t *s);
  void (*move)(cs_surface_t *s, int x, int y);
  void (*destroy)(cs_surface_t *s);  // backend teardown before free()
} cs_surface_ops_t;

struct cs_surface {
  const cs_surface_ops_t *ops;
  cs_theme_t theme;
  cs_caps_t caps;

  FILE *stream_fp;  // stream backend
#ifdef APP_ENABLE_CLI_STYLE
  app_cli_term_t term;
  bool have_term;
#endif

  tui_window_t *window;  // curses backend
};

// Allocate a zeroed surface with the theme copied in. Returns NULL on OOM.
cs_surface_t *cs_surface_alloc_(const cs_theme_t *theme);

// Locale/environment capability shared by stream and curses surfaces. Returns
// true only when the active locale advertises UTF-8, so glyph-using components
// can fall back to ASCII on legacy terminals.
bool cs_surface_unicode_enabled_(void);
