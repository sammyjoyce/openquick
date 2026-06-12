#include "commands_openquick.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "../io/output.h"

static char *quick_strdup_cli(const char *value) {
  if (!value) {
    return NULL;
  }
  size_t len = strlen(value);
  char *copy = malloc(len + 1U);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len + 1U);
  return copy;
}

const char *quick_cmd_value(int argc, char *const argv[], const char *name) {
  if (!argv || !name) {
    return NULL;
  }
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      return NULL;
    }
    if (strcmp(argv[i], name) == 0) {
      return (i + 1 < argc) ? argv[i + 1] : NULL;
    }
  }
  return NULL;
}

bool quick_cmd_flag(int argc, char *const argv[], const char *name) {
  if (!argv || !name) {
    return false;
  }
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      return false;
    }
    if (strcmp(argv[i], name) == 0) {
      return true;
    }
  }
  return false;
}

static bool quick_name_in_list(const char *name, const char *const *items,
                               size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(name, items[i]) == 0) {
      return true;
    }
  }
  return false;
}

const char *quick_cmd_first_positional(int argc, char *const argv[],
                                       const char *const *value_options,
                                       size_t value_option_count) {
  bool end_options = false;
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg) {
      continue;
    }
    if (!end_options && strcmp(arg, "--") == 0) {
      end_options = true;
      continue;
    }
    if (!end_options && strncmp(arg, "--", 2) == 0) {
      if (quick_name_in_list(arg, value_options, value_option_count)) {
        i++;
      }
      continue;
    }
    return arg;
  }
  return NULL;
}

app_error quick_cmd_load_profiles(quick_profile_config_t *profiles) {
  if (!profiles) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_profile_config_init(profiles);
  const char *override = getenv("QUICK_CONFIG_PATH");
  if (override && override[0] != '\0') {
    app_error err = quick_profile_config_load_file(override, profiles);
    return err == APP_ERROR_NOT_FOUND ? APP_SUCCESS : err;
  }
  return quick_profile_config_load_default(profiles);
}

char *quick_path_join_cli(const char *a, const char *b) {
  if (!a || a[0] == '\0') {
    return quick_strdup_cli(b ? b : "");
  }
  if (!b || b[0] == '\0') {
    return quick_strdup_cli(a);
  }
  const size_t alen = strlen(a);
  const size_t blen = strlen(b);
  const bool slash = alen > 0 && a[alen - 1] != '/';
  char *out = malloc(alen + (slash ? 1U : 0U) + blen + 1U);
  if (!out) {
    return NULL;
  }
  memcpy(out, a, alen);
  size_t pos = alen;
  if (slash) {
    out[pos++] = '/';
  }
  memcpy(out + pos, b, blen);
  out[pos + blen] = '\0';
  return out;
}

bool quick_path_exists_cli(const char *path) {
  if (!path) {
    return false;
  }
#ifdef _WIN32
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fclose(f);
  return true;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

bool quick_dir_exists_cli(const char *path) {
#ifdef _WIN32
  return quick_path_exists_cli(path);
#else
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

app_error quick_mkdir_p_cli(const char *path, int mode) {
#ifdef _WIN32
  (void)path;
  (void)mode;
  return APP_SUCCESS;
#else
  if (!path) {
    return APP_ERROR_INVALID_ARG;
  }
  char *copy = quick_strdup_cli(path);
  if (!copy) {
    return APP_ERROR_MEMORY;
  }
  for (char *p = copy + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(copy, (mode_t)mode) != 0 && errno != EEXIST) {
        free(copy);
        return APP_ERROR_IO;
      }
      *p = '/';
    }
  }
  if (mkdir(copy, (mode_t)mode) != 0 && errno != EEXIST) {
    free(copy);
    return APP_ERROR_IO;
  }
  free(copy);
  return APP_SUCCESS;
#endif
}

