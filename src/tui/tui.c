/*
 * Terminal UI (TUI) implementation using ncurses.
 *
 * Module map:
 *   - lifecycle, signal, and color setup
 *   - window allocation, drawing, and bounded text helpers
 *   - shared modal runner plus message/confirm/input dialogs
 */

#include "tui.h"

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../io/terminal.h"
#include "../style/cs_theme.h"
#include "../style/design_tokens.h"
#include "../style/ui_theme.h"
#include "../ui/text_layout.h"
#include "../utils/logging.h"
#include "tui_internal.h"

static bool tui_initialized = false;
static bool tui_default_colors = false;
// Color policy the app resolves from config/env via app_use_colors(): -1 unset
// (fall back to terminal capability alone), 0 forced off, 1 allowed. Set by
// tui_set_color_enabled() before tui_init().
static int tui_color_policy = -1;
// Whether color was actually activated this session (policy allowed AND the
// terminal supports it AND start_color() succeeded). Gates tui_set_color() so a
// policy-disabled run never applies uninitialized color pairs.
static bool tui_colors_active = false;
static volatile sig_atomic_t tui_interrupted_signal = 0;
static int tui_saved_cursor_state = 0;
static tui_window_t *tui_background_win = NULL;
static bool tui_color_env_disabled(void);
#define TUI_BACKGROUND_STACK_MAX 16
static tui_window_t *tui_background_stack[TUI_BACKGROUND_STACK_MAX];
static size_t tui_background_stack_depth = 0;

void tui_clear_background_window(void) {
  tui_background_win = NULL;
  for (size_t i = 0; i < tui_background_stack_depth; i++) {
    tui_background_stack[i] = NULL;
  }
  tui_background_stack_depth = 0;
}

tui_window_t *tui_get_background_window(void) {
  return tui_background_win;
}

void tui_push_background(tui_window_t *window) {
  if (tui_background_stack_depth < TUI_BACKGROUND_STACK_MAX) {
    tui_background_stack[tui_background_stack_depth++] = tui_background_win;
  } else {
    for (size_t i = 1; i < TUI_BACKGROUND_STACK_MAX; i++) {
      tui_background_stack[i - 1] = tui_background_stack[i];
    }
    tui_background_stack[TUI_BACKGROUND_STACK_MAX - 1] = tui_background_win;
    LOG_WARNING("TUI background stack overflow; oldest background dropped");
  }
  tui_background_win = window;
}

void tui_pop_background(void) {
  if (tui_background_stack_depth == 0) {
    tui_background_win = NULL;
    return;
  }
  tui_background_stack_depth--;
  tui_background_win = tui_background_stack[tui_background_stack_depth];
  tui_background_stack[tui_background_stack_depth] = NULL;
}

void tui_replace_background(tui_window_t *old_window,
                            tui_window_t *new_window) {
  if (tui_background_win == old_window) {
    tui_background_win = new_window;
  }
  for (size_t i = 0; i < tui_background_stack_depth; i++) {
    if (tui_background_stack[i] == old_window) {
      tui_background_stack[i] = new_window;
    }
  }
}

/* ---- signal handling ---------------------------------------------------- */

#ifndef _WIN32
static struct sigaction tui_previous_sigint;
static struct sigaction tui_previous_sigterm;
static bool tui_signal_handlers_installed = false;

static void tui_signal_handler(int signum) {
  const int saved_errno = errno;
  tui_interrupted_signal = signum;
  errno = saved_errno;
}
#endif

static app_error tui_install_signal_handlers(void) {
#ifndef _WIN32
  struct sigaction sa_int = {.sa_handler = tui_signal_handler};
  sigemptyset(&sa_int.sa_mask);
  /* Clear SA_RESTART so wgetch returns ERR and the menu loop can react. */
  sa_int.sa_flags = 0;
  if (sigaction(SIGINT, &sa_int, &tui_previous_sigint) != 0) {
    LOG_ERROR("Failed to install SIGINT handler: %s", strerror(errno));
    return APP_ERROR_SIGNAL;
  }

  struct sigaction sa_term = {.sa_handler = tui_signal_handler};
  sigemptyset(&sa_term.sa_mask);
  /* Match SIGINT: wake blocking wgetch() so SIGTERM can shut down promptly. */
  sa_term.sa_flags = 0;
  if (sigaction(SIGTERM, &sa_term, &tui_previous_sigterm) != 0) {
    const int saved_errno = errno;
    (void)sigaction(SIGINT, &tui_previous_sigint, NULL);
    errno = saved_errno;
    LOG_ERROR("Failed to install SIGTERM handler: %s", strerror(errno));
    return APP_ERROR_SIGNAL;
  }
  tui_signal_handlers_installed = true;
#endif
  return APP_SUCCESS;
}

static void tui_uninstall_signal_handlers(void) {
#ifndef _WIN32
  if (!tui_signal_handlers_installed) {
    return;
  }
  (void)sigaction(SIGINT, &tui_previous_sigint, NULL);
  (void)sigaction(SIGTERM, &tui_previous_sigterm, NULL);
  tui_signal_handlers_installed = false;
#endif
}

