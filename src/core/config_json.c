/*
 * Minimal JSON reader for app configuration files.
 */

#include "config_json.h"

#include <string.h>

#include "../utils/logging.h"
#include "json_scan.h"

static app_error app_config_parse_json_string(const char **cursor, char *out,
                                              size_t out_size) {
  if (!cursor || !*cursor || !out || out_size == 0) {
    return APP_ERROR_INVALID_ARG;
  }

  const char *p = app_json_skip_ws(*cursor);
  if (!p || *p != '"') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;

  size_t used = 0;
  while (*p != '\0') {
    unsigned char ch = (unsigned char)*p++;
    if (ch == '"') {
      out[used] = '\0';
      *cursor = p;
      return APP_SUCCESS;
    }
    if (ch == '\\') {
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
      case 'u':
        // \uXXXX escapes are valid JSON but would need real UTF-8 decoding
        // to round-trip. Rather than silently replace them with '?', reject
        // the value so users see a parse error and can switch to a literal
        // UTF-8 byte sequence in their config file.
        return APP_ERROR_CONFIG_PARSE;
      default:
        return APP_ERROR_CONFIG_PARSE;
      }
    } else if (ch < 0x20) {
      return APP_ERROR_CONFIG_PARSE;
    }

    if (used + 1 >= out_size) {
      return APP_ERROR_OUT_OF_RANGE;
    }
    out[used++] = (char)ch;
  }

  return APP_ERROR_CONFIG_PARSE;
}

static app_error app_config_skip_json_string(const char **cursor) {
  if (!cursor || !*cursor) {
    return APP_ERROR_INVALID_ARG;
  }

  const char *p = app_json_skip_ws(*cursor);
  if (!p || *p != '"') {
    return APP_ERROR_CONFIG_PARSE;
  }
  p++;

  while (*p != '\0') {
    unsigned char ch = (unsigned char)*p++;
    if (ch == '"') {
      *cursor = p;
      return APP_SUCCESS;
    }
    if (ch == '\\') {
      ch = (unsigned char)*p++;
      switch (ch) {
      case '"':
      case '\\':
      case '/':
      case 'b':
      case 'f':
      case 'n':
      case 'r':
      case 't':
        break;
      case 'u':
        return APP_ERROR_CONFIG_PARSE;
      default:
        return APP_ERROR_CONFIG_PARSE;
      }
    } else if (ch < 0x20) {
      return APP_ERROR_CONFIG_PARSE;
    }
  }

  return APP_ERROR_CONFIG_PARSE;
}

static app_error app_config_skip_json_literal(const char **cursor,
                                              const char *literal) {
  const char *p = app_json_skip_ws(*cursor);
  const char *end = NULL;
  if (!app_json_match_literal(p, literal, &end)) {
    return APP_ERROR_CONFIG_PARSE;
  }
  *cursor = end;
  return APP_SUCCESS;
}

static app_error app_config_skip_json_scalar(const char **cursor) {
  const char *p = app_json_skip_ws(*cursor);
  if (!p) {
    return APP_ERROR_CONFIG_PARSE;
  }

  if (*p == '"') {
    return app_config_skip_json_string(cursor);
  }
  if (*p == 't') {
    return app_config_skip_json_literal(cursor, "true");
  }
  if (*p == 'f') {
    return app_config_skip_json_literal(cursor, "false");
  }
  if (*p == 'n') {
    return app_config_skip_json_literal(cursor, "null");
  }
  return app_json_skip_number(cursor);
}

// Bound on nested container recursion when skipping an unknown key's value.
// Matches request_json.c so the two readers tolerate the same structures.
#define APP_CONFIG_JSON_MAX_DEPTH 32

