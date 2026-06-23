#pragma once

#include <signal.h>
#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "deploy_plan.h"
#include "error.h"
#include "process.h"
#include "profile_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  QUICK_INIT_TEMPLATE_BLANK = 0,
  QUICK_INIT_TEMPLATE_REALTIME = 1,
} quick_init_template_t;

typedef struct {
  const char *target_dir;
  const char *name;
  quick_init_template_t template_kind;
  const char *profile;
  bool adopt_existing;
} quick_init_request_t;

typedef struct {
  char *site;
  char *path;
  char *profile;
  char **files_created;
  size_t file_count;
} quick_init_result_t;

void quick_init_result_init(quick_init_result_t *result);
void quick_init_result_destroy(quick_init_result_t *result);
app_error quick_op_init(const quick_init_request_t *request,
                        quick_init_result_t *out);

typedef enum {
  QUICK_DEPLOY_PHASE_NONE = 0,
  QUICK_DEPLOY_PHASE_BUILD,
  QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
  QUICK_DEPLOY_PHASE_PREPARE,
  QUICK_DEPLOY_PHASE_TRANSFER,
  QUICK_DEPLOY_PHASE_ACTIVATE,
  QUICK_DEPLOY_PHASE_RECORD,
} quick_deploy_phase_t;

typedef struct {
  bool no_build;
  bool no_delete;
  bool checksum;
  bool bootstrap;
  bool allow_unpublished;
  bool assume_yes;
  bool overwrite_confirmed;
  const char *deployer;
  const char *ssh_key_id;
  const char *ssh_principals;
  const char *zip_path;
  const volatile sig_atomic_t *cancel_flag;
} quick_deploy_options_t;

typedef void (*quick_deploy_progress_cb)(quick_deploy_phase_t phase,
                                         quick_stream_kind_t stream,
                                         const char *line, void *userdata);

typedef struct {
  char *release;
  char *url;
  long changed;
  long reused;
  long deleted;
  quick_deploy_phase_t failure_phase;
  char *failure_message;
  char *bootstrap_install_command;
  char *last_deployer;
  char *last_release;
  char *last_deployed_at;
  char *cleanup_path;
  char *cleanup_message;
  bool cleanup_attempted;
  bool cleanup_ok;
  bool bootstrap_missing;
  bool publication_issue;
  bool overwrite_confirmation_required;
  bool zip_deploy;
} quick_deploy_result_t;

void quick_deploy_result_init(quick_deploy_result_t *result);
void quick_deploy_result_destroy(quick_deploy_result_t *result);
char *quick_op_default_deployer_identity(void);
app_error quick_op_deploy_execute(const app_config_t *config,
                                  const quick_profile_config_t *profiles,
                                  const quick_deploy_plan_t *plan,
                                  const quick_deploy_options_t *options,
                                  quick_deploy_progress_cb cb, void *userdata,
                                  quick_deploy_result_t *out);
void quick_op_deploy_parse_rsync_counts(const char *output, long *changed,
                                        long *reused, long *deleted);

typedef enum {
  QUICK_LIST_SOURCE_LOCAL = 0,
  QUICK_LIST_SOURCE_REMOTE = 1,
} quick_list_source_t;

typedef struct {
  const quick_profile_config_t *profiles;
  quick_plan_overrides_t overrides;
  bool remote;
} quick_list_request_t;

typedef struct {
  char *name;
  char *url;
  char *release;
  char *updated_at;
  char *deployer;
  char *subdomain;
  bool stale;
  bool is_public;
  bool have_public;
  quick_list_source_t source;
} quick_list_item_t;

typedef struct {
  quick_list_item_t *items;
  size_t count;
  bool remote_requested;
  bool remote_ok;
  bool have_local;
  char *remote_json;
  char *remote_error;
  char *remote_phase;
  char *remote_remediation;
  char *site;
  char *profile;
  char *url;
} quick_list_result_t;