/* ---- helpers ------------------------------------------------------------- */

static bool tui_has_interactive_terminal(void) {
  return app_terminal_is_interactive();
}

static void tui_discard_curses_input(void) {
  /* ncurses/terminfo may probe the terminal during startup, for example with
   * DSR/u7 cursor-position requests. If an interrupt or early error tears the
   * screen down before wgetch consumes the reply, that reply can leak back to
   * the shell as text such as "[[26;1R". Drop ncurses' own input queue before
   * endwin(), then flush the underlying TTY queue after terminal modes are
   * restored. */
  (void)flushinp();
}

static int tui_clamped_strlen(const char *text, int max_len) {
  if (text == nullptr || max_len <= 0) {
    return 0;
  }
  int columns = 0;
  (void)app_text_truncate_utf8_columns(text, max_len, &columns);
  return columns;
}

static void tui_write_clamped(WINDOW *win, int y, int x, int width,
                              const char *text) {
  if (!win || !text || width <= 0) {
    return;
  }

  const int max_y = getmaxy(win);
  const int max_x = getmaxx(win);
  if (y < 0 || y >= max_y || x >= max_x) {
    return;
  }
  if (x < 0) {
    x = 0;
  }
  if (width > max_x - x) {
    width = max_x - x;
  }
  if (width <= 0) {
    return;
  }

  const size_t bytes = app_text_truncate_utf8_columns(text, width, NULL);
  mvwaddnstr(win, y, x, text, bytes > (size_t)INT_MAX ? INT_MAX : (int)bytes);
}

static short tui_default_fg(void) {
  return tui_default_colors ? -1 : COLOR_WHITE;
}

short tui_default_bg(void) {
  return tui_default_colors ? -1 : COLOR_BLACK;
}

/* ---- public lifecycle --------------------------------------------------- */

app_error tui_init(void) {
  if (tui_initialized) {
    return APP_SUCCESS;
  }

  setlocale(LC_ALL, "");

  if (!tui_has_interactive_terminal()) {
    LOG_ERROR("TUI requires an interactive terminal");
    return APP_ERROR_IO;
  }

  if (initscr() == NULL) {
    LOG_ERROR("Failed to initialize ncurses");
    return APP_ERROR_INTERNAL;
  }

  app_error err = tui_install_signal_handlers();
  if (err != APP_SUCCESS) {
    tui_discard_curses_input();
    endwin();
    app_terminal_discard_pending_input();
    return err;
  }

  if (cbreak() == ERR) {
    LOG_ERROR("Failed to enable cbreak mode");
    err = APP_ERROR_INTERNAL;
    goto fail;
  }
  if (noecho() == ERR) {
    LOG_ERROR("Failed to disable terminal echo");
    err = APP_ERROR_INTERNAL;
    goto fail;
  }
  if (keypad(stdscr, TRUE) == ERR) {
    LOG_ERROR("Failed to enable keypad input");
    err = APP_ERROR_INTERNAL;
    goto fail;
  }

  // With keypad() on, ncurses holds back a bare ESC byte for ESCDELAY ms to
  // disambiguate it from an arrow/function-key sequence; the historical default
  // (~1s) makes every Esc affordance (overlay menu, cancel search, dismiss
  // dialog) feel like a hang. 25ms is instant to a human yet ample for a
  // genuine CSI/SS3 sequence to arrive intact over local and typical SSH
  // latency. PDCurses reads the Windows console directly and delivers Esc and
  // function keys as distinct events, so it never imposes this wait and exposes
  // no ESCDELAY/set_escdelay knob to tune — the ncurses-only guard is correct.
#if defined(NCURSES_VERSION)
  (void)set_escdelay(25);
#endif

  tui_saved_cursor_state = curs_set(0);

  // Honor the app-resolved color policy (app_use_colors) so the TUI suppresses
  // or forces color for exactly the same NO_COLOR / FORCE_COLOR / CLICOLOR(_
  // FORCE) / --no-color / --plain inputs the CLI obeys. tui_color_policy == 0
  // means a hard disable; -1 (unset) and 1 both allow color subject to terminal
  // capability. When disabled we skip start_color() entirely and leave
  // tui_colors_active false, so tui_set_color() becomes a no-op and the whole
  // UI renders in the terminal's default monochrome.
  if (tui_color_policy != 0 && !tui_color_env_disabled() && has_colors()) {
    if (start_color() != ERR) {
      tui_default_colors = use_default_colors() != ERR;
      (void)tui_init_colors();
      tui_colors_active = true;
    }
  }

  clear();
  refresh();

  tui_initialized = true;
  LOG_DEBUG("TUI initialized successfully");
  return APP_SUCCESS;

fail:
  tui_discard_curses_input();
  tui_uninstall_signal_handlers();
  endwin();
  app_terminal_discard_pending_input();
  tui_default_colors = false;
  tui_colors_active = false;
  tui_interrupted_signal = 0;
  return err;
}

