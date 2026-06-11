/*
 * tui_menu_model.c - pure-data model for tui_menu.
 *
 * No ncurses; everything is wchar_t / int / pointer math. Unit-testable.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "tui_menu_internal.h"

struct tui_menu_state {
  const tui_menu_config_t *cfg;
  wchar_t **label_w;  /* size = cfg->item_count; ampersand-stripped */
  wchar_t *mnemonics; /* size = cfg->item_count; lower-cased; 0 = none */
  int *visible;       /* size = cfg->item_count; visible items[] indices */
  int visible_count;
  int selected_visible; /* index into visible[] */
  int top_visible;
  wchar_t search_buf[64];
  size_t search_len;
  bool search_active;
};

static bool menu_item_selectable(const tui_menu_item_t *it) {
  return it && it->kind == TUI_MENU_ITEM_NORMAL && !it->disabled;
}

static int menu_first_enabled(const tui_menu_config_t *cfg) {
  for (int i = 0; i < cfg->item_count; i++) {
    if (menu_item_selectable(&cfg->items[i]))
      return i;
  }
  return -1;
}

static void menu_state_parse_labels(struct tui_menu_state *s) {
  for (int i = 0; i < s->cfg->item_count; i++) {
    const char *src = s->cfg->items[i].label;
    if (!src) {
      s->label_w[i] = calloc(1, sizeof(wchar_t));
      s->mnemonics[i] = 0;
      continue;
    }
    const size_t src_bytes = strlen(src);
    wchar_t *dst = calloc(src_bytes + 1, sizeof(wchar_t));
    if (!dst) {
      s->label_w[i] = NULL;
      s->mnemonics[i] = 0;
      continue;
    }

    size_t out = 0;
    wchar_t mnemonic = 0;
    const char *p = src;
    while (*p) {
      if (p[0] == '&' && p[1] == '&') {
        dst[out++] = L'&';
        p += 2;
        continue;
      }
      if (p[0] == '&' && p[1] != '\0' && mnemonic == 0) {
        wchar_t wc = 0;
        const int n = mbtowc(&wc, p + 1, MB_CUR_MAX);
        if (n > 0 && iswalnum(wc)) {
          mnemonic = (wchar_t)towlower(wc);
          dst[out++] = wc;
          p += 1 + (size_t)n;
          continue;
        }
      }
      wchar_t wc = 0;
      const int n = mbtowc(&wc, p, MB_CUR_MAX);
      if (n <= 0) {
        p++;
        continue;
      }
      dst[out++] = wc;
      p += n;
    }
    dst[out] = 0;
    s->label_w[i] = dst;
    s->mnemonics[i] = mnemonic;
  }
}
static bool wcs_contains_icase(const wchar_t *hay, const wchar_t *needle) {
  if (!needle || needle[0] == 0)
    return true;
  const size_t nlen = wcslen(needle);
  for (const wchar_t *p = hay; *p; p++) {
    size_t k = 0;
    while (k < nlen && p[k] != 0 && towlower(p[k]) == towlower(needle[k]))
      k++;
    if (k == nlen)
      return true;
  }
  return false;
}

static void menu_state_apply_filter(struct tui_menu_state *s) {
  s->visible_count = 0;
  if (!s->search_active || s->search_len == 0) {
    for (int i = 0; i < s->cfg->item_count; i++)
      s->visible[s->visible_count++] = i;
    return;
  }
  s->search_buf[s->search_len] = 0;
  for (int i = 0; i < s->cfg->item_count; i++) {
    if (s->cfg->items[i].kind == TUI_MENU_ITEM_SEPARATOR)
      continue;
    const wchar_t *lab = s->label_w[i] ? s->label_w[i] : L"";
    if (wcs_contains_icase(lab, s->search_buf)) {
      s->visible[s->visible_count++] = i;
    }
  }
  /* Snap selection to first selectable visible. */
  s->selected_visible = 0;
  for (int v = 0; v < s->visible_count; v++) {
    if (menu_item_selectable(&s->cfg->items[s->visible[v]])) {
      s->selected_visible = v;
      return;
    }
  }
}