// Skip an arbitrary JSON value under an unknown config key. Known bool keys are
// still parsed strictly elsewhere; this path only discards values the loader
// does not consume, so a forward-compatible config that carries extra nested
// metadata still loads (and its known sibling flags still apply). The nesting
// skip lives here rather than in json_scan.c by design: that shared floor owns
// byte-level scanning, while each reader keeps its own object/array policy
// (config skips values allocation-free and rejects \u in strings; the headless
// reader accepts \u). See json_scan.h.
static app_error app_config_skip_json_value(const char **cursor, int depth);

static app_error app_config_skip_json_array(const char **cursor, int depth) {
  if (depth > APP_CONFIG_JSON_MAX_DEPTH) {
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
    app_error err = app_config_skip_json_value(&value_cursor, depth + 1);
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

static app_error app_config_skip_json_object(const char **cursor, int depth) {
  if (depth > APP_CONFIG_JSON_MAX_DEPTH) {
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
    app_error err = app_config_skip_json_string(&p);
    if (err != APP_SUCCESS) {
      return err;
    }

    p = app_json_skip_ws(p);
    if (*p != ':') {
      return APP_ERROR_CONFIG_PARSE;
    }
    p++;

    err = app_config_skip_json_value(&p, depth + 1);
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

static app_error app_config_skip_json_value(const char **cursor, int depth) {
  const char *p = app_json_skip_ws(*cursor);
  if (!p) {
    return APP_ERROR_CONFIG_PARSE;
  }
  if (*p == '{') {
    return app_config_skip_json_object(cursor, depth + 1);
  }
  if (*p == '[') {
    return app_config_skip_json_array(cursor, depth + 1);
  }
  return app_config_skip_json_scalar(cursor);
}

static bool app_config_apply_json_bool_key(app_config_json_state_t *state,
                                           const char *key, bool value) {
  const app_flag_spec_t *spec = app_flag_find_by_json_key(key);
  if (!spec) {
    return false;
  }

  app_flag_apply(state->values, spec->id, value);
  LOG_DEBUG("Loaded config key '%s' from file", key);
  return true;
}

static bool app_config_is_known_bool_key(const char *key) {
  return app_flag_find_by_json_key(key) != NULL;
}

static app_error app_config_finish_json_parse(const char *cursor) {
  cursor = app_json_skip_ws(cursor);
  if (!cursor || *cursor != '\0') {
    return APP_ERROR_CONFIG_PARSE;
  }

  return APP_SUCCESS;
}

app_error app_config_parse_json_state(app_config_json_state_t *staged,
                                      const char *content) {
  CHECK_NULL(staged, APP_ERROR_INVALID_ARG);
  CHECK_NULL(content, APP_ERROR_INVALID_ARG);

  const char *cursor = app_json_skip_ws(content);
  if (!cursor || *cursor == '\0') {
    return APP_SUCCESS;
  }
  if (*cursor != '{') {
    return APP_ERROR_CONFIG_PARSE;
  }

  cursor++;
  cursor = app_json_skip_ws(cursor);
  if (*cursor == '}') {
    return app_config_finish_json_parse(cursor + 1);
  }

  while (*cursor != '\0') {
    char key[64];
    app_error err = app_config_parse_json_string(&cursor, key, sizeof(key));
    if (err != APP_SUCCESS) {
      return err;
    }

    cursor = app_json_skip_ws(cursor);
    if (*cursor != ':') {
      return APP_ERROR_CONFIG_PARSE;
    }
    cursor++;

    if (app_config_is_known_bool_key(key)) {
      bool value = false;
      err = app_json_read_bool(&cursor, &value);
      if (err != APP_SUCCESS) {
        LOG_WARNING("Invalid boolean value for config key '%s'", key);
        return err;
      }
      (void)app_config_apply_json_bool_key(staged, key, value);
    } else {
      err = app_config_skip_json_value(&cursor, 0);
      if (err != APP_SUCCESS) {
        return err;
      }
    }

    cursor = app_json_skip_ws(cursor);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == '}') {
      return app_config_finish_json_parse(cursor + 1);
    }
    return APP_ERROR_CONFIG_PARSE;
  }

  return APP_ERROR_CONFIG_PARSE;
}
