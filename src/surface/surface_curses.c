/*
 * cs_surface — curses backend. See surface.h.
 *
 * Linked only into TUI builds (ENABLE_TUI). All ncurses contact is isolated
 * here; surface.c reaches these functions solely through the ops vtable, so a
 * CLI-only or unit-test build never references a curses symbol.
 *
 * Color: a foreground role is resolved through the caller's cs_theme — the same
 * shared resolver the stream backend uses — and bound to a dedicated pair drawn
 * from a block reserved above the TUI's own pairs, so a per-surface theme (e.g.
 * "mono") renders the same on curses as on a stream. When the theme doesn't
 * resolve to a palette index, curses has no free pair, or a background role is
 * set (curses pairs fix fg+bg together), we fall back to the nearest pre-seeded
 * TUI_COLOR_* pair from tui_init_colors.
 */

#include "surface_internal.h"

#ifdef ENABLE_TUI

#include "../tui/tui.h"

static tui_color_pair_t cs_role_to_pair(cs_role_t role) {
  switch (role) {
  case APP_UI_ROLE_TITLE:
  case APP_UI_ROLE_PROGRAM:
  case APP_UI_ROLE_COMMAND:
  case APP_UI_ROLE_FLAG:
  case APP_UI_ROLE_HELP:
    return TUI_COLOR_TITLE;
  case APP_UI_ROLE_ACCENT:
  case APP_UI_ROLE_CODE:
    return TUI_COLOR_ACCENT;
  case APP_UI_ROLE_MUTED:
  case APP_UI_ROLE_COMMENT:
  case APP_UI_ROLE_FLAG_DEFAULT:
  case APP_UI_ROLE_DASH:
    return TUI_COLOR_DIM;
  case APP_UI_ROLE_BORDER:
    return TUI_COLOR_BORDER;
  case APP_UI_ROLE_SUCCESS:
  case APP_UI_ROLE_QUOTED_STRING:
    return TUI_COLOR_SUCCESS;
  case APP_UI_ROLE_WARNING:
    return TUI_COLOR_WARNING;
  case APP_UI_ROLE_INFO:
    return TUI_COLOR_INFO;
  case APP_UI_ROLE_ERROR_DETAILS:
  case APP_UI_ROLE_ERROR_HEADER_FG:
  case APP_UI_ROLE_ERROR_HEADER_BG:
    return TUI_COLOR_ERROR;
  case APP_UI_ROLE_SELECTION_FG:
  case APP_UI_ROLE_SELECTION_BG:
    return TUI_COLOR_HIGHLIGHT;
  case APP_UI_ROLE_TEXT:
  case APP_UI_ROLE_DESCRIPTION:
  case APP_UI_ROLE_ARGUMENT:
  default:
    return TUI_COLOR_MENU_NORMAL;
  }
}

static WINDOW *curses_win(cs_surface_t *s) {
  return s->window ? s->window->win : NULL;
}

// Bind a reserved pair to the theme-resolved foreground for `role` and return
// it, or -1 when the theme resolves to no palette index (e.g. terminal-default)
// or curses has no free pair. The pair block lives above TUI_COLOR_MAX so it
// never collides with the TUI's own pairs; we re-bind on each call (cheap) to
// stay correct across theme changes and tui re-init without per-pair state.
static short cs_curses_role_pair(cs_surface_t *s, cs_role_t role) {
  app_ui_resolved_color_t r =
      cs_theme_resolve(&s->theme, role, s->caps.profile, s->caps.color_count);
  if (r.kind != APP_UI_RESOLVED_INDEXED) {
    return -1;
  }
  short pair = (short)(TUI_COLOR_MAX + (int)role);
  if (pair >= COLOR_PAIRS) {
    return -1;
  }
  if (init_pair(pair, (short)r.index, tui_default_bg()) == ERR) {
    return -1;
  }
  return pair;
}

static void curses_set_color(cs_surface_t *s, cs_role_t role, bool bg) {
  WINDOW *w = curses_win(s);
  if (!w || !tui_colors_enabled()) {
    return;
  }
  // Clear any current color pair before applying the new one. wattron ORs the
  // pair bits, so two set_role calls before a reset (which the surface API
  // documents as legal — "additive until reset") would otherwise combine into
  // a corrupted third pair index. A_COLOR is just the color field, so this
  // leaves bold/dim/underline intact.
  wattroff(w, A_COLOR);

  // Foreground roles resolve through the caller's theme so a custom theme looks
  // the same here as on the stream backend. A background role keeps the static
  // fg+bg TUI pair (which already carries the surrounding background), as does
  // any case where the themed pair is unavailable.
  if (!bg) {
    short pair = cs_curses_role_pair(s, role);
    if (pair > 0) {
      wattron(w, COLOR_PAIR(pair));
      return;
    }
  }
  wattron(w, COLOR_PAIR(cs_role_to_pair(role)));
}

static void curses_set_attr(cs_surface_t *s, cs_attr_t attrs) {
  WINDOW *w = curses_win(s);
  if (!w) {
    return;
  }
  if (attrs & CS_ATTR_BOLD) {
    wattron(w, A_BOLD);
  }
  if (attrs & CS_ATTR_DIM) {
    wattron(w, A_DIM);
  }
  if (attrs & CS_ATTR_UNDERLINE) {
    wattron(w, A_UNDERLINE);
  }
#ifdef A_ITALIC
  if (attrs & CS_ATTR_ITALIC) {
    wattron(w, A_ITALIC);
  }
#endif
}

static void curses_reset(cs_surface_t *s) {
  WINDOW *w = curses_win(s);
  if (w) {
    wattrset(w, A_NORMAL);
  }
}

static void curses_write_n(cs_surface_t *s, const char *text, size_t n) {
  WINDOW *w = curses_win(s);
  if (w && text && n) {
    waddnstr(w, text, (int)n);
  }
}

static void curses_newline(cs_surface_t *s) {
  WINDOW *w = curses_win(s);
  if (!w) {
    return;
  }
  int y, x;
  getyx(w, y, x);
  (void)x;
  wmove(w, y + 1, 0);
}

static void curses_move(cs_surface_t *s, int x, int y) {
  WINDOW *w = curses_win(s);
  if (w) {
    wmove(w, y, x);
  }
}

static void curses_destroy(cs_surface_t *s) {
  (void)s;  // the window is owned by the caller
}

static const cs_surface_ops_t cs_curses_ops = {
    .set_color = curses_set_color,
    .set_attr = curses_set_attr,
    .reset = curses_reset,
    .write_n = curses_write_n,
    .newline = curses_newline,
    .move = curses_move,
    .destroy = curses_destroy,
};

cs_surface_t *cs_surface_curses_new(tui_window_t *window,
                                    const cs_theme_t *theme) {
  cs_surface_t *s = cs_surface_alloc_(theme);
  if (!s) {
    return NULL;
  }
  s->ops = &cs_curses_ops;
  s->window = window;
  WINDOW *w = window ? window->win : NULL;
  int cols = w ? getmaxx(w) : 0;
  s->caps = (cs_caps_t){
      .tty = true,
      .color = tui_colors_enabled(),
      .unicode = cs_surface_unicode_enabled_(),
      .interactive = true,
      .profile = (COLORS >= 256) ? APP_CLI_COLOR_PROFILE_ANSI256
                                 : APP_CLI_COLOR_PROFILE_ANSI16,
      .color_count = COLORS,
      .width = (cols > 0) ? (size_t)cols : 80,
  };
  return s;
}

#endif  // ENABLE_TUI
