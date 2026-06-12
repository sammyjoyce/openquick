#include "json_util.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "json_scan.h"

#define QUICK_JSON_MAX_DEPTH 32

const char *quick_json_skip_ws(const char *cursor) {
  return app_json_skip_ws(cursor);
}

static app_error quick_json_grow(char **buffer, size_t *capacity) {
  if (!buffer || !*buffer || !capacity || *capacity == 0) {
    return APP_ERROR_INVALID_ARG;
  }
  if (*capacity > SIZE_MAX / 2U) {
    return APP_ERROR_OVERFLOW;
  }
  char *grown = realloc(*buffer, *capacity * 2U);
  if (!grown) {
    return APP_ERROR_MEMORY;
  }
  *buffer = grown;
  *capacity *= 2U;
  return APP_SUCCESS;
}

static int quick_json_hex(unsigned char ch) {
  if (ch >= '0' && ch <= '9') {
    return (int)(ch - '0');
  }
  if (ch >= 'a' && ch <= 'f') {
    return (int)(ch - 'a') + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return (int)(ch - 'A') + 10;
  }
  return -1;
}

static app_error quick_json_read_hex4(const char **cursor, unsigned int *out) {
  unsigned int value = 0;
  for (int i = 0; i < 4; i++) {
    const int digit = quick_json_hex((unsigned char)(*cursor)[i]);
    if (digit < 0) {
      return APP_ERROR_CONFIG_PARSE;
    }
    value = (value << 4) | (unsigned int)digit;
  }
  *cursor += 4;
  *out = value;
  return APP_SUCCESS;
}

