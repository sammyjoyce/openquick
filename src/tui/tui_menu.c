/*
 * tui_menu.c - view + controller + orchestrator for the tui_menu module.
 *
 * The model lives in tui_menu_model.c (no ncurses). This file calls into
 * the model through tui_menu_internal.h.
 *
 * Module map:
 *   - wide-character rendering and layout helpers
 *   - keyboard/mouse input adapters over the pure menu model
 *   - blocking tui_show_menu loop with resize/background handling
 */
#ifdef _WIN32
#include <curses.h>
#else
#include <ncurses.h>
#endif

/* BUTTON5_PRESSED (mouse wheel down) is only defined when the curses build
 * advertises mouse protocol v2. The ncurses that ships with macOS predates
 * that, so referencing it unconditionally fails to compile there even though
 * NCURSES_MOUSE_VERSION is defined. Fall back to a no-op mask so wheel-down
 * handling compiles everywhere and is simply inert on v1 curses. */
#ifndef BUTTON5_PRESSED
#define BUTTON5_PRESSED 0
#endif

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "../utils/logging.h"
#include "tui.h"
#include "tui_internal.h"
#include "tui_menu.h"
#include "tui_menu_internal.h"

static void tui_menu_write_wchar(WINDOW *w, int y, int x, wchar_t wc) {
  char mb[MB_LEN_MAX];
  mbstate_t state = {0};
  size_t len = wcrtomb(mb, wc, &state);
  if (len == (size_t)-1 || len == 0) {
    mb[0] = '?';
    len = 1;
  }
  mvwaddnstr(w, y, x, mb, (int)len);
}

/* Column-correct wide-string write: budgets `cols` display columns,
 * truncates at glyph boundaries using wcwidth, never emits partial glyphs.
 */
/* Write `s` at (y,x), clipped to `cols` display columns and the window edge.
 * Returns the number of columns actually written so callers can advance their
 * cursor by what fit rather than by the string's full width. */
static int tui_menu_write_wcs(WINDOW *w, int y, int x, int cols,
                              const wchar_t *s) {
  if (!w || !s || cols <= 0)
    return 0;
  const int max_y = getmaxy(w);
  const int max_x = getmaxx(w);
  if (y < 0 || y >= max_y || x < 0 || x >= max_x)
    return 0;
  if (cols > max_x - x)
    cols = max_x - x;
  if (cols <= 0)
    return 0;

  int used = 0;
  int cur_x = x;
  for (size_t i = 0; s[i]; i++) {
    int w_cols = tui_wcwidth(s[i]);
    if (w_cols < 0)
      w_cols = 1; /* non-printable: best effort */
    if (used + w_cols > cols)
      break;
    tui_menu_write_wchar(w, y, cur_x, s[i]);
    used += w_cols;
    cur_x += w_cols;
  }
  return used;
}

typedef struct {
  tui_window_t *frame;
  bool owns_frame;
  int content_left;  /* left padding column for all content */
  int content_width; /* usable width for content */
  int title_y;       /* centered title row */
  int meta_y;        /* filter/breadcrumb + count row, or -1 when compact */
  int rule_y;        /* horizontal rule row */
  int item_area_y;   /* first list row */
  int item_area_h;   /* list rows */
  int footer_y;      /* key-hint row */
  int desired_h;
  int desired_w;
} tui_menu_layout_t;

#define MENU_MIN_ITEM_ROWS 3
#define MENU_PAD 3 /* left/right content padding (dawn-style margin) */

/* Borderless, dawn-style stack: a centered title, a meta line (breadcrumb or
 * live search) with a right-aligned count, a full-width rule, the list, then a
 * dim key-hint footer. A compact variant drops the top padding and meta line
 * so the menu still fits the documented 48x12 minimum. */
static bool tui_menu_layout_compute(tui_menu_layout_t *L, const tui_window_t *w,
                                    const tui_menu_config_t *cfg) {
  if (!L || !w || !cfg)
    return false;
  const int H = w->height;
  const int W = w->width;
  if (W < 24)
    return false;

  L->content_left = MENU_PAD;
  L->content_width = W - 2 * MENU_PAD;
  if (H >= 12) {
    L->title_y = 1;
    L->meta_y = 3;
    L->rule_y = 4;
    L->item_area_y = 6;
    L->footer_y = H - 2;
  } else {
    /* Compact variant so the 48x12 minimum still renders. */
    L->title_y = 0;
    L->meta_y = -1;
    L->rule_y = 1;
    L->item_area_y = 2;
    L->footer_y = H - 1;
  }
  L->item_area_h =
      (L->footer_y - 1) - L->item_area_y; /* blank row above footer */
  return L->item_area_h >= MENU_MIN_ITEM_ROWS;
}

