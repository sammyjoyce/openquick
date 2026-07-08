#include "profile_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "json_util.h"
#include "types.h"

static char *quick_strdup(const char *value) {
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

static void quick_free_replace(char **slot, char *value) {
  if (!slot) {
    free(value);
    return;
  }
  free(*slot);
  *slot = value;
}

static void quick_iap_destroy(quick_iap_config_t *iap) {
  if (!iap) {
    return;
  }
  free(iap->type);
  free(iap->mode);
  free(iap->team_domain);
  free(iap->audience);
  *iap = (quick_iap_config_t){0};
}

static void quick_profile_destroy(quick_profile_t *profile) {
  if (!profile) {
    return;
  }
  free(profile->name);
  free(profile->ssh);
  free(profile->remote_root);
  free(profile->base_domain);
  free(profile->base_url);
  quick_iap_destroy(&profile->iap);
  *profile = (quick_profile_t){0};
}

void quick_profile_config_init(quick_profile_config_t *config) {
  if (config) {
    *config = (quick_profile_config_t){0};
  }
}

void quick_profile_config_destroy(quick_profile_config_t *config) {
  if (!config) {
    return;
  }
  free(config->default_profile);
  for (size_t i = 0; i < config->profile_count; i++) {
    quick_profile_destroy(&config->profiles[i]);
  }
  *config = (quick_profile_config_t){0};
}

const quick_profile_t *quick_profile_config_find(
    const quick_profile_config_t *config, const char *name) {
  if (!config || !name || name[0] == '\0') {
    return NULL;
  }
  for (size_t i = 0; i < config->profile_count; i++) {
    if (config->profiles[i].name &&
        strcmp(config->profiles[i].name, name) == 0) {
      return &config->profiles[i];
    }
  }
  return NULL;
}

quick_profile_t *quick_profile_config_upsert(quick_profile_config_t *config,
                                             const char *name) {
  if (!config || !name || name[0] == '\0') {
    return NULL;
  }
  for (size_t i = 0; i < config->profile_count; i++) {
    if (config->profiles[i].name &&
        strcmp(config->profiles[i].name, name) == 0) {
      return &config->profiles[i];
    }
  }
  if (config->profile_count >= QUICK_PROFILE_MAX_PROFILES) {
    return NULL;
  }
  quick_profile_t *profile = &config->profiles[config->profile_count++];
  *profile = (quick_profile_t){0};
  profile->name = quick_strdup(name);
  if (!profile->name) {
    config->profile_count--;
    return NULL;
  }
  return profile;
}

static app_error quick_parse_iap(quick_iap_config_t *iap, const char **cursor) {
  app_error err = quick_json_expect_char(cursor, '{');
  if (err != APP_SUCCESS) {
    return err;
  }
  const char *p = quick_json_skip_ws(*cursor);
  if (*p == '}') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }
  while (*p != '\0') {
    char *key = NULL;
    err = quick_json_read_string_alloc(&p, &key);
    if (err != APP_SUCCESS) {
      return err;
    }
    err = quick_json_expect_char(&p, ':');
    if (err != APP_SUCCESS) {
      free(key);
      return err;
    }
    char *value = NULL;
    if (strcmp(key, "type") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&iap->type, value);
      }
    } else if (strcmp(key, "mode") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&iap->mode, value);
      }
    } else if (strcmp(key, "team_domain") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&iap->team_domain, value);
      }
    } else if (strcmp(key, "audience") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&iap->audience, value);
      }
    } else {
      err = quick_json_skip_value(&p, 0);
    }
    free(key);
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

static app_error quick_parse_deploy(quick_profile_deploy_t *deploy,
                                    const char **cursor) {
  app_error err = quick_json_expect_char(cursor, '{');
  if (err != APP_SUCCESS) {
    return err;
  }
  const char *p = quick_json_skip_ws(*cursor);
  if (*p == '}') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }
  while (*p != '\0') {
    char *key = NULL;
    err = quick_json_read_string_alloc(&p, &key);
    if (err != APP_SUCCESS) {
      return err;
    }
    err = quick_json_expect_char(&p, ':');
    if (err != APP_SUCCESS) {
      free(key);
      return err;
    }
    if (strcmp(key, "delete") == 0) {
      bool value = false;
      err = quick_json_read_bool_value(&p, &value);
      if (err == APP_SUCCESS) {
        deploy->delete = value;
        deploy->has_delete = true;
      }
    } else if (strcmp(key, "open_after_deploy") == 0) {
      bool value = false;
      err = quick_json_read_bool_value(&p, &value);
      if (err == APP_SUCCESS) {
        deploy->open_after_deploy = value;
        deploy->has_open_after_deploy = true;
      }
    } else {
      err = quick_json_skip_value(&p, 0);
    }
    free(key);
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

static app_error quick_parse_profile(quick_profile_t *profile,
                                     const char **cursor) {
  app_error err = quick_json_expect_char(cursor, '{');
  if (err != APP_SUCCESS) {
    return err;
  }
  const char *p = quick_json_skip_ws(*cursor);
  if (*p == '}') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }
  while (*p != '\0') {
    char *key = NULL;
    err = quick_json_read_string_alloc(&p, &key);
    if (err != APP_SUCCESS) {
      return err;
    }
    err = quick_json_expect_char(&p, ':');
    if (err != APP_SUCCESS) {
      free(key);
      return err;
    }
    if (strcmp(key, "ssh") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&profile->ssh, value);
      }
    } else if (strcmp(key, "remote_root") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&profile->remote_root, value);
      }
    } else if (strcmp(key, "base_domain") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&profile->base_domain, value);
      }
    } else if (strcmp(key, "base_url") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&profile->base_url, value);
      }
    } else if (strcmp(key, "iap") == 0) {
      err = quick_parse_iap(&profile->iap, &p);
    } else if (strcmp(key, "deploy") == 0) {
      err = quick_parse_deploy(&profile->deploy, &p);
    } else {
      err = quick_json_skip_value(&p, 0);
    }
    free(key);
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

