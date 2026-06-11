#include "text_layout.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Display columns of a Unicode codepoint. Deliberately locale-independent (no
 * wcwidth/mbrtowc) so layout is identical on every platform and does not depend
 * on setlocale() picking a UTF-8 locale (Windows "" is a code page, not UTF-8).
 * Control and common combining marks are zero width; common East Asian wide and
 * fullwidth ranges are two; everything else is one. */
static int app_text_cp_columns(uint32_t cp) {
  if (cp == 0) {
    return 0;
  }
  if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0)) {
    return 0; /* C0/C1 control */
  }
  if ((cp >= 0x0300 && cp <= 0x036f) || (cp >= 0x1ab0 && cp <= 0x1aff) ||
      (cp >= 0x1dc0 && cp <= 0x1dff) || (cp >= 0x20d0 && cp <= 0x20ff) ||
      cp == 0x200b) {
    return 0; /* combining marks / zero-width space */
  }
  if ((cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0x303e) ||
      (cp >= 0x3041 && cp <= 0x33ff) || (cp >= 0x3400 && cp <= 0x4dbf) ||
      (cp >= 0x4e00 && cp <= 0x9fff) || (cp >= 0xa000 && cp <= 0xa4cf) ||
      (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff) ||
      (cp >= 0xfe30 && cp <= 0xfe4f) || (cp >= 0xff00 && cp <= 0xff60) ||
      (cp >= 0xffe0 && cp <= 0xffe6) || (cp >= 0x20000 && cp <= 0x3fffd)) {
    return 2; /* East Asian wide / fullwidth */
  }
  return 1;
}

/* Decode one UTF-8 sequence without relying on the C locale. Sets *out_bytes to
 * the sequence length and *out_columns to its display width. Returns false on
 * an invalid or truncated sequence, consuming a single byte as width 1 so
 * callers still make forward progress. */
static bool app_text_decode_one(const char *text, size_t remaining,
                                size_t *out_bytes, int *out_columns) {
  if (remaining == 0) {
    *out_bytes = 0;
    *out_columns = 0;
    return false;
  }
  const unsigned char *p = (const unsigned char *)text;
  const unsigned char c = p[0];
  uint32_t cp;
  size_t len;
  if (c < 0x80) {
    cp = c;
    len = 1;
  } else if ((c & 0xe0u) == 0xc0u) {
    cp = c & 0x1fu;
    len = 2;
  } else if ((c & 0xf0u) == 0xe0u) {
    cp = c & 0x0fu;
    len = 3;
  } else if ((c & 0xf8u) == 0xf0u) {
    cp = c & 0x07u;
    len = 4;
  } else {
    *out_bytes = 1;
    *out_columns = 1;
    return false;
  }
  if (len > remaining) {
    *out_bytes = 1;
    *out_columns = 1;
    return false;
  }
  for (size_t i = 1; i < len; i++) {
    if ((p[i] & 0xc0u) != 0x80u) {
      *out_bytes = 1;
      *out_columns = 1;
      return false;
    }
    cp = (cp << 6) | (p[i] & 0x3fu);
  }
  *out_bytes = len;
  *out_columns = app_text_cp_columns(cp);
  return true;
}

int app_text_width_utf8_n(const char *text, size_t byte_count) {
  if (!text) {
    return 0;
  }
  int columns = 0;
  size_t offset = 0;
  while (offset < byte_count && text[offset] != '\0') {
    size_t bytes = 0;
    int cell_width = 0;
    (void)app_text_decode_one(text + offset, byte_count - offset, &bytes,
                              &cell_width);
    if (bytes == 0) {
      break;
    }
    columns += cell_width;
    offset += bytes;
  }
  return columns;
}

int app_text_width_utf8(const char *text) {
  return text ? app_text_width_utf8_n(text, strlen(text)) : 0;
}

size_t app_text_truncate_utf8_columns(const char *text, int max_columns,
                                      int *out_columns) {
  if (out_columns) {
    *out_columns = 0;
  }
  if (!text || max_columns <= 0) {
    return 0;
  }

  int columns = 0;
  size_t offset = 0;
  const size_t len = strlen(text);
  while (offset < len) {
    size_t bytes = 0;
    int cell_width = 0;
    (void)app_text_decode_one(text + offset, len - offset, &bytes, &cell_width);
    if (bytes == 0 || columns + cell_width > max_columns) {
      break;
    }
    columns += cell_width;
    offset += bytes;
  }
  if (out_columns) {
    *out_columns = columns;
  }
  return offset;
}