static bool tui_menu_recenter_frame(tui_menu_layout_t *L) {
  if (!L || !L->frame || !L->frame->win)
    return false;

  const int max_y = getmaxy(stdscr);
  const int max_x = getmaxx(stdscr);
  int height = L->desired_h > 0 ? L->desired_h : L->frame->height;
  int width = L->desired_w > 0 ? L->desired_w : L->frame->width;
  if (height > max_y)
    height = max_y;
  if (width > max_x)
    width = max_x;
  if (height < 8 || width < 24)
    return false;

  const int y = (max_y - height) / 2;
  const int x = (max_x - width) / 2;
  if (wresize(L->frame->win, height, width) == ERR)
    return false;
  if (mvwin(L->frame->win, y, x) == ERR)
    return false;

  L->frame->height = height;
  L->frame->width = width;
  L->frame->y = y;
  L->frame->x = x;
  touchwin(stdscr);
  return true;
}

/* Count selectable choices (everything that is not a separator) for the
 * "N items" status cell. */
static int tui_menu_listable_count(const tui_menu_config_t *cfg) {
  int n = 0;
  for (int i = 0; i < cfg->item_count; i++) {
    if (cfg->items[i].kind != TUI_MENU_ITEM_SEPARATOR)
      n++;
  }
  return n;
}

/* Centered title (bold), a meta line (left: breadcrumb or live search; right:
 * count), and a full-width rule - dawn's borderless overlay header. */
