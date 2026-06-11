/*
 * tui_progress.c - progress bar implementation using ncurses windows.
 */

#include "tui_progress.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "tui.h"

struct tui_progress {
  tui_window_t *window;
  int max_value;
  int current_value;
  char *title;
};

static void tui_progress_draw(tui_progress_t *progress, const char *status) {
  if (!progress || !progress->window) {
    return;
  }

  tui_window_t *window = progress->window;
  WINDOW *win = window->win;
  tui_clear_window(window);

  if (progress->title) {
    tui_set_color(win, TUI_COLOR_TITLE);
    tui_print_centered(win, 1, progress->title);
    tui_unset_color(win, TUI_COLOR_TITLE);
  }

  const int bar_width = window->width - 6;
  const int bar_y = window->height / 2;
  const int bar_x = 3;
  if (bar_width <= 0) {
    tui_refresh_window(window);
    return;
  }

  int current_value = progress->current_value;
  if (current_value < 0) {
    current_value = 0;
  }
  if (current_value > progress->max_value) {
    current_value = progress->max_value;
  }

  assert(progress->max_value > 0);
  double ratio = (double)current_value / (double)progress->max_value;

  const int fill_width = (int)(ratio * bar_width);

  mvwhline(win, bar_y, bar_x, ' ', bar_width);

  tui_set_color(win, TUI_COLOR_HIGHLIGHT);
  mvwhline(win, bar_y, bar_x, ' ', fill_width);
  tui_unset_color(win, TUI_COLOR_HIGHLIGHT);

  char percent[16];
  snprintf(percent, sizeof(percent), "%3d%%", (int)(ratio * 100.0));
  tui_print_centered(win, bar_y, percent);

  if (status) {
    tui_print_centered(win, bar_y + 2, status);
  }

  tui_refresh_window(window);
}

APP_NODISCARD tui_progress_t *tui_progress_create(const char *title, int max) {
  if (max <= 0) {
    return NULL;
  }

  int max_y = tui_get_max_y();
  int max_x = tui_get_max_x();
  int width = 60;
  int height = 7;
  if (max_y < height || max_x < 10) {
    return NULL;
  }
  if (width > max_x - 4) {
    width = max_x - 4;
  }
  tui_window_t *w = tui_create_centered_window(height, width);
  if (!w) {
    return NULL;
  }
  tui_draw_border(w);
  tui_set_window_title(w, title ? title : "Progress");

  tui_progress_t *progress = calloc(1, sizeof(tui_progress_t));
  if (!progress) {
    tui_destroy_window(w);
    return NULL;
  }
  progress->window = w;
  progress->max_value = max;
  progress->current_value = 0;
  progress->title = title ? strdup(title) : NULL;

  tui_progress_draw(progress, NULL);
  return progress;
}

void tui_progress_update(tui_progress_t *progress, int current,
                         const char *status) {
  if (!progress)
    return;
  progress->current_value = current;
  tui_progress_draw(progress, status);
}

void tui_progress_destroy(tui_progress_t *progress) {
  if (!progress)
    return;
  free(progress->title);
  tui_destroy_window(progress->window);
  free(progress);

  touchwin(stdscr);
  refresh();
}
