#include "tui_panel.h"

#include <stdio.h>
#include <string.h>

#include "tui_internal.h"

static void quick_tui_write(WINDOW *win, int y, int x, int width,
                            const char *text) {
  if (!win || !text || width <= 0) {
    return;
  }
  int max_y = getmaxy(win);
  int max_x = getmaxx(win);
  if (y < 0 || y >= max_y || x < 0 || x >= max_x) {
    return;
  }
  if (width > max_x - x) {
    width = max_x - x;
  }
  if (width <= 0) {
    return;
  }
  mvwaddnstr(win, y, x, text, width);
}

typedef struct {
  const quick_tui_kv_row_t *rows;
  size_t row_count;
  const char *footer;
} quick_tui_kv_state_t;

static void quick_tui_kv_redraw(tui_window_t *window, void *userdata) {
  quick_tui_kv_state_t *state = userdata;
  tui_draw_border(window);
  int y = 3;
  int key_width = 0;
  for (size_t i = 0; i < state->row_count; i++) {
    int cols = tui_display_cols(state->rows[i].key ? state->rows[i].key : "");
    if (cols > key_width) {
      key_width = cols;
    }
  }
  if (key_width > 18) {
    key_width = 18;
  }
  for (size_t i = 0; i < state->row_count && y < window->height - 3; i++) {
    const quick_tui_kv_row_t *row = &state->rows[i];
    tui_set_color(window->win, TUI_COLOR_DIM);
    quick_tui_write(window->win, y, 3, key_width, row->key ? row->key : "");
    tui_unset_color(window->win, TUI_COLOR_DIM);
    quick_tui_write(window->win, y, 3 + key_width, 2, "  ");
    tui_color_pair_t color =
        row->color == TUI_COLOR_DEFAULT ? TUI_COLOR_MENU_NORMAL : row->color;
    tui_set_color(window->win, color);
    quick_tui_write(window->win, y, 5 + key_width,
                    window->width - key_width - 8,
                    row->value ? row->value : "");
    tui_unset_color(window->win, color);
    y++;
  }
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2,
                     state->footer ? state->footer : "Enter/Esc closes");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_tui_panel_close_key(tui_window_t *window,
                                                      int ch, void *userdata) {
  (void)window;
  (void)userdata;
  return ch == '\n' || ch == KEY_ENTER || ch == 27 || ch == 'q' || ch == 'Q'
             ? TUI_MODAL_DONE
             : TUI_MODAL_CONTINUE;
}

void quick_tui_show_keyvalue_panel(const char *title,
                                   const quick_tui_kv_row_t *rows,
                                   size_t row_count, const char *footer) {
  quick_tui_kv_state_t state = {
      .rows = rows, .row_count = row_count, .footer = footer};
  int height = 8 + (int)row_count;
  if (height < 10) {
    height = 10;
  }
  (void)tui_modal_run(height, 76, title, quick_tui_kv_redraw,
                      quick_tui_panel_close_key, &state);
}

typedef struct {
  const char *const *lines;
  size_t line_count;
  const char *footer;
  int scroll;
} quick_tui_lines_state_t;

static void quick_tui_lines_redraw(tui_window_t *window, void *userdata) {
  quick_tui_lines_state_t *state = userdata;
  tui_draw_border(window);
  const int first_y = 3;
  const int rows = window->height - 6;
  if (state->scroll < 0) {
    state->scroll = 0;
  }
  if (rows > 0 && state->scroll > (int)state->line_count - rows) {
    int max_scroll = (int)state->line_count - rows;
    state->scroll = max_scroll > 0 ? max_scroll : 0;
  }
  for (int i = 0; i < rows; i++) {
    size_t index = (size_t)(state->scroll + i);
    if (index >= state->line_count) {
      break;
    }
    quick_tui_write(window->win, first_y + i, 3, window->width - 6,
                    state->lines[index] ? state->lines[index] : "");
  }
  char hint[160];
  snprintf(
      hint, sizeof(hint), "%s%s", state->footer ? state->footer : "Esc closes",
      state->line_count > (size_t)(rows > 0 ? rows : 0) ? " · ↑↓ scroll" : "");
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2, hint);
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_tui_lines_key(tui_window_t *window, int ch,
                                                void *userdata) {
  quick_tui_lines_state_t *state = userdata;
  switch (ch) {
  case KEY_UP:
  case 'k':
    if (state->scroll > 0) {
      state->scroll--;
    }
    break;
  case KEY_DOWN:
  case 'j':
    state->scroll++;
    break;
  case KEY_PPAGE:
    state->scroll -= window->height > 8 ? window->height - 8 : 1;
    break;
  case KEY_NPAGE:
    state->scroll += window->height > 8 ? window->height - 8 : 1;
    break;
  case 27:
  case 'q':
  case 'Q':
  case '\n':
  case KEY_ENTER:
    return TUI_MODAL_DONE;
  default:
    return TUI_MODAL_CONTINUE;
  }
  werase(window->win);
  quick_tui_lines_redraw(window, state);
  tui_refresh_window(window);
  return TUI_MODAL_CONTINUE;
}

void quick_tui_show_lines_panel(const char *title, const char *const *lines,
                                size_t line_count, const char *footer) {
  quick_tui_lines_state_t state = {
      .lines = lines, .line_count = line_count, .footer = footer, .scroll = 0};
  (void)tui_modal_run(18, 78, title, quick_tui_lines_redraw,
                      quick_tui_lines_key, &state);
}