static void tui_menu_render_header(const tui_menu_layout_t *L,
                                   const tui_menu_state_t *s) {
  const tui_menu_config_t *cfg = tui_menu_state_config(s);
  if (!cfg->title || !L->frame)
    return;
  WINDOW *win = L->frame->win;
  const int W = L->frame->width;
  const bool searching = tui_menu_state_search_active(s);

  /* Centered, bold, UPPERCASE title (dawn-style section header). ASCII-safe
   * upper preserves multibyte UTF-8; titles longer than the stack buffer
   * (see TUI_MENU_TITLE_MAX in tui_menu.h) are truncated with an ellipsis.
   * Centre by display columns, not byte length. */
  char up[TUI_MENU_TITLE_MAX];
  size_t tbytes = tui_ascii_upper_copy(up, sizeof(up), cfg->title);
  if (cfg->title[tbytes] != '\0' && tbytes >= 1) {
    /* Truncated: replace the trailing byte with a single-byte '~' marker. The
     * copy may have stopped mid UTF-8 sequence, so back up over any trailing
     * continuation bytes (0x80-0xBF) to a lead-byte boundary first; otherwise
     * the '~' would leave a dangling, invalid partial sequence. */
    while (tbytes >= 1 && ((unsigned char)up[tbytes - 1] & 0xC0u) == 0x80u)
      tbytes--;
    if (tbytes >= 1) {
      up[tbytes - 1] = '~';
      up[tbytes] = '\0';
    }
  }
  const int tcols = tui_display_cols(up);
  int tx = (W - tcols) / 2;
  if (tx < L->content_left)
    tx = L->content_left;
  tui_set_color(win, TUI_COLOR_TITLE);
  wattron(win, A_BOLD);
  mvwaddnstr(win, L->title_y, tx, up, L->content_width);
  wattroff(win, A_BOLD);
  tui_unset_color(win, TUI_COLOR_TITLE);

  /* Meta line: left label + right-aligned count. */
  if (L->meta_y >= 0) {
    char right[32];
    if (searching) {
      snprintf(right, sizeof(right), "%d matches",
               tui_menu_state_visible_count(s));
    } else {
      snprintf(right, sizeof(right), "%d items", tui_menu_listable_count(cfg));
    }
    const int rlen = (int)strlen(right);
    tui_set_color(win, TUI_COLOR_DIM);
    mvwaddnstr(win, L->meta_y, L->content_left + L->content_width - rlen, right,
               rlen);
    tui_unset_color(win, TUI_COLOR_DIM);

    /* Left cell shares the row with the right-aligned count: budget the
     * columns that remain after the count and a one-column gap. */
    const int left_max = L->content_width - rlen - 1;

    if (searching) {
      static const char kFindPrefix[] = "find: ";
      const int prefix_cols = (int)sizeof(kFindPrefix) - 1; /* 6 */
      char mb[64];
      const wchar_t *q = tui_menu_state_search_query(s);
      size_t n = wcstombs(mb, q, sizeof(mb) - 1);
      if (n == (size_t)-1)
        n = 0;
      mb[n] = 0;
      tui_set_color(win, TUI_COLOR_DIM);
      mvwaddnstr(win, L->meta_y, L->content_left, kFindPrefix, prefix_cols);
      tui_unset_color(win, TUI_COLOR_DIM);
      /* Bound the query so it cannot overrun the count or the cursor block:
       * reserve the prefix and one column for the reverse-video cursor. */
      int query_max = left_max - prefix_cols - 1;
      if (query_max < 0)
        query_max = 0;
      tui_set_color(win, TUI_COLOR_ACCENT);
      mvwaddnstr(win, L->meta_y, L->content_left + prefix_cols, mb, query_max);
      const int query_cols = tui_display_cols(mb);
      const int cursor_x = L->content_left + prefix_cols +
                           (query_cols < query_max ? query_cols : query_max);
      mvwaddch(win, L->meta_y, cursor_x, ' ' | A_REVERSE); /* cursor block */
      tui_unset_color(win, TUI_COLOR_ACCENT);
    } else {
      /* Surface the selected item's description on the meta row; fall back to
       * the breadcrumb subtitle when the item has none. */
      const int sel = tui_menu_state_selected_index(s);
      const char *meta = (sel >= 0 && cfg->items[sel].description &&
                          cfg->items[sel].description[0])
                             ? cfg->items[sel].description
                             : cfg->subtitle;
      if (meta && meta[0] && left_max > 0) {
        tui_set_color(win, TUI_COLOR_DIM);
        mvwaddnstr(win, L->meta_y, L->content_left, meta, left_max);
        tui_unset_color(win, TUI_COLOR_DIM);
      }
    }
  }

  /* Full-width rule. */
  tui_set_color(win, TUI_COLOR_BORDER);
  mvwhline(win, L->rule_y, L->content_left, ACS_HLINE, L->content_width);
  tui_unset_color(win, TUI_COLOR_BORDER);
}

/* Single-column, borderless list (dawn style): the selected row gets a "▸ "
 * marker in the accent colour and a bold label; every other row is dim. No
 * highlight bar - the marker and weight carry the selection. */
