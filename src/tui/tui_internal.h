/*
 * tui_internal.h - private helpers shared by TUI implementation units.
 *
 * This header is not part of the public TUI API; it exists so modal redraws
 * and menu orchestration can share state without ad-hoc extern declarations.
 */
#pragma once

#include <stddef.h>
#include <wchar.h>

#include "tui.h"

/* Display width of a wide character. POSIX wcwidth() is unavailable in the
 * Windows CRT, so approximate there: control chars are -1, the common East
 * Asian wide range (>= U+1100) is 2 columns, everything else is 1. */
#ifdef _WIN32
static inline int tui_wcwidth(wchar_t wc) {
  if (wc == 0) {
    return 0;
  }
  if (wc < 32 || (wc >= 0x7f && wc < 0xa0)) {
    return -1;
  }
  return wc >= 0x1100 ? 2 : 1;
}
#else
static inline int tui_wcwidth(wchar_t wc) {
  return wcwidth(wc);
}
#endif

/* Display columns of a NUL-terminated UTF-8 string, decoding multibyte
 * sequences and summing tui_wcwidth() (non-printable / undecodable bytes count
 * as one column). Use this to centre/align text instead of byte strlen,
 * which mis-measures multibyte glyphs. */
static inline int tui_display_cols(const char *s) {
  if (s == nullptr) {
    return 0;
  }
  int cols = 0;
  mbstate_t state = {0};
  const char *p = s;
  size_t remaining = 0;
  for (remaining = 0; p[remaining] != '\0'; remaining++) {
    /* count remaining bytes */
  }
  while (remaining > 0) {
    wchar_t wc = 0;
    size_t len = mbrtowc(&wc, p, remaining, &state);
    if (len == (size_t)-1 || len == (size_t)-2) {
      /* invalid/incomplete: treat one byte as one column and resync. */
      cols += 1;
      p += 1;
      remaining -= 1;
      state = (mbstate_t){0};
      continue;
    }
    if (len == 0) {
      break;
    }
    int w = tui_wcwidth(wc);
    cols += w < 0 ? 1 : w;
    p += len;
    remaining -= len;
  }
  return cols;
}

/* ASCII-safe uppercase: uppercases only bytes in 'a'..'z' and leaves every
 * other byte untouched, so multibyte UTF-8 sequences are preserved verbatim
 * (a plain byte-wise toupper would corrupt continuation bytes). Writes a
 * NUL-terminated copy of `src` into `dst` (capacity `dst_size`), truncating
 * on a byte boundary if needed; returns the number of bytes written
 * (excluding the terminator). */
static inline size_t tui_ascii_upper_copy(char *dst, size_t dst_size,
                                          const char *src) {
  if (dst == nullptr || dst_size == 0) {
    return 0;
  }
  size_t n = 0;
  if (src != nullptr) {
    for (; src[n] != '\0' && n + 1 < dst_size; n++) {
      const unsigned char c = (unsigned char)src[n];
      dst[n] = (c >= 'a' && c <= 'z') ? (char)(c - ('a' - 'A')) : (char)c;
    }
  }
  dst[n] = '\0';
  return n;
}

typedef enum {
  TUI_MODAL_CONTINUE,
  TUI_MODAL_DONE,
} tui_modal_decision_t;

typedef void (*tui_modal_redraw_fn)(tui_window_t *window, void *userdata);
typedef tui_modal_decision_t (*tui_modal_key_fn)(tui_window_t *window, int ch,
                                                 void *userdata);

void tui_clear_background_window(void);
tui_window_t *tui_get_background_window(void);
void tui_push_background(tui_window_t *window);
void tui_pop_background(void);
void tui_replace_background(tui_window_t *old_window, tui_window_t *new_window);

bool tui_modal_run(int height, int width, const char *title,
                   tui_modal_redraw_fn redraw, tui_modal_key_fn handle,
                   void *userdata);
