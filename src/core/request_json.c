/*
 * Minimal JSON reader for the headless request protocol.
 */

#include "request_json.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../utils/logging.h"
#include "json_scan.h"

#define APP_REQUEST_JSON_MAX_DEPTH 32

static app_error app_request_grow_string(char **buffer, size_t *capacity) {
  if (!buffer || !*buffer || !capacity || *capacity == 0) {
    return APP_ERROR_INVALID_ARG;
  }
  if (*capacity > SIZE_MAX / 2U) {
    return APP_ERROR_OVERFLOW;
  }

  const size_t new_capacity = *capacity * 2U;
  char *grown = realloc(*buffer, new_capacity);
  if (!grown) {
    return APP_ERROR_MEMORY;
  }

  *buffer = grown;
  *capacity = new_capacity;
  return APP_SUCCESS;
}

// Decode a single hex digit, or -1 when the byte is not [0-9A-Fa-f].
static int app_request_hex_digit(unsigned char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Read exactly four hex digits at *p into *out, advancing *p past them. Stops
// (and fails) at the first non-hex byte, including the NUL terminator, so it
// never reads past end of input.
static app_error app_request_read_hex4(const char **p, unsigned int *out) {
  unsigned int value = 0;
  for (int i = 0; i < 4; i++) {
    const int digit = app_request_hex_digit((unsigned char)(*p)[i]);
    if (digit < 0) {
      return APP_ERROR_CONFIG_PARSE;
    }
    value = (value << 4) | (unsigned int)digit;
  }
  *p += 4;
  *out = value;
  return APP_SUCCESS;
}

// Decode a \uXXXX escape (the cursor sits just past the 'u') into a Unicode
// scalar value, combining a UTF-16 surrogate pair when present. Lone or
// inverted surrogates are rejected as malformed, matching RFC 8259.
static app_error app_request_decode_unicode_escape(const char **cursor,
                                                   unsigned int *out_cp) {
  const char *p = *cursor;
  unsigned int unit = 0;
  app_error err = app_request_read_hex4(&p, &unit);
  if (err != APP_SUCCESS) {
    return err;
  }

  if (unit >= 0xD800u && unit <= 0xDBFFu) {
    // High surrogate: must be followed by a \uDC00-\uDFFF low surrogate.
    if (p[0] != '\\' || p[1] != 'u') {
      return APP_ERROR_CONFIG_PARSE;
    }
    p += 2;
    unsigned int low = 0;
    err = app_request_read_hex4(&p, &low);
    if (err != APP_SUCCESS) {
      return err;
    }
    if (low < 0xDC00u || low > 0xDFFFu) {
      return APP_ERROR_CONFIG_PARSE;
    }
    *out_cp = 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u);
  } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
    return APP_ERROR_CONFIG_PARSE;  // lone low surrogate
  } else {
    *out_cp = unit;
  }

  // Reject an escaped NUL: args are NUL-terminated C strings, so emitting a
  // 0x00 byte would silently truncate the value. This mirrors the rejection of
  // an unescaped control byte (ch < 0x20) in the string scanner above. Other
  // escaped controls (e.g. 	) remain valid and decode to their byte.
  if (*out_cp == 0u) {
    return APP_ERROR_CONFIG_PARSE;
  }

  *cursor = p;
  return APP_SUCCESS;
}

// UTF-8-encode a scalar value into the growing string buffer, growing it byte
// by byte through the same path string accumulation uses so the trailing NUL
// always fits.
static app_error app_request_emit_utf8(char **buffer, size_t *capacity,
                                       size_t *used, unsigned int cp) {
  unsigned char bytes[4];
  size_t count;
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
      const app_error err = app_request_grow_string(buffer, capacity);
      if (err != APP_SUCCESS) {
        return err;
      }
    }
    (*buffer)[(*used)++] = (char)bytes[i];
  }
  return APP_SUCCESS;
}