void tui_cleanup(void) {
  if (!tui_initialized) {
    return;
  }

  tui_discard_curses_input();
  clear();
  refresh();
  endwin();
  app_terminal_discard_pending_input();
  tui_uninstall_signal_handlers();

  tui_initialized = false;
  tui_default_colors = false;
  tui_colors_active = false;
  tui_clear_background_window();
  tui_interrupted_signal = 0;
  LOG_DEBUG("TUI cleaned up");
}

bool tui_is_initialized(void) {
  return tui_initialized;
}

bool tui_interrupted(void) {
  return tui_interrupted_signal != 0;
}

app_error tui_take_interrupt_error(void) {
  const int signum = (int)tui_interrupted_signal;
  tui_interrupted_signal = 0;
  return signum == SIGTERM ? APP_ERROR_TERMINATED : APP_ERROR_INTERRUPTED;
}

/* ---- color management --------------------------------------------------- */

/* Curses cannot portably emit per-cell 24-bit SGR without desynchronizing its
 * screen model. The closest high-fidelity path is a programmable palette: seed
 * custom slots from the shared UI roles, then bind small semantic color pairs
 * to those slots. When palette mutation is unavailable, use the same RGB ->
 * xterm256 / semantic ANSI-16 degradation the styled CLI uses. */
enum {
  TUI_RGB_TEXT = 16,
  TUI_RGB_TITLE,
  TUI_RGB_MUTED,
  TUI_RGB_ACCENT,
  TUI_RGB_SUCCESS,
  TUI_RGB_WARNING,
  TUI_RGB_INFO,
  TUI_RGB_ERROR,
  TUI_RGB_SELECTION_FG,
  TUI_RGB_SELECTION_BG,
  TUI_RGB_LAST,
};

static bool tui_color_env_disabled(void) {
  const char *color = getenv("APP_CLI_COLOR");
  return color && strcmp(color, "never") == 0;
}

static app_cli_color_profile_id tui_requested_color_profile(void) {
  const char *color = getenv("APP_CLI_COLOR");
  if (!color || color[0] == '\0' || strcmp(color, "auto") == 0 ||
      strcmp(color, "never") == 0) {
    return APP_CLI_COLOR_PROFILE_NONE;
  }
  if (strcmp(color, "truecolor") == 0) {
    return APP_CLI_COLOR_PROFILE_TRUECOLOR;
  }
  if (strcmp(color, "256") == 0) {
    return APP_CLI_COLOR_PROFILE_ANSI256;
  }
  if (strcmp(color, "16") == 0) {
    return APP_CLI_COLOR_PROFILE_ANSI16;
  }
  return APP_CLI_COLOR_PROFILE_NONE;
}

static bool tui_accent_override_is_indexed(void) {
  const char *accent = getenv("APP_CLI_ACCENT");
  if (!accent || !accent[0]) {
    return false;
  }
  app_ui_color_t color;
  if (!app_ui_color_parse(accent, &color)) {
    return false;
  }
  return color.kind == APP_UI_COLOR_ANSI16 ||
         color.kind == APP_UI_COLOR_ANSI256;
}

static bool tui_palette_is_programmable(void) {
  return can_change_color() && COLORS >= TUI_RGB_LAST &&
         COLOR_PAIRS > TUI_COLOR_MAX;
}

static app_rgb_t tui_role_rgb(const app_ui_color_scheme_t *scheme,
                              app_ui_theme_mode_id mode, app_ui_role_id role,
                              app_rgb_t fallback) {
  app_ui_color_t color = app_ui_theme_pick(scheme, role, mode);
  if (color.kind == APP_UI_COLOR_RGB) {
    return color.rgb;
  }
  color = app_ui_theme_pick(app_ui_theme_default_scheme(), role, mode);
  return color.kind == APP_UI_COLOR_RGB ? color.rgb : fallback;
}

/* Seed one custom palette entry from an 8-bit RGB triple, converting to the
 * 0..1000 scale init_color() expects. */
static void tui_init_color_from(short slot, app_rgb_t c) {
  (void)init_color(slot, (short)app_design_chan_to_curses(c.r),
                   (short)app_design_chan_to_curses(c.g),
                   (short)app_design_chan_to_curses(c.b));
}

