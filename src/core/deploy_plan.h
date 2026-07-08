#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "error.h"
#include "profile_config.h"
#include "site_config.h"

#define QUICK_SLUG_MAX 63
#define QUICK_IGNORE_MAX_PATTERNS 128

typedef struct {
  const char *site;
  const char *subdomain;
  const char *profile;
  const char *remote;
  const char *base_domain;
  const char *path;
} quick_plan_overrides_t;

typedef struct {
  char *site_root;
  char *quick_json_path;
  char *source_dir;
  char *output_dir;
  char site[QUICK_SLUG_MAX + 1];
  char subdomain[QUICK_SLUG_MAX + 1];
  char profile[128];
  char *ssh;
  char *remote_root;
  char *base_domain;
  char *base_url;
  char *url;
  quick_site_config_t site_config;
} quick_deploy_plan_t;

typedef struct {
  char *patterns[QUICK_IGNORE_MAX_PATTERNS];
  size_t count;
} quick_ignore_t;

typedef struct {
  char *profile;
  char *site;
  char *url;
  char *release;
  char *deployed_at;
  bool stale;
} quick_deployment_record_t;

app_error quick_slug_normalize(const char *input, char out[QUICK_SLUG_MAX + 1]);
bool quick_slug_is_valid(const char *slug);
bool quick_profile_name_is_safe(const char *name);
bool quick_ssh_target_is_safe(const char *target);
bool quick_remote_path_is_safe(const char *path);
bool quick_domain_is_safe(const char *domain);

void quick_deploy_plan_init(quick_deploy_plan_t *plan);
void quick_deploy_plan_destroy(quick_deploy_plan_t *plan);
app_error quick_deploy_plan_resolve(const quick_plan_overrides_t *overrides,
                                    const quick_profile_config_t *profiles,
                                    quick_deploy_plan_t *plan);

void quick_ignore_init(quick_ignore_t *ignore);
void quick_ignore_destroy(quick_ignore_t *ignore);
app_error quick_ignore_load_file(const char *path, quick_ignore_t *ignore);
app_error quick_ignore_load_for_site(const char *site_root,
                                     quick_ignore_t *ignore);
char **quick_ignore_to_rsync_args(const quick_ignore_t *ignore,
                                  size_t *argc_out);
void quick_ignore_args_destroy(char **args, size_t argc);

void quick_deployment_record_init(quick_deployment_record_t *record);
void quick_deployment_record_destroy(quick_deployment_record_t *record);
app_error quick_local_state_write_deployment(const char *site_root,
                                             const char *profile,
                                             const char *site, const char *url,
                                             const char *release);
app_error quick_local_state_read_deployment(const char *site_root,
                                            const char *profile,
                                            quick_deployment_record_t *record);
