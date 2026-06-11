/*
 * Terminal UI (TUI) public API using ncurses.
 */

#pragma once

#ifdef _WIN32
#include <curses.h>
#else
#include <ncurses.h>
#endif
#include <stdbool.h>
#include <stddef.h>

#include "../core/error.h"
#include "../core/types.h"

#define TUI_MIN_COLS 48
#define TUI_MIN_ROWS 12

// TUI color pairs
typedef enum {
  TUI_COLOR_DEFAULT = 0,
  TUI_COLOR_HIGHLIGHT,
  TUI_COLOR_ERROR,
  TUI_COLOR_SUCCESS,
  TUI_COLOR_WARNING,
  TUI_COLOR_INFO,
  TUI_COLOR_MENU_NORMAL,
  TUI_COLOR_BORDER,
  TUI_COLOR_TITLE,
  TUI_COLOR_ACCENT,
  TUI_COLOR_DIM,
  TUI_COLOR_MAX
} tui_color_pair_t;

typedef struct tui_window tui_window_t;

// Window wrapper for safer window management
struct tui_window {
  WINDOW *win;
  int height;
  int width;
  int y;
  int x;
  bool has_border;
  char *title;
};

// Initialization / cleanup
APP_NODISCARD app_error tui_init(void);
void tui_cleanup(void);
bool tui_is_initialized(void);
bool tui_interrupted(void);
APP_NODISCARD app_error tui_take_interrupt_error(void);

// Color management
APP_NODISCARD app_error tui_init_colors(void);
void tui_set_color(WINDOW *win, tui_color_pair_t color);
void tui_unset_color(WINDOW *win, tui_color_pair_t color);

// Set the TUI color policy before tui_init(), mirroring the CLI. The generated
// app passes app_use_colors(config) so NO_COLOR / FORCE_COLOR /
// CLICOLOR(_FORCE) / --no-color / --plain disable or force TUI color exactly as
// they do CLI output. When never called, the TUI falls back to terminal
// capability detection alone, preserving standalone behavior for foreign
// library consumers.
void tui_set_color_enabled(bool enabled);

// Whether color was actually activated by the most recent tui_init() (policy
// allowed it, the terminal supports color, and start_color() succeeded).
bool tui_colors_enabled(void);

// The background color index the TUI binds its pairs against (-1 when the
// terminal's default background is in use). Exposed so a cs_surface can bind
// its own theme-resolved pairs against the same background the surrounding TUI
// uses.
short tui_default_bg(void);

// Window helpers
APP_NODISCARD tui_window_t *tui_create_window(int height, int width, int y,
                                              int x);
APP_NODISCARD tui_window_t *tui_create_centered_window(int height, int width);
void tui_destroy_window(tui_window_t *window);
void tui_draw_border(tui_window_t *window);
void tui_set_window_title(tui_window_t *window, const char *title);
void tui_refresh_window(tui_window_t *window);
void tui_clear_window(tui_window_t *window);
void tui_draw_status_line(WINDOW *win, const char *left, const char *right);

// Text helpers
void tui_print_centered(WINDOW *win, int y, const char *text);
void tui_print_wrapped(WINDOW *win, int y, int x, int width, const char *text);

// Input helpers
APP_NODISCARD int tui_get_char(void);
APP_NODISCARD app_error tui_get_string(WINDOW *win, char *buffer, size_t size,
                                       const char *prompt);

// Dialogs
void tui_show_message(const char *title, const char *message);
bool tui_confirm(const char *title, const char *question);
APP_NODISCARD app_error tui_input_dialog(const char *title, const char *prompt,
                                         char *buffer, size_t size);
APP_NODISCARD app_error tui_run_app(void);

// Utilities
void tui_beep(void);
void tui_flash(void);
int tui_get_max_x(void);
int tui_get_max_y(void);
bool tui_terminal_meets_minimum(void);

// Menu and progress APIs are declared in dedicated headers to keep tui.h lean.
#include "tui_menu.h"
#include "tui_progress.h"