static app_error quick_parse_profiles(quick_profile_config_t *config,
                                      const char **cursor) {
  app_error err = quick_json_expect_char(cursor, '{');
  if (err != APP_SUCCESS) {
    return err;
  }
  const char *p = quick_json_skip_ws(*cursor);
  if (*p == '}') {
    *cursor = p + 1;
    return APP_SUCCESS;
  }
  while (*p != '\0') {
    char *name = NULL;
    err = quick_json_read_string_alloc(&p, &name);
    if (err != APP_SUCCESS) {
      return err;
    }
    err = quick_json_expect_char(&p, ':');
    if (err != APP_SUCCESS) {
      free(name);
      return err;
    }
    quick_profile_t *profile = quick_profile_config_upsert(config, name);
    free(name);
    if (!profile) {
      return APP_ERROR_MEMORY;
    }
    err = quick_parse_profile(profile, &p);
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

static app_error quick_profile_parse(quick_profile_config_t *config,
                                     const char *content) {
  const char *p = quick_json_skip_ws(content);
  if (!p || *p == '\0') {
    return APP_SUCCESS;
  }
  app_error err = quick_json_expect_char(&p, '{');
  if (err != APP_SUCCESS) {
    return err;
  }
  p = quick_json_skip_ws(p);
  if (*p == '}') {
    return quick_json_finish(p + 1);
  }
  while (*p != '\0') {
    char *key = NULL;
    err = quick_json_read_string_alloc(&p, &key);
    if (err != APP_SUCCESS) {
      return err;
    }
    err = quick_json_expect_char(&p, ':');
    if (err != APP_SUCCESS) {
      free(key);
      return err;
    }
    if (strcmp(key, "default_profile") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->default_profile, value);
      }
    } else if (strcmp(key, "profiles") == 0) {
      err = quick_parse_profiles(config, &p);
    } else {
      err = quick_json_skip_value(&p, 0);
    }
    free(key);
    if (err != APP_SUCCESS) {
      return err;
    }
    p = quick_json_skip_ws(p);
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == '}') {
      return quick_json_finish(p + 1);
    }
    return APP_ERROR_CONFIG_PARSE;
  }
  return APP_ERROR_CONFIG_PARSE;
}

app_error quick_profile_config_load_file(const char *path,
                                         quick_profile_config_t *config) {
  if (!path || !config) {
    return APP_ERROR_INVALID_ARG;
  }
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
  char *content = malloc((size_t)size + 1U);
  if (!content) {
    fclose(stream);
    return APP_ERROR_MEMORY;
  }
  const size_t read_len = fread(content, 1, (size_t)size, stream);
  fclose(stream);
  if (read_len != (size_t)size) {
    free(content);
    return APP_ERROR_IO;
  }
  content[read_len] = '\0';

  quick_profile_config_t parsed;
  quick_profile_config_init(&parsed);
  app_error err = quick_profile_parse(&parsed, content);
  free(content);
  if (err != APP_SUCCESS) {
    quick_profile_config_destroy(&parsed);
    return err;
  }
  quick_profile_config_destroy(config);
  *config = parsed;
  return APP_SUCCESS;
}