tui_menu_status_t tui_menu_state_create(const tui_menu_config_t *cfg,
                                        tui_menu_state_t **out) {
  if (!cfg || !out)
    return TUI_MENU_INVALID_ARG;
  *out = NULL;
  if (!cfg->items || cfg->item_count <= 0)
    return TUI_MENU_INVALID_ARG;
  if ((size_t)cfg->item_count > SIZE_MAX / sizeof(int)) {
    return TUI_MENU_INVALID_ARG;
  }

  struct tui_menu_state *s = calloc(1, sizeof(*s));
  if (!s)
    return TUI_MENU_NO_MEMORY;
  s->cfg = cfg;

  s->visible = calloc((size_t)cfg->item_count, sizeof(int));
  s->label_w = calloc((size_t)cfg->item_count, sizeof(wchar_t *));
  s->mnemonics = calloc((size_t)cfg->item_count, sizeof(wchar_t));
  if (!s->visible || !s->label_w || !s->mnemonics) {
    tui_menu_state_destroy(s);
    return TUI_MENU_NO_MEMORY;
  }

  menu_state_parse_labels(s);
  menu_state_apply_filter(s);

  int initial = -1;
  if (cfg->default_index >= 0 && cfg->default_index < cfg->item_count &&
      menu_item_selectable(&cfg->items[cfg->default_index])) {
    initial = cfg->default_index;
  } else {
    initial = menu_first_enabled(cfg);
  }
  if (initial < 0) {
    tui_menu_state_destroy(s);
    return TUI_MENU_INVALID_ARG;
  }
  /* Find initial in visible[]. */
  for (int v = 0; v < s->visible_count; v++) {
    if (s->visible[v] == initial) {
      s->selected_visible = v;
      break;
    }
  }
  *out = s;
  return TUI_MENU_OK;
}

void tui_menu_state_destroy(tui_menu_state_t *s) {
  if (!s)
    return;
  if (s->label_w) {
    for (int i = 0; i < s->cfg->item_count; i++)
      free(s->label_w[i]);
    free(s->label_w);
  }
  free(s->mnemonics);
  free(s->visible);
  free(s);
}

int tui_menu_state_visible_count(const tui_menu_state_t *s) {
  return s ? s->visible_count : 0;
}

int tui_menu_state_selected_index(const tui_menu_state_t *s) {
  if (!s || s->visible_count == 0)
    return -1;
  return s->visible[s->selected_visible];
}

int tui_menu_state_selected_visible(const tui_menu_state_t *s) {
  return s ? s->selected_visible : -1;
}

int tui_menu_state_top_visible(const tui_menu_state_t *s) {
  return s ? s->top_visible : 0;
}

void tui_menu_state_ensure_selection_visible(tui_menu_state_t *s,
                                             int viewport_rows) {
  if (!s || viewport_rows <= 0)
    return;
  if (s->selected_visible < s->top_visible) {
    s->top_visible = s->selected_visible;
  } else if (s->selected_visible >= s->top_visible + viewport_rows) {
    s->top_visible = s->selected_visible - viewport_rows + 1;
  }
  if (s->top_visible < 0)
    s->top_visible = 0;
}

bool tui_menu_state_select_visible(tui_menu_state_t *s, int visible_row) {
  if (!s || visible_row < 0 || visible_row >= s->visible_count)
    return false;
  const int item_idx = s->visible[visible_row];
  if (!menu_item_selectable(&s->cfg->items[item_idx]))
    return false;
  s->selected_visible = visible_row;
  return true;
}

bool tui_menu_state_search_active(const tui_menu_state_t *s) {
  return s ? s->search_active : false;
}

const wchar_t *tui_menu_state_search_query(const tui_menu_state_t *s) {
  return s ? s->search_buf : L"";
}

const wchar_t *tui_menu_state_label_wcs(const tui_menu_state_t *s, int idx) {
  if (!s || idx < 0 || idx >= s->cfg->item_count)
    return L"";
  return s->label_w[idx] ? s->label_w[idx] : L"";
}

wchar_t tui_menu_state_mnemonic(const tui_menu_state_t *s, int idx) {
  if (!s || idx < 0 || idx >= s->cfg->item_count)
    return 0;
  return s->mnemonics[idx];
}

void tui_menu_state_step(tui_menu_state_t *s, int direction) {
  if (!s || s->visible_count == 0 || direction == 0)
    return;
  int next = s->selected_visible;
  for (int i = 0; i < s->visible_count; i++) {
    next += direction;
    if (next < 0)
      next = s->visible_count - 1;
    else if (next >= s->visible_count)
      next = 0;
    const int item_idx = s->visible[next];
    if (menu_item_selectable(&s->cfg->items[item_idx])) {
      s->selected_visible = next;
      return;
    }
  }
  /* All non-selectable; leave selection in place. */
}
void tui_menu_state_home(tui_menu_state_t *s) {
  if (!s || s->visible_count == 0)
    return;
  for (int v = 0; v < s->visible_count; v++) {
    if (menu_item_selectable(&s->cfg->items[s->visible[v]])) {
      s->selected_visible = v;
      return;
    }
  }
}
void tui_menu_state_end(tui_menu_state_t *s) {
  if (!s || s->visible_count == 0)
    return;
  for (int v = s->visible_count - 1; v >= 0; v--) {
    if (menu_item_selectable(&s->cfg->items[s->visible[v]])) {
      s->selected_visible = v;
      return;
    }
  }
}
void tui_menu_state_page(tui_menu_state_t *s, int direction, int page_size) {
  if (!s || page_size <= 0 || direction == 0)
    return;
  for (int i = 0; i < page_size; i++) {
    const int prev = s->selected_visible;
    tui_menu_state_step(s, direction);
    /* Stop if we wrapped (step is wrap-around but page is clamped) or
     * couldn't move. */
    if (s->selected_visible == prev)
      break;
    if (direction > 0 && s->selected_visible < prev) {
      s->selected_visible = prev;
      break;
    }
    if (direction < 0 && s->selected_visible > prev) {
      s->selected_visible = prev;
      break;
    }
  }
}
void tui_menu_state_search_open(tui_menu_state_t *s) {
  if (!s)
    return;
  s->search_active = true;
  s->search_len = 0;
  s->search_buf[0] = 0;
  menu_state_apply_filter(s);
}