static void tui_define_rgb_palette(const app_ui_color_scheme_t *scheme,
                                   app_ui_theme_mode_id mode) {
  const app_design_palette_t *p = &APP_DESIGN_PALETTE;
  tui_init_color_from(TUI_RGB_TEXT,
                      tui_role_rgb(scheme, mode, APP_UI_ROLE_TEXT, p->fg));
  tui_init_color_from(TUI_RGB_TITLE,
                      tui_role_rgb(scheme, mode, APP_UI_ROLE_TITLE, p->amber));
  tui_init_color_from(TUI_RGB_MUTED,
                      tui_role_rgb(scheme, mode, APP_UI_ROLE_MUTED, p->muted));
  tui_init_color_from(TUI_RGB_ACCENT,
                      tui_role_rgb(scheme, mode, APP_UI_ROLE_ACCENT, p->amber));
  tui_init_color_from(
      TUI_RGB_SUCCESS,
      tui_role_rgb(scheme, mode, APP_UI_ROLE_SUCCESS, p->green));
  tui_init_color_from(
      TUI_RGB_WARNING,
      tui_role_rgb(scheme, mode, APP_UI_ROLE_WARNING, p->yellow));
  tui_init_color_from(TUI_RGB_INFO,
                      tui_role_rgb(scheme, mode, APP_UI_ROLE_INFO, p->blue));
  tui_init_color_from(
      TUI_RGB_ERROR,
      tui_role_rgb(scheme, mode, APP_UI_ROLE_ERROR_DETAILS, p->red));
  tui_init_color_from(
      TUI_RGB_SELECTION_FG,
      tui_role_rgb(scheme, mode, APP_UI_ROLE_SELECTION_FG, p->near_black));
  tui_init_color_from(
      TUI_RGB_SELECTION_BG,
      tui_role_rgb(scheme, mode, APP_UI_ROLE_SELECTION_BG, p->amber));
}

static short tui_fold_ansi16(uint8_t index, short fallback) {
  if (COLORS > 0 && COLORS < 16 && index >= 8) {
    if (index == 8 && fallback >= 0) {
      return fallback;
    }
    index = (uint8_t)(index - 8);
  }
  if (COLORS > 0 && index >= (uint8_t)COLORS) {
    return fallback;
  }
  return (short)index;
}

static short tui_color_index(app_ui_color_t color,
                             app_cli_color_profile_id profile, short fallback) {
  switch (color.kind) {
  case APP_UI_COLOR_ANSI16:
    return tui_fold_ansi16(color.ansi16, fallback);
  case APP_UI_COLOR_ANSI256:
    if (profile == APP_CLI_COLOR_PROFILE_ANSI256 && COLORS >= 256) {
      return (short)color.ansi256;
    }
    return color.ansi256 < 16 ? tui_fold_ansi16(color.ansi256, fallback)
                              : fallback;
  case APP_UI_COLOR_RGB:
    if (profile == APP_CLI_COLOR_PROFILE_ANSI256 && COLORS >= 256) {
      return (short)app_color_rgb_to_xterm256(color.rgb);
    }
    if (color.has_ansi16_hint) {
      return tui_fold_ansi16(color.ansi16_hint, fallback);
    }
    return tui_fold_ansi16(app_color_rgb_to_ansi16(color.rgb), fallback);
  case APP_UI_COLOR_DEFAULT:
    return fallback;
  case APP_UI_COLOR_UNSET:
  default:
    return fallback;
  }
}

static short tui_role_index(const app_ui_color_scheme_t *scheme,
                            app_ui_theme_mode_id mode, app_ui_role_id role,
                            app_cli_color_profile_id profile, short fallback) {
  return tui_color_index(app_ui_theme_pick(scheme, role, mode), profile,
                         fallback);
}

static void tui_init_programmable_pairs(const app_ui_color_scheme_t *scheme,
                                        app_ui_theme_mode_id mode, short bg) {
  tui_define_rgb_palette(scheme, mode);
  init_pair(TUI_COLOR_HIGHLIGHT, TUI_RGB_SELECTION_FG, TUI_RGB_SELECTION_BG);
  init_pair(TUI_COLOR_ERROR, TUI_RGB_ERROR, bg);
  init_pair(TUI_COLOR_SUCCESS, TUI_RGB_SUCCESS, bg);
  init_pair(TUI_COLOR_WARNING, TUI_RGB_WARNING, bg);
  init_pair(TUI_COLOR_INFO, TUI_RGB_INFO, bg);
  init_pair(TUI_COLOR_MENU_NORMAL, TUI_RGB_TEXT, bg);
  init_pair(TUI_COLOR_BORDER, TUI_RGB_MUTED, bg);
  init_pair(TUI_COLOR_TITLE, TUI_RGB_TITLE, bg);
  init_pair(TUI_COLOR_ACCENT, TUI_RGB_ACCENT, bg);
  init_pair(TUI_COLOR_DIM, TUI_RGB_MUTED, bg);
}