static app_error app_request_parse_json_string_alloc(const char **cursor,
                                                     char **out) {
  if (!cursor || !*cursor || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  *out = NULL;

  const char *p = app_json_skip_ws(*cursor);
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
        // \uXXXX is how standard serializers (jq, JSON.stringify, Python's
        // json.dumps with ensure_ascii) emit non-ASCII and control characters,
        // so the headless machine surface must accept it. Decode the scalar
        // value (combining surrogate pairs) and emit its UTF-8 bytes directly,
        // bypassing the single-byte append below.
        unsigned int code_point = 0;
        app_error uerr = app_request_decode_unicode_escape(&p, &code_point);
        if (uerr != APP_SUCCESS) {
          free(buffer);
          return uerr;
        }
        uerr = app_request_emit_utf8(&buffer, &capacity, &used, code_point);
        if (uerr != APP_SUCCESS) {
          free(buffer);
          return uerr;
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
      const app_error err = app_request_grow_string(&buffer, &capacity);
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

static app_error app_request_skip_json_value(const char **cursor, int depth);

static app_error app_request_skip_json_array(const char **cursor, int depth) {
  if (depth > APP_REQUEST_JSON_MAX_DEPTH) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  const char *p = app_json_skip_ws(*cursor);
  if (!p || *p != '[') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;
  p = app_json_skip_ws(p);
  if (*p == ']') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }

  while (*p != '\0') {
    const char *value_cursor = p;
    app_error err = app_request_skip_json_value(&value_cursor, depth + 1);
    if (err != APP_SUCCESS) {
      return err;
    }
    p = app_json_skip_ws(value_cursor);
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

static app_error app_request_skip_json_object(const char **cursor, int depth) {
  if (depth > APP_REQUEST_JSON_MAX_DEPTH) {
    return APP_ERROR_OUT_OF_RANGE;
  }

  const char *p = app_json_skip_ws(*cursor);
  if (!p || *p != '{') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;
  p = app_json_skip_ws(p);
  if (*p == '}') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }

  while (*p != '\0') {
    char *key = NULL;
    app_error err = app_request_parse_json_string_alloc(&p, &key);
    if (err != APP_SUCCESS) {
      return err;
    }
    free(key);

    p = app_json_skip_ws(p);
    if (*p != ':') {
      return APP_ERROR_CONFIG_PARSE;
    }
    p++;

    err = app_request_skip_json_value(&p, depth + 1);
    if (err != APP_SUCCESS) {
      return err;
    }
    p = app_json_skip_ws(p);
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

static app_error app_request_skip_json_value(const char **cursor, int depth) {
  const char *p = app_json_skip_ws(*cursor);
  if (!p) {
    return APP_ERROR_CONFIG_PARSE;
  }

  if (*p == '"') {
    char *value = NULL;
    const app_error err = app_request_parse_json_string_alloc(&p, &value);
    free(value);
    if (err == APP_SUCCESS) {
      *cursor = p;
    }
    return err;
  }
  if (*p == '{') {
    return app_request_skip_json_object(cursor, depth + 1);
  }
  if (*p == '[') {
    return app_request_skip_json_array(cursor, depth + 1);
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

static void app_request_record_flag(app_request_t *request, app_flag_id id,
                                    bool value) {
  app_flag_apply(request->flag_values, id, value);
  request->flag_seen[id] = true;

  if (!value) {
    return;
  }

  size_t count = 0;
  const app_flag_spec_t *specs = app_flag_table(&count);
  for (size_t i = 0; i < count; i++) {
    if (specs[i].id != id) {
      continue;
    }
    for (app_flag_id other = 0; other < APP_FLAG_COUNT; other++) {
      if ((specs[i].exclusive_mask & APP_FLAG_MASK(other)) != 0) {
        request->flag_seen[other] = false;
      }
    }
    return;
  }
}

static app_error app_request_parse_args_array(app_request_t *request,
                                              const char **cursor) {
  const char *p = app_json_skip_ws(*cursor);
  if (!p || *p != '[') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;
  p = app_json_skip_ws(p);
  if (*p == ']') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }

  while (*p != '\0') {
    if (request->arg_count >= APP_MAX_COMMAND_ARGS) {
      return APP_ERROR_OUT_OF_RANGE;
    }

    char *arg = NULL;
    app_error err = app_request_parse_json_string_alloc(&p, &arg);
    if (err != APP_SUCCESS) {
      return err;
    }
    request->args[request->arg_count++] = arg;

    p = app_json_skip_ws(p);
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

static app_error app_request_parse_flags_object(app_request_t *request,
                                                const char **cursor) {
  const char *p = app_json_skip_ws(*cursor);
  if (!p || *p != '{') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;
  p = app_json_skip_ws(p);
  if (*p == '}') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }

  while (*p != '\0') {
    char *key = NULL;
    app_error err = app_request_parse_json_string_alloc(&p, &key);
    if (err != APP_SUCCESS) {
      return err;
    }

    p = app_json_skip_ws(p);
    if (*p != ':') {
      free(key);
      return APP_ERROR_CONFIG_PARSE;
    }
    p++;

    bool value = false;
    err = app_json_read_bool(&p, &value);
    if (err != APP_SUCCESS) {
      free(key);
      return err;
    }

    const app_flag_spec_t *spec = app_flag_find_by_json_key(key);
    if (!spec) {
      free(key);
      return APP_ERROR_UNKNOWN_OPTION;
    }
    app_request_record_flag(request, spec->id, value);
    free(key);

    p = app_json_skip_ws(p);
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

static app_error app_request_finish_json_parse(const char *cursor) {
  cursor = app_json_skip_ws(cursor);
  if (!cursor || *cursor != '\0') {
    return APP_ERROR_CONFIG_PARSE;
  }
  return APP_SUCCESS;
}

void app_request_init(app_request_t *request) {
  if (request) {
    *request = (app_request_t){0};
  }
}

void app_request_destroy(app_request_t *request) {
  if (!request) {
    return;
  }

  free(request->command);
  request->command = NULL;
  for (size_t i = 0; i < request->arg_count; i++) {
    free(request->args[i]);
    request->args[i] = NULL;
  }
  request->arg_count = 0;
}

app_error app_request_parse_json(app_request_t *request, const char *content) {
  CHECK_NULL(request, APP_ERROR_INVALID_ARG);
  CHECK_NULL(content, APP_ERROR_INVALID_ARG);

  const char *cursor = app_json_skip_ws(content);
  if (!cursor || *cursor == '\0') {
    return APP_ERROR_CONFIG_PARSE;
  }
  if (*cursor != '{') {
    return APP_ERROR_CONFIG_PARSE;
  }

  cursor++;
  cursor = app_json_skip_ws(cursor);
  if (*cursor == '}') {
    cursor++;
    const app_error err = app_request_finish_json_parse(cursor);
    return err == APP_SUCCESS ? APP_ERROR_MISSING_ARG : err;
  }

  while (*cursor != '\0') {
    char *key = NULL;
    app_error err = app_request_parse_json_string_alloc(&cursor, &key);
    if (err != APP_SUCCESS) {
      return err;
    }

    cursor = app_json_skip_ws(cursor);
    if (*cursor != ':') {
      free(key);
      return APP_ERROR_CONFIG_PARSE;
    }
    cursor++;

    if (strcmp(key, "command") == 0) {
      char *command = NULL;
      err = app_request_parse_json_string_alloc(&cursor, &command);
      if (err == APP_SUCCESS) {
        free(request->command);
        request->command = command;
      }
    } else if (strcmp(key, "args") == 0) {
      err = app_request_parse_args_array(request, &cursor);
    } else if (strcmp(key, "flags") == 0) {
      err = app_request_parse_flags_object(request, &cursor);
    } else {
      err = app_request_skip_json_value(&cursor, 0);
    }
    free(key);
    if (err != APP_SUCCESS) {
      return err;
    }

    cursor = app_json_skip_ws(cursor);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == '}') {
      cursor++;
      err = app_request_finish_json_parse(cursor);
      if (err != APP_SUCCESS) {
        return err;
      }
      return request->command && request->command[0] != '\0'
                 ? APP_SUCCESS
                 : APP_ERROR_MISSING_ARG;
    }
    return APP_ERROR_CONFIG_PARSE;
  }

  return APP_ERROR_CONFIG_PARSE;
}

app_error app_request_apply_to_config(const app_request_t *request,
                                      app_config_t *config) {
  CHECK_NULL(request, APP_ERROR_INVALID_ARG);
  CHECK_NULL(config, APP_ERROR_INVALID_ARG);
  if (!request->command || request->command[0] == '\0') {
    return APP_ERROR_MISSING_ARG;
  }

  app_error err = app_config_set_command(config, request->command);
  if (err != APP_SUCCESS) {
    return err;
  }

  for (size_t i = 0; i < request->arg_count; i++) {
    err = app_config_add_command_arg(config, request->args[i]);
    if (err != APP_SUCCESS) {
      return err;
    }
  }

  for (app_flag_id id = 0; id < APP_FLAG_COUNT; id++) {
    if (!request->flag_seen[id]) {
      continue;
    }
    err = app_config_set_flag(config, id, request->flag_values[id]);
    if (err != APP_SUCCESS) {
      return err;
    }
  }

  return APP_SUCCESS;
}