void quick_list_result_init(quick_list_result_t *result);
void quick_list_result_destroy(quick_list_result_t *result);
app_error quick_op_list(const quick_list_request_t *request,
                        quick_list_result_t *out);

typedef struct {
  char *name;
  char *subdomain;
  char *url;
  char *release;
  char *updated_at;
  char *deployer;
  bool is_public;
  bool have_public;
  char *raw_json;
} quick_remote_site_info_t;

void quick_remote_site_info_init(quick_remote_site_info_t *info);
void quick_remote_site_info_destroy(quick_remote_site_info_t *info);

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
  const char *site;
  bool assume_yes;
  bool confirmed;
} quick_delete_request_t;

typedef struct {
  quick_remote_site_info_t site;
  char *profile;
  char *ssh;
  char *delete_json;
  char *archive;
  bool confirmation_required;
  bool deleted;
} quick_delete_result_t;

void quick_delete_result_init(quick_delete_result_t *result);
void quick_delete_result_destroy(quick_delete_result_t *result);
app_error quick_op_delete(const quick_delete_request_t *request,
                          quick_delete_result_t *out);

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
  const char *site;
  const char *archive;
  bool assume_yes;
  bool confirmed;
} quick_restore_request_t;

typedef struct {
  char *profile;
  char *ssh;
  char *remote_json;
  char *site;
  char *archive;
  char *release;
  char *url;
  bool confirmation_required;
  bool restored;
} quick_restore_result_t;

void quick_restore_result_init(quick_restore_result_t *result);
void quick_restore_result_destroy(quick_restore_result_t *result);
app_error quick_op_restore(const quick_restore_request_t *request,
                           quick_restore_result_t *out);

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
  const char *site;
  const char *release;
  bool assume_yes;
  bool confirmed;
} quick_rollback_request_t;

typedef struct {
  quick_remote_site_info_t site;
  char *profile;
  char *ssh;
  char *remote_json;
  char *release;
  char *previous_release;
  bool confirmation_required;
  bool rolled_back;
} quick_rollback_result_t;

void quick_rollback_result_init(quick_rollback_result_t *result);
void quick_rollback_result_destroy(quick_rollback_result_t *result);
app_error quick_op_rollback(const quick_rollback_request_t *request,
                            quick_rollback_result_t *out);

typedef enum {
  QUICK_PUBLIC_STATUS = 0,
  QUICK_PUBLIC_ON,
  QUICK_PUBLIC_OFF,
} quick_public_action_t;

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
  const char *site;
  quick_public_action_t action;
  bool assume_yes;
  bool confirmed;
} quick_public_request_t;

typedef struct {
  quick_remote_site_info_t site;
  char *profile;
  char *ssh;
  char *remote_json;
  bool confirmation_required;
  bool changed;
  bool is_public;
  bool have_public;
} quick_public_result_t;

void quick_public_result_init(quick_public_result_t *result);
void quick_public_result_destroy(quick_public_result_t *result);
app_error quick_op_public(const quick_public_request_t *request,
                          quick_public_result_t *out);

typedef enum {
  QUICK_DOMAIN_LIST = 0,
  QUICK_DOMAIN_ADD,
  QUICK_DOMAIN_REMOVE,
} quick_domain_action_t;

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
  const char *site;
  const char *domain;
  quick_domain_action_t action;
} quick_domain_request_t;

typedef struct {
  char *profile;
  char *ssh;
  char *site;
  char *domain;
  char *remote_json;
} quick_domain_result_t;

void quick_domain_result_init(quick_domain_result_t *result);
void quick_domain_result_destroy(quick_domain_result_t *result);
app_error quick_op_domain(const quick_domain_request_t *request,
                          quick_domain_result_t *out);

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
} quick_host_stats_request_t;

typedef struct {
  char *profile;
  char *ssh;
  char *raw_json;
  long sites;
  long releases;
  long sites_bytes;
  long uploads_bytes;
  long db_bytes;
  bool parsed;
} quick_host_stats_result_t;