char *quick_profile_config_default_path(void) {
#ifdef _WIN32
  const char *home = getenv("USERPROFILE");
  if (!home || home[0] == '\0') {
    return NULL;
  }
  const char *suffix = "\\AppData\\Local\\openquick\\config.json";
#else
  const char *xdg = getenv("XDG_CONFIG_HOME");
  const char *home = getenv("HOME");
  if (xdg && xdg[0] != '\0') {
    const char *suffix = "/openquick/config.json";
    char *path = malloc(strlen(xdg) + strlen(suffix) + 1U);
    if (!path) {
      return NULL;
    }
    sprintf(path, "%s%s", xdg, suffix);
    return path;
  }
  if (!home || home[0] == '\0') {
    return NULL;
  }
  const char *suffix = "/.config/openquick/config.json";
#endif
  char *path = malloc(strlen(home) + strlen(suffix) + 1U);
  if (!path) {
    return NULL;
  }
  sprintf(path, "%s%s", home, suffix);
  return path;
}

app_error quick_profile_config_load_default(quick_profile_config_t *config) {
  if (!config) {
    return APP_ERROR_INVALID_ARG;
  }
  char *path = quick_profile_config_default_path();
  if (!path) {
    return APP_SUCCESS;
  }
  app_error err = quick_profile_config_load_file(path, config);
  free(path);
  if (err == APP_ERROR_NOT_FOUND) {
    return APP_SUCCESS;
  }
  return err;
}

static void quick_write_json_string(FILE *stream, const char *value) {
  fputc('"', stream);
  for (const unsigned char *p = (const unsigned char *)(value ? value : "");
       *p != '\0'; p++) {
    switch (*p) {
    case '"':
      fputs("\\\"", stream);
      break;
    case '\\':
      fputs("\\\\", stream);
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
      } else {
        fputc(*p, stream);
      }
      break;
    }
  }
  fputc('"', stream);
}

static void quick_write_string_or_null(FILE *stream, const char *value) {
  if (value) {
    quick_write_json_string(stream, value);
  } else {
    fputs("null", stream);
  }
}

static app_error quick_mkdir_parent(const char *path) {
#ifndef _WIN32
  char *copy = quick_strdup(path);
  if (!copy) {
    return APP_ERROR_MEMORY;
  }
  for (char *p = copy + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
        free(copy);
        return APP_ERROR_IO;
      }
      *p = '/';
    }
  }
  free(copy);
#else
  (void)path;
#endif
  return APP_SUCCESS;
}

app_error quick_profile_config_write_file(
    const char *path, const quick_profile_config_t *config) {
  if (!path || !config) {
    return APP_ERROR_INVALID_ARG;
  }
  app_error err = quick_mkdir_parent(path);
  if (err != APP_SUCCESS) {
    return err;
  }
  FILE *stream = fopen(path, "wb");
  if (!stream) {
    return APP_ERROR_IO;
  }
  fputs("{\n", stream);
  fputs("  \"$schema\": \"https://openquick.dev/schemas/user.v1.json\",\n",
        stream);
  fputs("  \"default_profile\": ", stream);
  quick_write_string_or_null(stream, config->default_profile);
  fputs(",\n  \"profiles\": {", stream);
  if (config->profile_count > 0) {
    fputc('\n', stream);
  }
  for (size_t i = 0; i < config->profile_count; i++) {
    const quick_profile_t *profile = &config->profiles[i];
    fputs("    ", stream);
    quick_write_json_string(stream, profile->name);
    fputs(": {\n", stream);
    fputs("      \"ssh\": ", stream);
    quick_write_string_or_null(stream, profile->ssh);
    fputs(",\n      \"remote_root\": ", stream);
    quick_write_string_or_null(stream, profile->remote_root);
    fputs(",\n      \"base_domain\": ", stream);
    quick_write_string_or_null(stream, profile->base_domain);
    fputs(",\n      \"base_url\": ", stream);
    quick_write_string_or_null(stream, profile->base_url);
    fputs(",\n      \"iap\": {\n        \"type\": ", stream);
    quick_write_string_or_null(stream, profile->iap.type);
    fputs(",\n        \"mode\": ", stream);
    quick_write_string_or_null(stream, profile->iap.mode);
    fputs(",\n        \"team_domain\": ", stream);
    quick_write_string_or_null(stream, profile->iap.team_domain);
    fputs(",\n        \"audience\": ", stream);
    quick_write_string_or_null(stream, profile->iap.audience);
    fputs("\n      },\n      \"deploy\": {\n", stream);
    fprintf(stream, "        \"delete\": %s,\n",
            (!profile->deploy.has_delete || profile->deploy.delete) ? "true"
                                                                    : "false");
    fprintf(stream, "        \"open_after_deploy\": %s\n",
            (profile->deploy.has_open_after_deploy &&
             profile->deploy.open_after_deploy)
                ? "true"
                : "false");
    fputs("      }\n    }", stream);
    if (i + 1 < config->profile_count) {
      fputc(',', stream);
    }
    fputc('\n', stream);
  }
  fputs(config->profile_count > 0 ? "  }\n" : "}\n", stream);
  fputs("}\n", stream);
  if (fclose(stream) != 0) {
    return APP_ERROR_IO;
  }
  return APP_SUCCESS;
}
