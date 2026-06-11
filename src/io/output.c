/*
 * Output handling implementation.
 */

#include "output.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/config.h"
#include "../utils/logging.h"

static void app_json_write_separator(FILE *stream, bool *needs_comma) {
  if (!stream || !needs_comma) {
    return;
  }

  if (*needs_comma) {
    fputc(',', stream);
  }
  *needs_comma = true;
}

static size_t app_utf8_sequence_len(const unsigned char *p, size_t remaining) {
  if (*p < 0x80) {
    return 1;
  }
  if (remaining >= 2 && *p >= 0xC2 && *p <= 0xDF && (p[1] & 0xC0) == 0x80) {
    return 2;
  }
  if (remaining >= 3 && *p == 0xE0 && p[1] >= 0xA0 && p[1] <= 0xBF &&
      (p[2] & 0xC0) == 0x80) {
    return 3;
  }
  if (remaining >= 3 && *p >= 0xE1 && *p <= 0xEC && (p[1] & 0xC0) == 0x80 &&
      (p[2] & 0xC0) == 0x80) {
    return 3;
  }
  if (remaining >= 3 && *p == 0xED && p[1] >= 0x80 && p[1] <= 0x9F &&
      (p[2] & 0xC0) == 0x80) {
    return 3;
  }
  if (remaining >= 3 && *p >= 0xEE && *p <= 0xEF && (p[1] & 0xC0) == 0x80 &&
      (p[2] & 0xC0) == 0x80) {
    return 3;
  }
  if (remaining >= 4 && *p == 0xF0 && p[1] >= 0x90 && p[1] <= 0xBF &&
      (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
    return 4;
  }
  if (remaining >= 4 && *p >= 0xF1 && *p <= 0xF3 && (p[1] & 0xC0) == 0x80 &&
      (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
    return 4;
  }
  if (remaining >= 4 && *p == 0xF4 && p[1] >= 0x80 && p[1] <= 0x8F &&
      (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
    return 4;
  }
  return 0;
}

void app_json_begin_object(FILE *stream) {
  if (stream) {
    fputc('{', stream);
  }
}

void app_json_end_object(FILE *stream) {
  if (stream) {
    fputc('}', stream);
  }
}

void app_json_end_line(FILE *stream) {
  if (stream) {
    fputc('\n', stream);
  }
}

void app_json_write_string(FILE *stream, const char *text) {
  if (!stream) {
    return;
  }

  if (!text) {
    fputs("null", stream);
    return;
  }

  fputc('"', stream);
  const unsigned char *end = (const unsigned char *)text + strlen(text);
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    switch (*p) {
    case '"':
      fputs("\\\"", stream);
      break;
    case '\\':
      fputs("\\\\", stream);
      break;
    case '\b':
      fputs("\\b", stream);
      break;
    case '\f':
      fputs("\\f", stream);
      break;
    case '\n':
      fputs("\\n", stream);
      break;
    case '\r':
      fputs("\\r", stream);
      break;
    case '\t':
      fputs("\\t", stream);
      break;
    default:
      if (*p < 0x20) {
        fprintf(stream, "\\u%04x", *p);
      } else if (*p >= 0x80) {
        const size_t len = app_utf8_sequence_len(p, (size_t)(end - p));
        if (len == 0) {
          fputs("\\ufffd", stream);
        } else {
          for (size_t i = 0; i < len; i++) {
            fputc(p[i], stream);
          }
          p += len - 1;
        }
      } else {
        fputc(*p, stream);
      }
      break;
    }
  }
  fputc('"', stream);
}

void app_json_write_string_field(FILE *stream, const char *key,
                                 const char *value, bool *needs_comma) {
  if (!stream || !key || !needs_comma) {
    return;
  }

  app_json_write_separator(stream, needs_comma);
  app_json_write_string(stream, key);
  fputc(':', stream);
  app_json_write_string(stream, value);
}

void app_json_write_bool_field(FILE *stream, const char *key, bool value,
                               bool *needs_comma) {
  if (!stream || !key || !needs_comma) {
    return;
  }

  app_json_write_separator(stream, needs_comma);
  app_json_write_string(stream, key);
  fputc(':', stream);
  fputs(value ? "true" : "false", stream);
}

void app_json_write_raw_field(FILE *stream, const char *key, const char *value,
                              bool *needs_comma) {
  if (!stream || !key || !value || !needs_comma) {
    return;
  }

  app_json_write_separator(stream, needs_comma);
  app_json_write_string(stream, key);
  fputc(':', stream);
  fputs(value, stream);
}

void app_json_write_indent(FILE *stream, int level) {
  if (!stream || level <= 0) {
    return;
  }

  for (int i = 0; i < level; i++) {
    fputs("  ", stream);
  }
}

static void app_json_write_pretty_suffix(FILE *stream, bool comma) {
  if (comma) {
    fputc(',', stream);
  }
  fputc('\n', stream);
}

void app_json_write_pretty_string_field(FILE *stream, int level,
                                        const char *key, const char *value,
                                        bool comma) {
  if (!stream || !key) {
    return;
  }

  app_json_write_indent(stream, level);
  app_json_write_string(stream, key);
  fputs(": ", stream);
  app_json_write_string(stream, value);
  app_json_write_pretty_suffix(stream, comma);
}

void app_json_write_pretty_bool_field(FILE *stream, int level, const char *key,
                                      bool value, bool comma) {
  if (!stream || !key) {
    return;
  }

  app_json_write_indent(stream, level);
  app_json_write_string(stream, key);
  fputs(value ? ": true" : ": false", stream);
  app_json_write_pretty_suffix(stream, comma);
}

void app_json_write_pretty_int_field(FILE *stream, int level, const char *key,
                                     int value, bool comma) {
  if (!stream || !key) {
    return;
  }

  app_json_write_indent(stream, level);
  app_json_write_string(stream, key);
  fprintf(stream, ": %d", value);
  app_json_write_pretty_suffix(stream, comma);
}

void app_json_write_pretty_null_field(FILE *stream, int level, const char *key,
                                      bool comma) {
  if (!stream || !key) {
    return;
  }

  app_json_write_indent(stream, level);
  app_json_write_string(stream, key);
  fputs(": null", stream);
  app_json_write_pretty_suffix(stream, comma);
}

void app_output(const char *text, const app_config_t *config, bool is_error) {
  if (text == nullptr || config == nullptr) {
    LOG_ERROR("Invalid parameters in app_output");
    return;
  }

  if (app_config_is_quiet(config) && !is_error) {
    return;  // Suppress non-error output in quiet mode
  }

  FILE *stream = is_error ? stderr : stdout;

  if (app_config_is_json_output(config)) {
    bool needs_comma = false;
    app_json_begin_object(stream);
    app_json_write_string_field(stream, "format_version", "1.0", &needs_comma);
    app_json_write_string_field(stream, "message", text, &needs_comma);
    app_json_end_object(stream);
    app_json_end_line(stream);
  } else {
    // Plain text output
    fprintf(stream, "%s\n", text);
  }
}

void app_output_format(const app_config_t *config, bool is_error,
                       const char *fmt, ...) {
  if (fmt == nullptr || config == nullptr) {
    LOG_ERROR("Invalid parameters in app_output_format");
    return;
  }

  if (app_config_is_quiet(config) && !is_error) {
    return;  // Suppress non-error output in quiet mode
  }

  va_list args;
  va_start(args, fmt);
  va_list args_copy;
  va_copy(args_copy, args);
  const int needed = vsnprintf(NULL, 0, fmt, args_copy);
  va_end(args_copy);
  if (needed < 0) {
    va_end(args);
    LOG_ERROR("Failed to format output");
    return;
  }

  char *buffer = malloc((size_t)needed + 1U);
  if (!buffer) {
    va_end(args);
    LOG_ERROR("Failed to allocate formatted output buffer");
    return;
  }

  const int written = vsnprintf(buffer, (size_t)needed + 1U, fmt, args);
  va_end(args);
  if (written < 0 || written > needed) {
    free(buffer);
    LOG_ERROR("Failed to format output");
    return;
  }

  app_output(buffer, config, is_error);
  free(buffer);
}