static void tui_init_indexed_pairs(const app_ui_color_scheme_t *scheme,
                                   app_ui_theme_mode_id mode,
                                   app_cli_color_profile_id profile, short bg) {
  const short text =
      tui_role_index(scheme, mode, APP_UI_ROLE_TEXT, profile, tui_default_fg());
  const short title =
      tui_role_index(scheme, mode, APP_UI_ROLE_TITLE, profile, COLOR_YELLOW);
  const short accent =
      tui_role_index(scheme, mode, APP_UI_ROLE_ACCENT, profile, COLOR_YELLOW);
  const short muted =
      tui_role_index(scheme, mode, APP_UI_ROLE_MUTED, profile, COLOR_WHITE);
  init_pair(TUI_COLOR_HIGHLIGHT,
            tui_role_index(scheme, mode, APP_UI_ROLE_SELECTION_FG, profile,
                           COLOR_BLACK),
            tui_role_index(scheme, mode, APP_UI_ROLE_SELECTION_BG, profile,
                           COLOR_YELLOW));
  init_pair(TUI_COLOR_ERROR,
            tui_role_index(scheme, mode, APP_UI_ROLE_ERROR_DETAILS, profile,
                           COLOR_RED),
            bg);
  init_pair(
      TUI_COLOR_SUCCESS,
      tui_role_index(scheme, mode, APP_UI_ROLE_SUCCESS, profile, COLOR_GREEN),
      bg);
  init_pair(
      TUI_COLOR_WARNING,
      tui_role_index(scheme, mode, APP_UI_ROLE_WARNING, profile, COLOR_YELLOW),
      bg);
  init_pair(TUI_COLOR_INFO,
            tui_role_index(scheme, mode, APP_UI_ROLE_INFO, profile, COLOR_BLUE),
            bg);
  init_pair(TUI_COLOR_MENU_NORMAL, text, bg);
  init_pair(TUI_COLOR_BORDER,
            tui_role_index(scheme, mode, APP_UI_ROLE_BORDER, profile, muted),
            bg);
  init_pair(TUI_COLOR_TITLE, title, bg);
  init_pair(TUI_COLOR_ACCENT, accent, bg);
  init_pair(TUI_COLOR_DIM, muted, bg);
}

app_error tui_init_colors(void) {
  if (!has_colors()) {
    LOG_WARNING("Terminal does not support colors");
    return APP_SUCCESS;
  }

  app_ui_color_scheme_t scheme = *app_ui_theme_default_scheme();
  app_ui_theme_apply_env_overrides(&scheme);
  const app_ui_theme_mode_id mode = cs_theme_mode_resolve();
  const app_cli_color_profile_id requested = tui_requested_color_profile();
  const short bg = tui_default_bg();

  if ((requested == APP_CLI_COLOR_PROFILE_NONE ||
       requested == APP_CLI_COLOR_PROFILE_TRUECOLOR) &&
      !tui_accent_override_is_indexed() && tui_palette_is_programmable()) {
    tui_init_programmable_pairs(&scheme, mode, bg);
    return APP_SUCCESS;
  }

  if (requested != APP_CLI_COLOR_PROFILE_ANSI16 && COLORS >= 256) {
    tui_init_indexed_pairs(&scheme, mode, APP_CLI_COLOR_PROFILE_ANSI256, bg);
    return APP_SUCCESS;
  }

  tui_init_indexed_pairs(&scheme, mode, APP_CLI_COLOR_PROFILE_ANSI16, bg);
  return APP_SUCCESS;
}

void tui_set_color(WINDOW *win, tui_color_pair_t color) {
  // Guard on tui_colors_active, not has_colors(): when the color policy
  // disabled color we never called start_color()/init_pair(), so applying a
  // COLOR_PAIR would reference an uninitialized pair. Skipping keeps the UI
  // monochrome.
  if (win && tui_colors_active && color > TUI_COLOR_DEFAULT &&
      color < TUI_COLOR_MAX) {
    wattron(win, COLOR_PAIR(color) | (color == TUI_COLOR_DIM ? A_DIM : 0));
  }
}

void tui_unset_color(WINDOW *win, tui_color_pair_t color) {
  if (win && tui_colors_active && color > TUI_COLOR_DEFAULT &&
      color < TUI_COLOR_MAX) {
    wattroff(win, COLOR_PAIR(color) | (color == TUI_COLOR_DIM ? A_DIM : 0));
  }
}

void tui_set_color_enabled(bool enabled) {
  tui_color_policy = enabled ? 1 : 0;
}

bool tui_colors_enabled(void) {
  return tui_colors_active;
}

tui_window_t *tui_create_window(int height, int width, int y, int x) {
  if (height <= 0 || width <= 0) {
    return NULL;
  }

  if (y < 0) {
    y = 0;
  }
  if (x < 0) {
    x = 0;
  }
  if (tui_initialized) {
    const int max_y = getmaxy(stdscr);
    const int max_x = getmaxx(stdscr);
    if (y >= max_y || x >= max_x) {
      return NULL;
    }
    if (height > max_y - y) {
      height = max_y - y;
    }
    if (width > max_x - x) {
      width = max_x - x;
    }
  }

  tui_window_t *window = calloc(1, sizeof(tui_window_t));
  if (!window) {
    return NULL;
  }

  window->height = height;
  window->width = width;
  window->y = y;
  window->x = x;

  window->win = newwin(height, width, y, x);
  if (!window->win) {
    free(window);
    return NULL;
  }

  keypad(window->win, TRUE);
  return window;
}