char *quick_find_executable_cli(const char *name) {
  if (!name || name[0] == '\0') {
    return NULL;
  }
  if (strchr(name, '/')) {
    return quick_path_exists_cli(name) ? quick_strdup_cli(name) : NULL;
  }
  const char *path_env = getenv("PATH");
  if (!path_env) {
    return NULL;
  }
  char *paths = quick_strdup_cli(path_env);
  if (!paths) {
    return NULL;
  }
  char *save = NULL;
  for (char *dir = strtok_r(paths, ":", &save); dir;
       dir = strtok_r(NULL, ":", &save)) {
    char *candidate = quick_path_join_cli(dir, name);
    if (candidate && quick_path_exists_cli(candidate)) {
      free(paths);
      return candidate;
    }
    free(candidate);
  }
  free(paths);
  return NULL;
}

app_error quick_read_file_cli(const char *path, char **out) {
  if (!path || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  *out = NULL;
  FILE *stream = fopen(path, "rb");
  if (!stream) {
    return APP_ERROR_NOT_FOUND;
  }
  if (fseek(stream, 0, SEEK_END) != 0) {
    fclose(stream);
    return APP_ERROR_IO;
  }
  long size = ftell(stream);
  if (size < 0 || size > CONFIG_MAX_SIZE) {
    fclose(stream);
    return APP_ERROR_OUT_OF_RANGE;
  }
  if (fseek(stream, 0, SEEK_SET) != 0) {
    fclose(stream);
    return APP_ERROR_IO;
  }
  char *buf = malloc((size_t)size + 1U);
  if (!buf) {
    fclose(stream);
    return APP_ERROR_MEMORY;
  }
  size_t n = fread(buf, 1, (size_t)size, stream);
  fclose(stream);
  if (n != (size_t)size) {
    free(buf);
    return APP_ERROR_IO;
  }
  buf[n] = '\0';
  *out = buf;
  return APP_SUCCESS;
}

static const char *quick_json_find_field_token(const char *json,
                                               const char *field) {
  if (!json || !field) {
    return NULL;
  }
  size_t flen = strlen(field);
  const char *p = json;
  while ((p = strchr(p, '"')) != NULL) {
    p++;
    if (strncmp(p, field, flen) == 0 && p[flen] == '"') {
      const char *colon = strchr(p + flen + 1, ':');
      if (colon) {
        return colon + 1;
      }
    }
    p += strcspn(p, "\"");
  }
  return NULL;
}

char *quick_json_get_string_field_cli(const char *json, const char *field) {
  const char *p = quick_json_find_field_token(json, field);
  if (!p) {
    return NULL;
  }
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  if (strncmp(p, "null", 4) == 0) {
    return NULL;
  }
  if (*p != '"') {
    return NULL;
  }
  p++;
  size_t cap = 64;
  size_t used = 0;
  char *out = malloc(cap);
  if (!out) {
    return NULL;
  }
  while (*p != '\0' && *p != '"') {
    char ch = *p++;
    if (ch == '\\' && *p != '\0') {
      ch = *p++;
      switch (ch) {
      case 'n': ch = '\n'; break;
      case 'r': ch = '\r'; break;
      case 't': ch = '\t'; break;
      case '"': ch = '"'; break;
      case '\\': ch = '\\'; break;
      default: break;
      }
    }
    if (used + 1U >= cap) {
      char *grown = realloc(out, cap * 2U);
      if (!grown) {
        free(out);
        return NULL;
      }
      out = grown;
      cap *= 2U;
    }
    out[used++] = ch;
  }
  out[used] = '\0';
  return out;
}

long quick_json_get_long_field_cli(const char *json, const char *field,
                                   long fallback) {
  const char *p = quick_json_find_field_token(json, field);
  if (!p) {
    return fallback;
  }
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  char *end = NULL;
  long value = strtol(p, &end, 10);
  return end && end != p ? value : fallback;
}

void quick_print_error(const app_config_t *config, const char *message) {
  app_output(message ? message : "OpenQuick error", config, true);
}
