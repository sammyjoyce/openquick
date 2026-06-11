#include "option_meta.h"

#include <stdio.h>
#include <string.h>

const char *app_option_normalized_long_name(const char *name) {
  if (!name) {
    return "";
  }
  return strncmp(name, "--", 2) == 0 ? name + 2 : name;
}

const char *app_option_normalized_short_name(const char *name) {
  if (!name) {
    return NULL;
  }
  if (name[0] == '-' && name[1] != '-' && name[1] != '\0') {
    return name + 1;
  }
  return name;
}

bool app_option_token_matches(const char *token, const char *long_name,
                              const char *short_name) {
  if (!token) {
    return false;
  }

  const char *normalized_long = app_option_normalized_long_name(long_name);
  const char *normalized_short = app_option_normalized_short_name(short_name);
  if (normalized_long[0] != '\0' && strncmp(token, "--", 2) == 0 &&
      strcmp(token + 2, normalized_long) == 0) {
    return true;
  }
  return normalized_short && normalized_short[0] != '\0' && token[0] == '-' &&
         token[1] != '-' && strcmp(token + 1, normalized_short) == 0;
}

static size_t appendf(char *buffer, size_t buffer_size, size_t used,
                      const char *fmt, const char *value) {
  int written = 0;
  if (buffer && used < buffer_size) {
    written = snprintf(buffer + used, buffer_size - used, fmt, value);
  } else {
    written = snprintf(NULL, 0, fmt, value);
  }
  if (written < 0) {
    return used;
  }
  return used + (size_t)written;
}

size_t app_option_format_label(char *buffer, size_t buffer_size,
                               const char *long_name, const char *short_name,
                               const app_command_arg_t *arguments,
                               size_t argument_count,
                               app_option_label_style_t style) {
  size_t used = 0;
  const char *lname = app_option_normalized_long_name(long_name);
  const char *sname = app_option_normalized_short_name(short_name);

  const bool has_long = lname[0] != '\0';
  const bool has_short = sname && sname[0];

  if (style == APP_OPTION_LABEL_USAGE) {
    used = appendf(buffer, buffer_size, used, "[--%s", lname);
  } else if (has_short && has_long) {
    used = appendf(buffer, buffer_size, used, "-%s, ", sname);
    used = appendf(buffer, buffer_size, used, "--%s", lname);
  } else if (has_long) {
    used = appendf(buffer, buffer_size, used, "--%s", lname);
  } else if (has_short) {
    used = appendf(buffer, buffer_size, used, "-%s", sname);
  }

  /* Treat a NULL argument array as zero arguments so a caller passing a stale
   * count without a pointer cannot trigger a NULL dereference. */
  const size_t arg_count = arguments ? argument_count : 0;
  for (size_t i = 0; i < arg_count; i++) {
    const app_command_arg_t *arg = &arguments[i];
    const char *name = arg->name ? arg->name : "value";
    if (arg->arity_maximum == APP_ARG_ARITY_UNBOUNDED) {
      used =
          appendf(buffer, buffer_size, used,
                  style == APP_OPTION_LABEL_CLI ? " %s..." : " <%s...>", name);
    } else {
      used = appendf(buffer, buffer_size, used,
                     style == APP_OPTION_LABEL_CLI ? " %s" : " <%s>", name);
    }
  }

  if (style == APP_OPTION_LABEL_USAGE) {
    used = appendf(buffer, buffer_size, used, "%s", "]");
  }
  if (buffer && buffer_size > 0) {
    buffer[used < buffer_size ? used : buffer_size - 1] = '\0';
  }
  return used;
}
