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
  bool bootstrap_missing;
  bool publication_issue;
} quick_deploy_result_t;

void quick_deploy_result_init(quick_deploy_result_t *result);
void quick_deploy_result_destroy(quick_deploy_result_t *result);
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
  bool stale;
  quick_list_source_t source;
} quick_list_item_t;

typedef struct {
  quick_list_item_t *items;
  size_t count;
  bool remote_requested;
  bool remote_ok;
  bool have_local;
  char *remote_json;
  char *site;
  char *profile;
  char *url;
} quick_list_result_t;

void quick_list_result_init(quick_list_result_t *result);
void quick_list_result_destroy(quick_list_result_t *result);
app_error quick_op_list(const quick_list_request_t *request,
                        quick_list_result_t *out);

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
  const char *port;
  const char *identity;
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