static void tui_menu_render_items(const tui_menu_layout_t *L,
                                  const tui_menu_state_t *s) {
  WINDOW *win = L->frame->win;
  const tui_menu_config_t *cfg = tui_menu_state_config(s);
  const int visible = tui_menu_state_visible_count(s);
  const int top = tui_menu_state_top_visible(s);
  const int sel_v = tui_menu_state_selected_visible(s);
  const int rows = L->item_area_h;
  const int marker_x = L->content_left;
  const int num_x = marker_x + 2; /* aligns under the "▸ " marker */
  const int content_right = L->content_left + L->content_width - 1;

  for (int row = 0; row < rows; row++) {
    const int v = top + row;
    if (v >= visible)
      break;
    const int idx = tui_menu_state_visible_at(s, v);
    const tui_menu_item_t *it = &cfg->items[idx];
    const int y = L->item_area_y + row;
    const bool is_selected = (v == sel_v);

    if (it->kind == TUI_MENU_ITEM_SEPARATOR) {
      tui_set_color(win, TUI_COLOR_BORDER);
      mvwhline(win, y, num_x, ACS_HLINE, L->content_width / 3);
      tui_unset_color(win, TUI_COLOR_BORDER);
      continue;
    }

    const int row_number =
        cfg->show_numeric_keys ? tui_menu_state_number_for_row(s, v) : 0;
    const bool has_number = row_number >= 1 && row_number <= 9;
    const int label_x = num_x + (has_number ? 3 : 0);
    const int disabled_suffix_cols = it->disabled ? 12 : 0;
    const int label_max_w = content_right - label_x + 1 - disabled_suffix_cols;

    /* Selection cue: an accent marker plus a bold label - no highlight bar. */
    if (is_selected) {
      tui_set_color(win, TUI_COLOR_ACCENT);
      tui_menu_write_wcs(win, y, marker_x, 2, L"▸ "); /* ▸ */
      tui_unset_color(win, TUI_COLOR_ACCENT);
    }

    if (has_number) {
      char num[4];
      snprintf(num, sizeof(num), "%d.", row_number);
      tui_set_color(win, TUI_COLOR_DIM);
      mvwaddnstr(win, y, num_x, num, 3);
      tui_unset_color(win, TUI_COLOR_DIM);
    }

    const tui_color_pair_t label_color =
        is_selected ? TUI_COLOR_MENU_NORMAL : TUI_COLOR_DIM;
    tui_set_color(win, label_color);
    if (is_selected)
      wattron(win, A_BOLD);
    const wchar_t *lab = tui_menu_state_label_wcs(s, idx);
    const wchar_t mn = tui_menu_state_mnemonic(s, idx);
    /* Render the label, applying A_UNDERLINE to the first wchar matching
     * mn (case-insensitive) - single pass. */
    int cur_x = label_x;
    int budget = label_max_w;
    bool underlined = (mn == 0);
    for (size_t k = 0; lab[k] && budget > 0; k++) {
      int cw = tui_wcwidth(lab[k]);
      if (cw < 0)
        cw = 1;
      if (cw > budget)
        break;
      const bool here = !underlined && (wchar_t)towlower(lab[k]) == mn;
      if (here)
        wattron(win, A_UNDERLINE);
      tui_menu_write_wchar(win, y, cur_x, lab[k]);
      if (here) {
        wattroff(win, A_UNDERLINE);
        underlined = true;
      }
      cur_x += cw;
      budget -= cw;
    }
    if (is_selected)
      wattroff(win, A_BOLD);
    tui_unset_color(win, label_color);

    if (it->disabled) {
      tui_set_color(win, TUI_COLOR_DIM);
      tui_menu_write_wcs(win, y, content_right - 9, 10, L"(disabled)");
      tui_unset_color(win, TUI_COLOR_DIM);
    }
  }
}

/* Scroll affordance: a dim arrow in the right-most content column of the footer
 * row. Painted AFTER the footer (which reserves that column) so the footer's
 * row clear cannot erase it. */
static void tui_menu_render_scroll(const tui_menu_layout_t *L,
                                   const tui_menu_state_t *s) {
  WINDOW *win = L->frame->win;
  const int visible = tui_menu_state_visible_count(s);
  const int top = tui_menu_state_top_visible(s);
  const int x = L->content_left + L->content_width - 1;
  const wchar_t *arrow = NULL;
  if (top > 0 && top + L->item_area_h < visible)
    arrow = L"↕"; /* up + down */
  else if (top > 0)
    arrow = L"↑"; /* up */
  else if (top + L->item_area_h < visible)
    arrow = L"↓"; /* down */
  if (arrow) {
    tui_set_color(win, TUI_COLOR_DIM);
    tui_menu_write_wcs(win, L->footer_y, x, 1, arrow);
    tui_unset_color(win, TUI_COLOR_DIM);
  }
}

/* Dim key:action footer, dawn-style. The live count lives on the meta line.
 * The right-most content column is reserved for the scroll affordance (painted
 * after this by tui_menu_render_scroll), so hints stop one column short of the
 * content edge and never collide with the indicator. */