void tui_menu_state_search_close(tui_menu_state_t *s) {
  if (!s)
    return;
  s->search_active = false;
  s->search_len = 0;
  s->search_buf[0] = 0;
  menu_state_apply_filter(s);
}

void tui_menu_state_search_append(tui_menu_state_t *s, wchar_t ch) {
  if (!s || !s->search_active)
    return;
  const size_t cap = (sizeof(s->search_buf) / sizeof(s->search_buf[0])) - 1;
  if (s->search_len >= cap)
    return;
  s->search_buf[s->search_len++] = ch;
  s->search_buf[s->search_len] = 0;
  menu_state_apply_filter(s);
}

void tui_menu_state_search_backspace(tui_menu_state_t *s) {
  if (!s || !s->search_active || s->search_len == 0)
    return;
  s->search_len--;
  s->search_buf[s->search_len] = 0;
  menu_state_apply_filter(s);
}
int tui_menu_state_mnemonic_jump(tui_menu_state_t *s, wchar_t key,
                                 bool *out_beep) {
  if (out_beep)
    *out_beep = false;
  if (!s || key == 0)
    return -1;
  const wchar_t target = (wchar_t)towlower(key);

  int matches[2] = {-1, -1};
  int match_count = 0;
  for (int i = 0; i < s->cfg->item_count; i++) {
    if (s->mnemonics[i] != target)
      continue;
    const tui_menu_item_t *it = &s->cfg->items[i];
    if (it->disabled || it->kind == TUI_MENU_ITEM_SEPARATOR) {
      continue;
    }
    if (match_count < 2)
      matches[match_count] = i;
    match_count++;
  }

  if (match_count == 0) {
    return -1;
  }
  if (match_count == 1) {
    return matches[0]; /* caller should auto-confirm */
  }

  /* Multiple matches - cycle selection through them, beep, no confirm. */
  const int current = tui_menu_state_selected_index(s);
  int target_visible = -1;
  bool past_current = false;
  for (int v = 0; v < s->visible_count; v++) {
    const int idx = s->visible[v];
    if (idx == current) {
      past_current = true;
      continue;
    }
    if (!past_current)
      continue;
    if (s->mnemonics[idx] == target &&
        menu_item_selectable(&s->cfg->items[idx])) {
      target_visible = v;
      break;
    }
  }
  if (target_visible < 0) {
    /* wrap to first match before current */
    for (int v = 0; v < s->visible_count; v++) {
      const int idx = s->visible[v];
      if (s->mnemonics[idx] == target &&
          menu_item_selectable(&s->cfg->items[idx])) {
        target_visible = v;
        break;
      }
    }
  }
  if (target_visible >= 0)
    s->selected_visible = target_visible;
  if (out_beep)
    *out_beep = true;
  return -1;
}
void tui_menu_state_numeric_jump(tui_menu_state_t *s, int visible_row) {
  if (!s || visible_row < 0 || visible_row >= s->visible_count)
    return;
  if (menu_item_selectable(&s->cfg->items[s->visible[visible_row]])) {
    s->selected_visible = visible_row;
  }
}

int tui_menu_state_number_for_row(const tui_menu_state_t *s, int visible_row) {
  if (!s || visible_row < 0 || visible_row >= s->visible_count)
    return 0;
  if (s->cfg->items[s->visible[visible_row]].kind == TUI_MENU_ITEM_SEPARATOR)
    return 0;
  int number = 0;
  for (int v = 0; v <= visible_row; v++) {
    if (s->cfg->items[s->visible[v]].kind != TUI_MENU_ITEM_SEPARATOR)
      number++;
  }
  return number;
}

int tui_menu_state_row_for_number(const tui_menu_state_t *s, int number) {
  if (!s || number <= 0)
    return -1;
  int seen = 0;
  for (int v = 0; v < s->visible_count; v++) {
    if (s->cfg->items[s->visible[v]].kind == TUI_MENU_ITEM_SEPARATOR)
      continue;
    if (++seen == number)
      return v;
  }
  return -1;
}

int tui_menu_state_visible_at(const tui_menu_state_t *s, int v) {
  if (!s || v < 0 || v >= s->visible_count)
    return -1;
  return s->visible[v];
}

const tui_menu_config_t *tui_menu_state_config(const tui_menu_state_t *s) {
  return s ? s->cfg : NULL;
}