static app_error quick_json_emit_utf8(char **buffer, size_t *capacity,
                                      size_t *used, unsigned int cp) {
  if (cp == 0 || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
    return APP_ERROR_CONFIG_PARSE;
  }

  unsigned char bytes[4];
  size_t count = 0;
  if (cp <= 0x7Fu) {
    bytes[0] = (unsigned char)cp;
    count = 1;
  } else if (cp <= 0x7FFu) {
    bytes[0] = (unsigned char)(0xC0u | (cp >> 6));
    bytes[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
    count = 2;
  } else if (cp <= 0xFFFFu) {
    bytes[0] = (unsigned char)(0xE0u | (cp >> 12));
    bytes[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    bytes[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
    count = 3;
  } else {
    bytes[0] = (unsigned char)(0xF0u | (cp >> 18));
    bytes[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
    bytes[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
    bytes[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
    count = 4;
  }

  for (size_t i = 0; i < count; i++) {
    if (*used + 1U >= *capacity) {
      const app_error err = quick_json_grow(buffer, capacity);
      if (err != APP_SUCCESS) {
        return err;
      }
    }
    (*buffer)[(*used)++] = (char)bytes[i];
  }
  return APP_SUCCESS;
}

static app_error quick_json_decode_unicode(const char **cursor,
                                           unsigned int *out_cp) {
  const char *p = *cursor;
  unsigned int unit = 0;
  app_error err = quick_json_read_hex4(&p, &unit);
  if (err != APP_SUCCESS) {
    return err;
  }

  if (unit >= 0xD800u && unit <= 0xDBFFu) {
    if (p[0] != '\\' || p[1] != 'u') {
      return APP_ERROR_CONFIG_PARSE;
    }
    p += 2;
    unsigned int low = 0;
    err = quick_json_read_hex4(&p, &low);
    if (err != APP_SUCCESS) {
      return err;
    }
    if (low < 0xDC00u || low > 0xDFFFu) {
      return APP_ERROR_CONFIG_PARSE;
    }
    *out_cp = 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u);
  } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
    return APP_ERROR_CONFIG_PARSE;
  } else {
    *out_cp = unit;
  }
  *cursor = p;
  return APP_SUCCESS;
}

app_error quick_json_read_string_alloc(const char **cursor, char **out) {
  if (!cursor || !*cursor || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  *out = NULL;
  const char *p = quick_json_skip_ws(*cursor);
  if (!p || *p != '"') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;

  size_t capacity = 32;
  size_t used = 0;
  char *buffer = malloc(capacity);
  if (!buffer) {
    return APP_ERROR_MEMORY;
  }

  while (*p != '\0') {
    unsigned char ch = (unsigned char)*p++;
    if (ch == '"') {
      buffer[used] = '\0';
      *cursor = p;
      *out = buffer;
      return APP_SUCCESS;
    }
    if (ch == '\\') {
      if (*p == '\0') {
        free(buffer);
        return APP_ERROR_CONFIG_PARSE;
      }
      ch = (unsigned char)*p++;
      switch (ch) {
      case '"':
      case '\\':
      case '/':
        break;
      case 'b':
        ch = '\b';
        break;
      case 'f':
        ch = '\f';
        break;
      case 'n':
        ch = '\n';
        break;
      case 'r':
        ch = '\r';
        break;
      case 't':
        ch = '\t';
        break;
      case 'u': {
        unsigned int cp = 0;
        app_error err = quick_json_decode_unicode(&p, &cp);
        if (err != APP_SUCCESS) {
          free(buffer);
          return err;
        }
        err = quick_json_emit_utf8(&buffer, &capacity, &used, cp);
        if (err != APP_SUCCESS) {
          free(buffer);
          return err;
        }
        continue;
      }
      default:
        free(buffer);
        return APP_ERROR_CONFIG_PARSE;
      }
    } else if (ch < 0x20) {
      free(buffer);
      return APP_ERROR_CONFIG_PARSE;
    }

    if (used + 1U >= capacity) {
      const app_error err = quick_json_grow(&buffer, &capacity);
      if (err != APP_SUCCESS) {
        free(buffer);
        return err;
      }
    }
    buffer[used++] = (char)ch;
  }

  free(buffer);
  return APP_ERROR_CONFIG_PARSE;
}

app_error quick_json_read_bool_value(const char **cursor, bool *out) {
  return app_json_read_bool(cursor, out);
}

app_error quick_json_read_string_or_null(const char **cursor, char **out) {
  if (!cursor || !*cursor || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  const char *p = quick_json_skip_ws(*cursor);
  const char *end = NULL;
  if (app_json_match_literal(p, "null", &end)) {
    *cursor = end;
    *out = NULL;
    return APP_SUCCESS;
  }
  return quick_json_read_string_alloc(cursor, out);
}

static app_error quick_json_skip_array(const char **cursor, int depth) {
  if (depth > QUICK_JSON_MAX_DEPTH) {
    return APP_ERROR_OUT_OF_RANGE;
  }
  const char *p = quick_json_skip_ws(*cursor);
  if (!p || *p != '[') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;
  p = quick_json_skip_ws(p);
  if (*p == ']') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }
  while (*p != '\0') {
    const char *value = p;
    app_error err = quick_json_skip_value(&value, depth + 1);
    if (err != APP_SUCCESS) {
      return err;
    }
    p = quick_json_skip_ws(value);
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == ']') {
      *cursor = p + 1;
      return APP_SUCCESS;
    }
    return APP_ERROR_CONFIG_PARSE;
  }
  return APP_ERROR_CONFIG_PARSE;
}

static app_error quick_json_skip_object(const char **cursor, int depth) {
  if (depth > QUICK_JSON_MAX_DEPTH) {
    return APP_ERROR_OUT_OF_RANGE;
  }
  const char *p = quick_json_skip_ws(*cursor);
  if (!p || *p != '{') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;
  p = quick_json_skip_ws(p);
  if (*p == '}') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }
  while (*p != '\0') {
    char *key = NULL;
    app_error err = quick_json_read_string_alloc(&p, &key);
    if (err != APP_SUCCESS) {
      return err;
    }
    free(key);
    p = quick_json_skip_ws(p);
    if (*p != ':') {
      return APP_ERROR_CONFIG_PARSE;
    }
    p++;
    err = quick_json_skip_value(&p, depth + 1);
    if (err != APP_SUCCESS) {
      return err;
    }
    p = quick_json_skip_ws(p);
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == '}') {
      *cursor = p + 1;
      return APP_SUCCESS;
    }
    return APP_ERROR_CONFIG_PARSE;
  }
  return APP_ERROR_CONFIG_PARSE;
}

app_error quick_json_skip_value(const char **cursor, int depth) {
  if (!cursor || !*cursor) {
    return APP_ERROR_INVALID_ARG;
  }
  const char *p = quick_json_skip_ws(*cursor);
  if (!p) {
    return APP_ERROR_CONFIG_PARSE;
  }
  if (*p == '"') {
    char *value = NULL;
    app_error err = quick_json_read_string_alloc(&p, &value);
    free(value);
    if (err == APP_SUCCESS) {
      *cursor = p;
    }
    return err;
  }
  if (*p == '{') {
    return quick_json_skip_object(cursor, depth + 1);
  }
  if (*p == '[') {
    return quick_json_skip_array(cursor, depth + 1);
  }
  const char *end = NULL;
  if (app_json_match_literal(p, "true", &end) ||
      app_json_match_literal(p, "false", &end) ||
      app_json_match_literal(p, "null", &end)) {
    *cursor = end;
    return APP_SUCCESS;
  }
  return app_json_skip_number(cursor);
}

app_error quick_json_expect_char(const char **cursor, char ch) {
  if (!cursor || !*cursor) {
    return APP_ERROR_INVALID_ARG;
  }
  const char *p = quick_json_skip_ws(*cursor);
  if (!p || *p != ch) {
    return APP_ERROR_CONFIG_PARSE;
  }
  *cursor = p + 1;
  return APP_SUCCESS;
}

app_error quick_json_finish(const char *cursor) {
  cursor = quick_json_skip_ws(cursor);
  if (!cursor || *cursor != '\0') {
    return APP_ERROR_CONFIG_PARSE;
  }
  return APP_SUCCESS;
}
