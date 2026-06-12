#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "error.h"

#define QUICK_PROFILE_MAX_PROFILES 32

typedef struct {
  char *type;
  char *mode;
  char *team_domain;
  char *audience;
} quick_iap_config_t;

typedef struct {
  bool delete;
  bool has_delete;
  bool open_after_deploy;
  bool has_open_after_deploy;
} quick_profile_deploy_t;

typedef struct {
  char *name;
  char *ssh;
  char *remote_root;
  char *base_domain;
  char *base_url;
  quick_iap_config_t iap;
  quick_profile_deploy_t deploy;
} quick_profile_t;

typedef struct {
  char *default_profile;
  quick_profile_t profiles[QUICK_PROFILE_MAX_PROFILES];
  size_t profile_count;
} quick_profile_config_t;

void quick_profile_config_init(quick_profile_config_t *config);
void quick_profile_config_destroy(quick_profile_config_t *config);
app_error quick_profile_config_load_file(const char *path,
                                         quick_profile_config_t *config);
app_error quick_profile_config_load_default(quick_profile_config_t *config);
char *quick_profile_config_default_path(void);
const quick_profile_t *quick_profile_config_find(
    const quick_profile_config_t *config, const char *name);
quick_profile_t *quick_profile_config_upsert(quick_profile_config_t *config,
                                             const char *name);
app_error quick_profile_config_write_file(const char *path,
                                          const quick_profile_config_t *config);
