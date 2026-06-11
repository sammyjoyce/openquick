/*
 * Shared low-level JSON scanning primitives. See json_scan.h.
 */

#include "json_scan.h"

#include <ctype.h>

const char *app_json_skip_ws(const char *cursor) {
  while (cursor && *cursor != '\0' && isspace((unsigned char)*cursor) != 0) {
    cursor++;
  }
  return cursor;
}

// True when ch legally terminates a JSON scalar value (separator, container
// close, whitespace, or end of input). Internal to this module — only the
// literal and number scanners below need it.
static bool app_json_value_boundary(char ch) {
  return ch == '\0' || ch == ',' || ch == '}' || ch == ']' ||
         isspace((unsigned char)ch) != 0;
}

bool app_json_match_literal(const char *cursor, const char *literal,
                            const char **end) {
  if (!cursor || !literal) {
    return false;
  }
  size_t i = 0;
  while (literal[i] != '\0') {
    if (cursor[i] == '\0' || cursor[i] != literal[i]) {
      return false;
    }
    i++;
  }
  if (!app_json_value_boundary(cursor[i])) {
    return false;
  }
  if (end) {
    *end = cursor + i;
  }
  return true;
}

app_error app_json_skip_number(const char **cursor) {
  if (!cursor || !*cursor) {
    return APP_ERROR_INVALID_ARG;
  }

  const char *p = app_json_skip_ws(*cursor);
  if (!p) {
    return APP_ERROR_INVALID_ARG;
  }
  if (*p == '-') {
    p++;
  }
  if (!isdigit((unsigned char)*p)) {
    return APP_ERROR_CONFIG_PARSE;
  }
  if (*p == '0') {
    p++;
  } else {
    while (isdigit((unsigned char)*p)) {
      p++;
    }
  }
  if (*p == '.') {
    p++;
    if (!isdigit((unsigned char)*p)) {
      return APP_ERROR_CONFIG_PARSE;
    }
    while (isdigit((unsigned char)*p)) {
      p++;
    }
  }
  if (*p == 'e' || *p == 'E') {
    p++;
    if (*p == '+' || *p == '-') {
      p++;
    }
    if (!isdigit((unsigned char)*p)) {
      return APP_ERROR_CONFIG_PARSE;
    }
    while (isdigit((unsigned char)*p)) {
      p++;
    }
  }

  if (!app_json_value_boundary(*p)) {
    return APP_ERROR_CONFIG_PARSE;
  }
  *cursor = p;
  return APP_SUCCESS;
}

app_error app_json_read_bool(const char **cursor, bool *value) {
  if (!cursor || !*cursor || !value) {
    return APP_ERROR_INVALID_ARG;
  }

  const char *p = app_json_skip_ws(*cursor);
  const char *end = NULL;
  if (app_json_match_literal(p, "true", &end)) {
    *value = true;
    *cursor = end;
    return APP_SUCCESS;
  }
  if (app_json_match_literal(p, "false", &end)) {
    *value = false;
    *cursor = end;
    return APP_SUCCESS;
  }

  return APP_ERROR_CONFIG_PARSE;
}