static void tui_menu_render_footer(const tui_menu_layout_t *L,
                                   const tui_menu_state_t *s) {
  WINDOW *win = L->frame->win;
  const tui_menu_config_t *cfg = tui_menu_state_config(s);
  const int x0 = L->content_left;
  /* Reserve the last content column for the scroll indicator. */
  const int right = L->content_left + L->content_width - 2;
  int x = x0;

  tui_set_color(win, TUI_COLOR_DIM);
  mvwhline(win, L->footer_y, x0, ' ', L->content_width);

  /* Advance x by the columns actually written (clipped to the space left),
   * never by the hint's full width. Otherwise x drifts past `right` and later
   * hints render with a negative budget, overlapping or vanishing silently. */
#define MENU_HINT(str)                                                    \
  do {                                                                    \
    if (x <= right)                                                       \
      x += tui_menu_write_wcs(win, L->footer_y, x, right - x + 1, (str)); \
  } while (0)

  if (tui_menu_state_search_active(s)) {
    MENU_HINT(L"↑↓:nav");
    MENU_HINT(L"  enter:jump");
    MENU_HINT(L"  esc:cancel");
  } else {
    MENU_HINT(L"↑↓:nav");
    if (cfg->enable_search)
      MENU_HINT(L"  /:find");
    if (cfg->show_numeric_keys)
      MENU_HINT(L"  1-9:jump");
    MENU_HINT(L"  enter:select");
    MENU_HINT(L"  q:quit");
    if (cfg->enable_menu_key)
      MENU_HINT(L"  esc:menu");
  }
#undef MENU_HINT

  tui_unset_color(win, TUI_COLOR_DIM);
}
typedef enum {
  TUI_MENU_EV_NONE,
  TUI_MENU_EV_CONFIRM,
  TUI_MENU_EV_CANCEL,
  TUI_MENU_EV_MENU, /* Esc with enable_menu_key */
} tui_menu_event_t;

static tui_menu_event_t menu_handle_key_in_search(tui_menu_state_t *s, int ch) {
  switch (ch) {
  case 27: /* Esc */
    tui_menu_state_search_close(s);
    return TUI_MENU_EV_NONE;
  case '\n':
  case KEY_ENTER:
    return TUI_MENU_EV_CONFIRM;
  case KEY_BACKSPACE:
  case 127:
    tui_menu_state_search_backspace(s);
    return TUI_MENU_EV_NONE;
  default:
    if (ch >= 32 && ch < 0x7f) {
      tui_menu_state_search_append(s, (wchar_t)ch);
    }
    return TUI_MENU_EV_NONE;
  }
}

static tui_menu_event_t menu_handle_key(tui_menu_state_t *s, int ch,
                                        int page_rows, int *out_confirm_index) {
  const tui_menu_config_t *cfg = tui_menu_state_config(s);
  const int page_size = page_rows > 0 ? page_rows : MENU_MIN_ITEM_ROWS;
  if (tui_menu_state_search_active(s)) {
    return menu_handle_key_in_search(s, ch);
  }

  if (ch > 0 && ch < KEY_MIN && iswalnum((wint_t)ch)) {
    bool beep = false;
    int auto_idx = tui_menu_state_mnemonic_jump(s, (wchar_t)ch, &beep);
    if (auto_idx >= 0) {
      *out_confirm_index = auto_idx;
      return TUI_MENU_EV_CONFIRM;
    }
    if (beep) {
      tui_beep();
      return TUI_MENU_EV_NONE;
    }
  }

  switch (ch) {
  case KEY_UP:
  case 'k':
    tui_menu_state_step(s, -1);
    return TUI_MENU_EV_NONE;
  case KEY_DOWN:
  case 'j':
    tui_menu_state_step(s, 1);
    return TUI_MENU_EV_NONE;
  case KEY_HOME:
    tui_menu_state_home(s);
    return TUI_MENU_EV_NONE;
  case KEY_END:
    tui_menu_state_end(s);
    return TUI_MENU_EV_NONE;
  case KEY_PPAGE:
    tui_menu_state_page(s, -1, page_size);
    return TUI_MENU_EV_NONE;
  case KEY_NPAGE:
    tui_menu_state_page(s, 1, page_size);
    return TUI_MENU_EV_NONE;
  case '\n':
  case KEY_ENTER:
    return TUI_MENU_EV_CONFIRM;
  case 27: /* Esc */
    return cfg->enable_menu_key ? TUI_MENU_EV_MENU : TUI_MENU_EV_CANCEL;
  case 'q':
    return TUI_MENU_EV_CANCEL;
  case '/':
    if (cfg->enable_search)
      tui_menu_state_search_open(s);
    return TUI_MENU_EV_NONE;
  default:
    if (cfg->show_numeric_keys && ch >= '1' && ch <= '9') {
      const int row = tui_menu_state_row_for_number(s, ch - '0');
      const int idx = tui_menu_state_visible_at(s, row);
      if (idx < 0 || cfg->items[idx].disabled ||
          cfg->items[idx].kind == TUI_MENU_ITEM_SEPARATOR) {
        tui_beep();
      } else {
        tui_menu_state_numeric_jump(s, row);
      }
      return TUI_MENU_EV_NONE;
    }
    return TUI_MENU_EV_NONE;
  }
}