static bool emit_empty_line(app_text_line_emit_fn emit, void *user) {
  return emit ? emit(user, "", 0, 0) : false;
}

void app_text_wrap_utf8(const char *text, int width_columns,
                        int first_indent_columns, int next_indent_columns,
                        app_text_line_emit_fn emit, void *user) {
  if (!text || !emit) {
    return;
  }
  if (width_columns <= 0) {
    (void)emit_empty_line(emit, user);
    return;
  }

  int indent = first_indent_columns < 0 ? 0 : first_indent_columns;
  const int next_indent = next_indent_columns < 0 ? 0 : next_indent_columns;
  int line_cols = 0;
  bool line_started = false;
  const char *line_start = NULL;
  size_t line_bytes = 0;
  /* Start of the current line in the source. Leading whitespace between this
   * point and the first word belongs to the line (indentation/bullets) and is
   * preserved, while inter-word space runs are measured for wrap decisions. */
  const char *segment_start = text;
  const char *word = text;

  while (*word) {
    if (*word == '\n') {
      if (!line_started) {
        /* A whitespace-only line still preserves its indentation. */
        const size_t lead_bytes = (size_t)(word - segment_start);
        if (lead_bytes > 0) {
          if (!emit(user, segment_start, lead_bytes,
                    app_text_width_utf8_n(segment_start, lead_bytes))) {
            return;
          }
        } else if (!emit_empty_line(emit, user)) {
          return;
        }
      } else if (!emit(user, line_start, line_bytes, line_cols)) {
        return;
      }
      indent = next_indent;
      line_cols = 0;
      line_started = false;
      line_start = NULL;
      line_bytes = 0;
      word++;
      segment_start = word;
      continue;
    }
    if (*word == ' ') {
      word++;
      continue;
    }

    /* Measure the run of spaces that precede this word within the current
     * line so column accounting matches the bytes actually emitted. For the
     * first word these are LEADING spaces (indentation): include them in the
     * emitted span. For later words they are inter-word separators. In both
     * cases the run starts at `segment_start` (the line origin) for the first
     * word, or at `line_start` for subsequent words. */
    const char *run_origin = line_started ? line_start : segment_start;
    int separator_cols = 0;
    {
      const char *sep = word;
      while (sep > run_origin && sep[-1] == ' ') {
        sep--;
        separator_cols++;
      }
    }

    const char *end = word;
    while (*end && *end != ' ' && *end != '\n') {
      end++;
    }
    const size_t word_bytes = (size_t)(end - word);
    const int word_cols = app_text_width_utf8_n(word, word_bytes);
    const int available = width_columns > indent ? width_columns - indent : 1;

    if (line_started && line_cols + separator_cols + word_cols > available) {
      if (!emit(user, line_start, line_bytes, line_cols)) {
        return;
      }
      indent = next_indent;
      line_cols = 0;
      line_started = false;
      line_start = NULL;
      line_bytes = 0;
      /* The wrapped word begins a fresh line with no leading indentation. */
      run_origin = word;
      separator_cols = 0;
    }

    if (!line_started) {
      /* Begin the line at the leading-whitespace origin so indentation and
       * bullet spacing are preserved in the emitted bytes and columns. */
      line_start = run_origin;
      line_bytes = (size_t)(end - run_origin);
      line_cols = separator_cols + word_cols;
      line_started = true;
    } else {
      line_bytes = (size_t)(end - line_start);
      line_cols += separator_cols + word_cols;
    }
    word = end;
  }

  if (line_started) {
    (void)emit(user, line_start, line_bytes, line_cols);
  } else {
    /* Trailing whitespace-only content preserves its indentation too. */
    const size_t lead_bytes = (size_t)(word - segment_start);
    if (lead_bytes > 0) {
      (void)emit(user, segment_start, lead_bytes,
                 app_text_width_utf8_n(segment_start, lead_bytes));
    } else {
      (void)emit_empty_line(emit, user);
    }
  }
}