static void tui_draw_window_title(tui_window_t *window, const char *title) {
  if (!window || !window->win || !title || !window->has_border) {
    return;
  }

  const int max_width = window->width - 2;
  if (max_width <= 0) {
    return;
  }
  /* Centered, bold, UPPERCASE title rendered one row inside the top border
   * (dawn-style), not on the border edge. ASCII-safe upper preserves any
   * multibyte UTF-8, and we centre by display columns, not byte length. */
  char up[128];
  tui_ascii_upper_copy(up, sizeof(up), title);
  /* Truncate by display columns, but remember the byte length so the
   * mvwaddnstr byte limit matches multibyte content. */
  int columns = 0;
  const size_t title_bytes =
      app_text_truncate_utf8_columns(up, max_width, &columns);
  int x_pos = (window->width - columns) / 2;
  if (x_pos < 1) {
    x_pos = 1;
  }
  tui_set_color(window->win, TUI_COLOR_TITLE);
  wattron(window->win, A_BOLD);
  mvwaddnstr(window->win, 1, x_pos, up,
             title_bytes > (size_t)INT_MAX ? INT_MAX : (int)title_bytes);
  wattroff(window->win, A_BOLD);
  tui_unset_color(window->win, TUI_COLOR_TITLE);
}

tui_window_t *tui_create_centered_window(int height, int width) {
  if (!tui_initialized) {
    return NULL;
  }

  const int max_y = getmaxy(stdscr);
  const int max_x = getmaxx(stdscr);
  if (max_y < 3 || max_x < 8) {
    return NULL;
  }

  if (height > max_y - 2) {
    height = max_y - 2;
  }
  if (width > max_x - 2) {
    width = max_x - 2;
  }
  if (height <= 0 || width <= 0) {
    return NULL;
  }

  const int y = (max_y - height) / 2;
  const int x = (max_x - width) / 2;
  return tui_create_window(height, width, y, x);
}

void tui_destroy_window(tui_window_t *window) {
  if (!window) {
    return;
  }

  if (window->win) {
    delwin(window->win);
  }

  free(window->title);
  free(window);
}

void tui_draw_border(tui_window_t *window) {
  if (!window || !window->win) {
    return;
  }

  WINDOW *win = window->win;
  const int h = getmaxy(win);
  const int w = getmaxx(win);
  tui_set_color(win, TUI_COLOR_BORDER);
  if (h >= 2 && w >= 2) {
    /* Rounded box (dawn-style). Sides use ACS line glyphs; only the four
     * corners need the rounded Unicode characters. */
    mvwhline(win, 0, 1, ACS_HLINE, w - 2);
    mvwhline(win, h - 1, 1, ACS_HLINE, w - 2);
    mvwvline(win, 1, 0, ACS_VLINE, h - 2);
    mvwvline(win, 1, w - 1, ACS_VLINE, h - 2);
    mvwaddnstr(win, 0, 0, "╭", -1);         /* top-left  */
    mvwaddnstr(win, 0, w - 1, "╮", -1);     /* top-right */
    mvwaddnstr(win, h - 1, 0, "╰", -1);     /* bottom-left */
    mvwaddnstr(win, h - 1, w - 1, "╯", -1); /* bottom-right */
  } else {
    box(win, 0, 0);
  }
  tui_unset_color(win, TUI_COLOR_BORDER);
  window->has_border = true;

  if (window->title) {
    tui_draw_window_title(window, window->title);
  }
}

void tui_set_window_title(tui_window_t *window, const char *title) {
  if (!window || !window->win || !title) {
    return;
  }

  char *title_copy = strdup(title);
  if (!title_copy) {
    return;
  }

  free(window->title);
  window->title = title_copy;

  tui_draw_window_title(window, window->title);
}

void tui_refresh_window(tui_window_t *window) {
  if (window && window->win) {
    wrefresh(window->win);
  }
}

void tui_clear_window(tui_window_t *window) {
  if (window && window->win) {
    wclear(window->win);
    if (window->has_border) {
      tui_draw_border(window);
    }
  }
}

void tui_draw_status_line(WINDOW *win, const char *left, const char *right) {
  if (!win) {
    return;
  }

  const int max_y = getmaxy(win);
  const int max_x = getmaxx(win);
  if (max_y <= 0 || max_x <= 0) {
    return;
  }

  const int y = max_y > 2 ? max_y - 2 : max_y - 1;
  const int x_start = max_x > 2 ? 1 : 0;
  const int line_width = max_x > 2 ? max_x - 2 : max_x;
  tui_set_color(win, TUI_COLOR_INFO);
  mvwhline(win, y, x_start, ' ', line_width);

  if (left) {
    tui_write_clamped(win, y, x_start + 1, line_width - 2, left);
  }
  if (right) {
    const int right_len = tui_clamped_strlen(right, line_width - 2);
    const int x = x_start + line_width - right_len - 1;
    if (x > x_start + 1) {
      tui_write_clamped(win, y, x, right_len, right);
    }
  }

  tui_unset_color(win, TUI_COLOR_INFO);
}

void tui_print_centered(WINDOW *win, int y, const char *text) {
  if (!win || !text) {
    return;
  }

  int max_x = getmaxx(win);
  if (max_x <= 0) {
    return;
  }
  int len = tui_clamped_strlen(text, max_x);
  int x = (max_x - len) / 2;
  if (x < 0)
    x = 0;

  tui_write_clamped(win, y, x, max_x - x, text);
}

typedef struct {
  WINDOW *win;
  int y;
  int x;
  int width;
  int max_y;
} tui_wrap_emit_ctx_t;

