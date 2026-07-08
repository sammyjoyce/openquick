#include "site_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json_util.h"
#include "types.h"

static void quick_free_replace(char **slot, char *value) {
  if (!slot) {
    free(value);
    return;
  }
  free(*slot);
  *slot = value;
}

void quick_site_config_init(quick_site_config_t *config) {
  if (config) {
    *config = (quick_site_config_t){0};
  }
}

void quick_site_config_destroy(quick_site_config_t *config) {
  if (!config) {
    return;
  }
  free(config->name);
  free(config->source);
  free(config->output);
  free(config->build);
  free(config->profile);
  free(config->subdomain);
  free(config->routing.spa_fallback);
  free(config->sdk.import);
  *config = (quick_site_config_t){0};
}

static app_error quick_site_parse_routing(quick_site_config_t *config,
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
    if (strcmp(key, "spa_fallback") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->routing.spa_fallback, value);
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

static app_error quick_site_parse_sdk(quick_site_config_t *config,
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
    if (strcmp(key, "enabled") == 0) {
      bool value = false;
      err = quick_json_read_bool_value(&p, &value);
      if (err == APP_SUCCESS) {
        config->sdk.enabled = value;
        config->sdk.has_enabled = true;
      }
    } else if (strcmp(key, "import") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->sdk.import, value);
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

static app_error quick_site_parse(quick_site_config_t *config,
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

    if (strcmp(key, "name") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->name, value);
      }
    } else if (strcmp(key, "source") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->source, value);
      }
    } else if (strcmp(key, "output") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->output, value);
      }
    } else if (strcmp(key, "build") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->build, value);
      }
    } else if (strcmp(key, "profile") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->profile, value);
      }
    } else if (strcmp(key, "subdomain") == 0) {
      char *value = NULL;
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_free_replace(&config->subdomain, value);
      }
    } else if (strcmp(key, "routing") == 0) {
      err = quick_site_parse_routing(config, &p);
    } else if (strcmp(key, "sdk") == 0) {
      err = quick_site_parse_sdk(config, &p);
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

app_error quick_site_config_load_file(const char *path,
                                      quick_site_config_t *config) {
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

  quick_site_config_t parsed;
  quick_site_config_init(&parsed);
  app_error err = quick_site_parse(&parsed, content);
  free(content);
  if (err != APP_SUCCESS) {
    quick_site_config_destroy(&parsed);
    return err;
  }
  quick_site_config_destroy(config);
  *config = parsed;
  return APP_SUCCESS;
}

static void quick_site_write_json_string(FILE *stream, const char *value) {
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

static void quick_site_write_string_field(FILE *stream, const char *key,
                                          const char *value, bool comma) {
  fputs("  ", stream);
  quick_site_write_json_string(stream, key);
  fputs(": ", stream);
  if (value) {
    quick_site_write_json_string(stream, value);
  } else {
    fputs("null", stream);
  }
  fputs(comma ? ",\n" : "\n", stream);
}

app_error quick_site_config_write_file(const char *path,
                                       const quick_site_config_t *config) {
  if (!path || !config) {
    return APP_ERROR_INVALID_ARG;
  }
  FILE *stream = fopen(path, "wb");
  if (!stream) {
    return APP_ERROR_IO;
  }
  fputs("{\n", stream);
  quick_site_write_string_field(
      stream, "$schema", "https://openquick.dev/schemas/site.v1.json", true);
  quick_site_write_string_field(stream, "name", config->name, true);
  quick_site_write_string_field(stream, "source",
                                config->source ? config->source : ".", true);
  quick_site_write_string_field(stream, "output",
                                config->output ? config->output : ".", true);
  quick_site_write_string_field(stream, "build", config->build, true);
  quick_site_write_string_field(stream, "profile", config->profile, true);
  quick_site_write_string_field(stream, "subdomain", config->subdomain, true);
  fputs("  \"sdk\": {\n", stream);
  fprintf(stream, "    \"enabled\": %s,\n",
          (!config->sdk.has_enabled || config->sdk.enabled) ? "true" : "false");
  fputs("    \"import\": ", stream);
  quick_site_write_json_string(
      stream, config->sdk.import ? config->sdk.import : "/_quick/sdk.js");
  fputs("\n  }\n", stream);
  fputs("}\n", stream);
  if (fclose(stream) != 0) {
    return APP_ERROR_IO;
  }
  return APP_SUCCESS;
}
