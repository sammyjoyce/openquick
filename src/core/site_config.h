#pragma once

#include <stdbool.h>

#include "error.h"

typedef struct {
  bool enabled;
  bool has_enabled;
  char *import;
} quick_site_sdk_t;

typedef struct {
  char *spa_fallback;
} quick_site_routing_t;

typedef struct {
  char *name;
  char *source;
  char *output;
  char *build;
  char *profile;
  char *subdomain;
  quick_site_routing_t routing;
  quick_site_sdk_t sdk;
} quick_site_config_t;

void quick_site_config_init(quick_site_config_t *config);
void quick_site_config_destroy(quick_site_config_t *config);
app_error quick_site_config_load_file(const char *path,
                                      quick_site_config_t *config);
app_error quick_site_config_write_file(const char *path,
                                       const quick_site_config_t *config);