static bool tui_wrap_emit_line(void *userdata, const char *bytes,
                               size_t byte_count, int columns) {
  (void)columns;
  tui_wrap_emit_ctx_t *ctx = userdata;
  if (ctx->y >= ctx->max_y) {
    return false;
  }
  char line[512];
  size_t n = byte_count < sizeof(line) - 1 ? byte_count : sizeof(line) - 1;
  memcpy(line, bytes, n);
  line[n] = '\0';
  tui_write_clamped(ctx->win, ctx->y, ctx->x, ctx->width, line);
  ctx->y++;
  return true;
}

void tui_print_wrapped(WINDOW *win, int y, int x, int width, const char *text) {
  if (!win || !text || width <= 0) {
    return;
  }

  const int max_y = getmaxy(win);
  const int max_x = getmaxx(win);
  if (max_y <= 0 || max_x <= 0) {
    return;
  }
  if (x < 0) {
    x = 0;
  }
  if (x >= max_x) {
    return;
  }
  if (width > max_x - x) {
    width = max_x - x;
  }

  tui_wrap_emit_ctx_t ctx = {
      .win = win, .y = y, .x = x, .width = width, .max_y = max_y};
  app_text_wrap_utf8(text, width, 0, 0, tui_wrap_emit_line, &ctx);
}

int tui_get_char(void) {
  if (!tui_initialized) {
    return ERR;
  }
  return getch();
}

app_error tui_get_string(WINDOW *win, char *buffer, size_t size,
                         const char *prompt) {
  if (!win || !buffer || size == 0) {
    return APP_ERROR_INVALID_ARG;
  }

  if (prompt) {
    wprintw(win, "%s", prompt);
  }

  const int previous_cursor = curs_set(1);
  echo();

  const size_t requested_len = size - 1;
  const int max_chars =
      requested_len > (size_t)INT_MAX ? INT_MAX : (int)requested_len;
  int result = wgetnstr(win, buffer, max_chars);

  noecho();
  if (previous_cursor != ERR) {
    (void)curs_set(previous_cursor);
  } else {
    (void)curs_set(tui_saved_cursor_state);
  }

  if (result == ERR) {
    return APP_ERROR_IO;
  }

  return APP_SUCCESS;
}

static tui_window_t *tui_modal_open(int preferred_height, int preferred_width,
                                    const char *title) {
  if (!tui_initialized) {
    return NULL;
  }

  const int max_y = getmaxy(stdscr);
  const int max_x = getmaxx(stdscr);
  if (max_y < 4 || max_x < 8) {
    return NULL;
  }

  int width = preferred_width;
  int height = preferred_height;
  if (width < 8) {
    width = 8;
  }
  if (height < 4) {
    height = 4;
  }
  if (width > max_x - 2) {
    width = max_x - 2;
  }
  if (height > max_y - 2) {
    height = max_y - 2;
  }

  tui_window_t *window = tui_create_centered_window(height, width);
  if (!window) {
    return NULL;
  }

  /* Blank the screen so the dialog floats on a clean backdrop instead of
   * letting the structured menu behind it peek around the box edges. The
   * background is restored when the dialog closes. */
  clear();
  refresh();

  tui_draw_border(window);
  if (title) {
    tui_set_window_title(window, title);
  }

  return window;
}

static void tui_modal_restore_background(void) {
  touchwin(stdscr);
  tui_window_t *bg = tui_get_background_window();
  if (bg && bg->win) {
    tui_clear_window(bg);
    if (bg->has_border) {
      tui_draw_border(bg);
    }
    if (bg->title) {
      tui_set_window_title(bg, bg->title);
    }
    tui_refresh_window(bg);
  }
  refresh();
}

static void tui_modal_close(tui_window_t *window) {
  tui_destroy_window(window);
  tui_modal_restore_background();
}

static void tui_modal_redraw_background(void);

// Run a modal dialog loop until the key handler reports DONE or the user
// triggers a SIGINT. Handles window setup, redraw on resize, and teardown.
// Returns true if the loop completed normally and false on open failure.
bool tui_modal_run(int height, int width, const char *title,
                   tui_modal_redraw_fn redraw, tui_modal_key_fn handle,
                   void *userdata) {
  tui_window_t *window = tui_modal_open(height, width, title);
  if (!window) {
    return false;
  }
  redraw(window, userdata);
  tui_refresh_window(window);

  while (1) {
    if (tui_interrupted()) {
      break;
    }
    const int ch = wgetch(window->win);
    if (ch == KEY_RESIZE) {
      tui_modal_redraw_background();
      tui_destroy_window(window);
      window = tui_modal_open(height, width, title);
      if (!window) {
        return false;
      }
      redraw(window, userdata);
      tui_refresh_window(window);
      continue;
    }
    if (ch == ERR) {
      napms(10);
      continue;
    }
    if (handle(window, ch, userdata) == TUI_MODAL_DONE) {
      break;
    }
  }

  tui_modal_close(window);
  return true;
}

static void tui_modal_redraw_background(void) {
  tui_modal_restore_background();
}

typedef struct {
  const char *message;
} tui_message_state_t;