#ifdef NCURSES_MOUSE_VERSION
static tui_menu_event_t menu_handle_mouse(tui_menu_state_t *s,
                                          const tui_menu_layout_t *L,
                                          int *out_confirm_index) {
  MEVENT ev;
  if (getmouse(&ev) != OK)
    return TUI_MENU_EV_NONE;
  int wy = ev.y, wx = ev.x;
  if (!wmouse_trafo(L->frame->win, &wy, &wx, FALSE))
    return TUI_MENU_EV_NONE;

  if (ev.bstate & BUTTON4_PRESSED) {
    tui_menu_state_step(s, -1);
    return TUI_MENU_EV_NONE;
  }
  if (ev.bstate & BUTTON5_PRESSED) {
    tui_menu_state_step(s, 1);
    return TUI_MENU_EV_NONE;
  }

  if (wy < L->item_area_y || wy >= L->item_area_y + L->item_area_h) {
    return TUI_MENU_EV_NONE;
  }
  /* Only the content column is clickable. */
  if (wx < L->content_left || wx >= L->content_left + L->content_width) {
    return TUI_MENU_EV_NONE;
  }
  const int row = wy - L->item_area_y;
  const int v = tui_menu_state_top_visible(s) + row;
  if (v < 0 || v >= tui_menu_state_visible_count(s))
    return TUI_MENU_EV_NONE;
  const tui_menu_config_t *cfg = tui_menu_state_config(s);
  const int idx = tui_menu_state_visible_at(s, v);
  const tui_menu_item_t *it = &cfg->items[idx];

  if (it->kind == TUI_MENU_ITEM_SEPARATOR || it->disabled) {
    tui_beep();
    return TUI_MENU_EV_NONE;
  }
  if (ev.bstate & BUTTON1_CLICKED) {
    /* Set selection but don't confirm. */
    (void)tui_menu_state_select_visible(s, v);
    return TUI_MENU_EV_NONE;
  }
  if (ev.bstate & BUTTON1_DOUBLE_CLICKED) {
    if (!tui_menu_state_select_visible(s, v))
      return TUI_MENU_EV_NONE;
    *out_confirm_index = idx;
    return TUI_MENU_EV_CONFIRM;
  }
  return TUI_MENU_EV_NONE;
}
#endif