void quick_host_stats_result_init(quick_host_stats_result_t *result);
void quick_host_stats_result_destroy(quick_host_stats_result_t *result);
app_error quick_op_host_stats(const quick_host_stats_request_t *request,
                              quick_host_stats_result_t *out);

typedef enum {
  QUICK_DOCTOR_STATUS_OK = 0,
  QUICK_DOCTOR_STATUS_WARN,
  QUICK_DOCTOR_STATUS_FAIL,
  QUICK_DOCTOR_STATUS_SKIP,
} quick_doctor_status_t;

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
  const char *site;
  bool remote;
  bool deep;
} quick_doctor_request_t;

typedef struct {
  char *name;
  char *group;
  quick_doctor_status_t status;
  char *detail;
  char *remediation;
} quick_doctor_check_t;

typedef struct {
  quick_doctor_check_t *checks;
  size_t count;
  bool ok;
} quick_doctor_result_t;

void quick_doctor_result_init(quick_doctor_result_t *result);
void quick_doctor_result_destroy(quick_doctor_result_t *result);
const char *quick_doctor_status_string(quick_doctor_status_t status);
app_error quick_op_doctor(const quick_doctor_request_t *request,
                          quick_doctor_result_t *out);

typedef enum {
  QUICK_URL_SOURCE_LOCAL = 0,
  QUICK_URL_SOURCE_REMOTE,
  QUICK_URL_SOURCE_DETERMINISTIC,
} quick_url_source_t;

typedef struct {
  char *url;
  bool live;
  quick_url_source_t source;
} quick_url_result_t;

void quick_url_result_init(quick_url_result_t *result);
void quick_url_result_destroy(quick_url_result_t *result);
const char *quick_url_source_string(quick_url_source_t source);
app_error quick_op_resolve_url(const quick_profile_config_t *profiles,
                               const quick_plan_overrides_t *overrides,
                               quick_url_result_t *out);
app_error quick_op_open_url(const char *url);
app_error quick_op_copy_url(const char *url, char **message_out);

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
  const char *site;
  int ttl_seconds;
} quick_dev_token_request_t;

typedef struct {
  char *profile;
  char *ssh;
  char *site;
  char *url;
  char *token;
  char *expires_at;
  char *raw_json;
} quick_dev_token_result_t;

void quick_dev_token_result_init(quick_dev_token_result_t *result);
void quick_dev_token_result_destroy(quick_dev_token_result_t *result);
app_error quick_op_mint_dev_token(const quick_dev_token_request_t *request,
                                  quick_dev_token_result_t *out);

typedef struct {
  const quick_profile_config_t *profiles;
  const char *profile;
  const char *port;
  const char *identity;
  const char *remote_api_profile;
} quick_serve_dev_request_t;

typedef struct {
  char *quickd_path;
  char **argv;
  size_t argc;
} quick_serve_dev_command_t;

void quick_serve_dev_command_init(quick_serve_dev_command_t *command);
void quick_serve_dev_command_destroy(quick_serve_dev_command_t *command);
app_error quick_op_serve_dev_command(const quick_serve_dev_request_t *request,
                                     quick_serve_dev_command_t *out);

typedef struct {
  const char *profile;
  const char *host;
  const char *remote_root;
  const char *domain;
  const char *iap;
} quick_serve_install_request_t;

typedef struct {
  char *summary;
} quick_serve_install_step_t;

typedef struct {
  quick_serve_install_step_t *steps;
  size_t count;
} quick_serve_install_steps_t;

void quick_serve_install_steps_init(quick_serve_install_steps_t *steps);
void quick_serve_install_steps_destroy(quick_serve_install_steps_t *steps);
app_error quick_op_serve_install_steps(
    const quick_serve_install_request_t *request,
    quick_serve_install_steps_t *out);

#ifdef __cplusplus
}
#endif