static void tui_message_redraw(tui_window_t *window, void *userdata) {
  const tui_message_state_t *state = userdata;
  if (state->message) {
    tui_print_wrapped(window->win, 3, 2, window->width - 4, state->message);
  }
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2, "Press any key");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t tui_message_key(tui_window_t *window, int ch,
                                            void *userdata) {
  (void)window;
  (void)ch;
  (void)userdata;
  return TUI_MODAL_DONE;
}

void tui_show_message(const char *title, const char *message) {
  int height = 9;
  if (message) {
    height = 7; /* border + title + blank + body + blank + prompt + border */
    for (const char *p = message; *p != '\0'; p++) {
      if (*p == '\n') {
        height++;
      }
    }
  }

  tui_message_state_t state = {.message = message};
  (void)tui_modal_run(height, 60, title, tui_message_redraw, tui_message_key,
                      &state);
}

typedef struct {
  const char *question;
  bool result;
} tui_confirm_state_t;

static void tui_confirm_redraw(tui_window_t *window, void *userdata) {
  const tui_confirm_state_t *state = userdata;
  if (state->question) {
    tui_print_wrapped(window->win, 3, 2, window->width - 4, state->question);
  }
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2, "y/n, Esc cancels");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t tui_confirm_key(tui_window_t *window, int ch,
                                            void *userdata) {
  (void)window;
  tui_confirm_state_t *state = userdata;
  if (ch == 'y' || ch == 'Y') {
    state->result = true;
    return TUI_MODAL_DONE;
  }
  if (ch == 'n' || ch == 'N' || ch == 'q' || ch == 'Q' || ch == 27) {
    state->result = false;
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

bool tui_confirm(const char *title, const char *question) {
  tui_confirm_state_t state = {.question = question, .result = false};
  if (!tui_modal_run(9, 50, title, tui_confirm_redraw, tui_confirm_key,
                     &state)) {
    return false;
  }
  return state.result;
}

typedef struct {
  const char *prompt;
  char *buffer;
  size_t size;
  size_t len;
  app_error result;
} tui_input_state_t;

static void tui_input_redraw(tui_window_t *window, void *userdata) {
  tui_input_state_t *state = userdata;
  if (state->prompt) {
    tui_write_clamped(window->win, 3, 2, window->width - 4, state->prompt);
  }

  mvwprintw(window->win, 5, 2, "> ");
  const int max_cols = window->width > 6 ? window->width - 6 : 0;
  size_t start = 0;
  if (max_cols > 0 && state->len > (size_t)max_cols) {
    start = state->len - (size_t)max_cols;
  }
  if (max_cols > 0) {
    tui_write_clamped(window->win, 5, 4, max_cols, state->buffer + start);
  }
  wmove(window->win, 5, 4 + (int)(state->len - start));
}

static tui_modal_decision_t tui_input_key(tui_window_t *window, int ch,
                                          void *userdata) {
  tui_input_state_t *state = userdata;
  switch (ch) {
  case '\n':
  case KEY_ENTER:
    state->result = APP_SUCCESS;
    return TUI_MODAL_DONE;
  case 27:
    state->result = APP_ERROR_IO;
    return TUI_MODAL_DONE;
  case KEY_BACKSPACE:
  case 127:
  case 8:
    if (state->len > 0) {
      state->buffer[--state->len] = '\0';
    } else {
      tui_beep();
    }
    break;
  default:
    if (ch >= 32 && ch < 0x7f) {
      if (state->len + 1 < state->size) {
        state->buffer[state->len++] = (char)ch;
        state->buffer[state->len] = '\0';
      } else {
        tui_beep();
      }
    }
    break;
  }

  werase(window->win);
  tui_draw_border(window);
  tui_input_redraw(window, state);
  tui_refresh_window(window);
  return TUI_MODAL_CONTINUE;
}

app_error tui_input_dialog(const char *title, const char *prompt, char *buffer,
                           size_t size) {
  if (!tui_initialized || !buffer || size == 0) {
    return APP_ERROR_INVALID_ARG;
  }

  buffer[0] = '\0';
  const int previous_cursor = curs_set(1);
  tui_input_state_t state = {.prompt = prompt,
                             .buffer = buffer,
                             .size = size,
                             .len = 0,
                             .result = APP_ERROR_IO};

  const bool opened =
      tui_modal_run(9, 60, title, tui_input_redraw, tui_input_key, &state);
  if (previous_cursor != ERR) {
    (void)curs_set(previous_cursor);
  } else {
    (void)curs_set(tui_saved_cursor_state);
  }
  return opened ? state.result : APP_ERROR_INTERNAL;
}

void tui_beep(void) {
  beep();
}

void tui_flash(void) {
  flash();
}

int tui_get_max_x(void) {
  return getmaxx(stdscr);
}

int tui_get_max_y(void) {
  return getmaxy(stdscr);
}

bool tui_terminal_meets_minimum(void) {
  return tui_get_max_x() >= TUI_MIN_COLS && tui_get_max_y() >= TUI_MIN_ROWS;
}