tui_menu_result_t tui_show_menu(tui_window_t *window,
                                const tui_menu_config_t *config) {
  tui_menu_result_t result = {.status = TUI_MENU_INVALID_ARG,
                              .selected_id = TUI_MENU_ID_NONE,
                              .selected_index = -1};
  if (!config)
    return result;

  tui_menu_state_t *state = NULL;
  tui_menu_status_t st = tui_menu_state_create(config, &state);
  if (st != TUI_MENU_OK) {
    result.status = st;
    return result;
  }

  tui_menu_layout_t L = {0};
  L.frame = window;
  L.owns_frame = (window == NULL);
  const bool has_requested_frame_size =
      config->frame_height > 0 || config->frame_width > 0;
  L.desired_h = config->frame_height > 0 ? config->frame_height
                                         : (window ? window->height : 22);
  L.desired_w = config->frame_width > 0 ? config->frame_width
                                        : (window ? window->width : 72);
  if (L.owns_frame) {
    L.frame = tui_create_centered_window(L.desired_h, L.desired_w);
    if (!L.frame) {
      tui_menu_state_destroy(state);
      result.status = TUI_MENU_TOO_SMALL;
      return result;
    }
  } else if (has_requested_frame_size && !tui_menu_recenter_frame(&L)) {
    tui_menu_state_destroy(state);
    result.status = TUI_MENU_TOO_SMALL;
    return result;
  }

  const bool pushed_background = tui_get_background_window() != L.frame;
  if (pushed_background)
    tui_push_background(L.frame);

#ifdef NCURSES_MOUSE_VERSION
  mmask_t prev_mask = 0;
  if (config->enable_mouse) {
    mousemask(BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED | BUTTON4_PRESSED |
                  BUTTON5_PRESSED,
              &prev_mask);
  }
#endif

  bool exit_loop = false;
  // Repaint only when the model actually changed. wgetch() here is blocking
  // (cbreak + keypad, no wtimeout/nodelay), so the common path already renders
  // once per key. The dirty flag matters on the signal path: SIGINT/SIGTERM
  // interrupt the blocking wgetch() (handlers installed without SA_RESTART, see
  // tui_install_signal_handlers) and return ERR; without this guard that wakeup
  // would re-erase and repaint the whole frame for no visible change. It also
  // keeps the loop correct if a future change adds an input timeout. Set true
  // on the first frame, on every processed key/mouse event, and on resize;
  // left false after a bare ERR wakeup.
  bool needs_render = true;
  while (!exit_loop) {
    if (tui_interrupted()) {
      result.status = TUI_MENU_INTERRUPTED;
      break;
    }
    if (needs_render) {
      if (!tui_menu_layout_compute(&L, L.frame, config)) {
        result.status = TUI_MENU_TOO_SMALL;
        break;
      }
      werase(L.frame->win);

      tui_menu_state_ensure_selection_visible(state, L.item_area_h);

      tui_menu_render_header(&L, state);
      tui_menu_render_items(&L, state);
      /* Footer clears its whole row (it reserves the last content column), then
       * the scroll affordance paints into that reserved column on top - so the
       * indicator survives the footer's row clear. Order matters here. */
      tui_menu_render_footer(&L, state);
      tui_menu_render_scroll(&L, state);

      wnoutrefresh(L.frame->win);
      doupdate();
      needs_render = false;
    }

    const int ch = wgetch(L.frame->win);
    int confirm_index = -1;
    tui_menu_event_t ev = TUI_MENU_EV_NONE;

    if (ch == ERR) {
      if (tui_interrupted()) {
        result.status = TUI_MENU_INTERRUPTED;
        exit_loop = true;
      } else {
        napms(10);
      }
      continue;
    }
    if (ch == KEY_RESIZE) {
      if (L.owns_frame) {
        tui_window_t *old_frame = L.frame;
        tui_window_t *new_frame =
            tui_create_centered_window(L.desired_h, L.desired_w);
        if (!new_frame) {
          tui_replace_background(old_frame, NULL);
          tui_destroy_window(old_frame);
          L.frame = NULL;
          result.status = TUI_MENU_TOO_SMALL;
          break;
        }
        L.frame = new_frame;
        tui_replace_background(old_frame, L.frame);
        tui_destroy_window(old_frame);
      } else if (!tui_menu_recenter_frame(&L)) {
        result.status = TUI_MENU_TOO_SMALL;
        break;
      }
      clear();
      refresh();
      needs_render = true;
      continue;
    }
#ifdef NCURSES_MOUSE_VERSION
    if (ch == KEY_MOUSE) {
      ev = menu_handle_mouse(state, &L, &confirm_index);
    } else
#endif
    {
      ev = menu_handle_key(state, ch, L.item_area_h, &confirm_index);
    }

    // A real key/mouse event was processed; the model may have changed (moved
    // selection, edited the search query, toggled help), so repaint next loop.
    needs_render = true;

    switch (ev) {
    case TUI_MENU_EV_CONFIRM: {
      const int idx = confirm_index >= 0 ? confirm_index
                                         : tui_menu_state_selected_index(state);
      if (idx < 0) {
        tui_beep();
        continue;
      }
      const tui_menu_item_t *it = &config->items[idx];
      if (it->disabled || it->kind == TUI_MENU_ITEM_SEPARATOR) {
        tui_beep();
        continue;
      }
      result.status = TUI_MENU_OK;
      result.selected_id = it->id;
      result.selected_index = idx;
      exit_loop = true;
      break;
    }
    case TUI_MENU_EV_CANCEL:
      result.status = TUI_MENU_CANCELLED;
      exit_loop = true;
      break;
    case TUI_MENU_EV_MENU:
      result.status = TUI_MENU_MENU;
      exit_loop = true;
      break;
    case TUI_MENU_EV_NONE:
      break;
    }
  }

#ifdef NCURSES_MOUSE_VERSION
  if (config->enable_mouse)
    mousemask(prev_mask, NULL);
#endif

  if (pushed_background)
    tui_pop_background();
  if (L.owns_frame) {
    if (L.frame)
      tui_destroy_window(L.frame);
  }
  tui_menu_state_destroy(state);
  return result;
}
