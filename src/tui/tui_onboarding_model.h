#pragma once

#include <stdbool.h>

#include "../core/ops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUICK_ONBOARDING_PATH_MAX 512
#define QUICK_ONBOARDING_NAME_MAX 160
#define QUICK_ONBOARDING_HOST_MAX 256
#define QUICK_ONBOARDING_IAP_MAX 64

typedef enum {
  QUICK_ONBOARD_FLOW_NONE = 0,
  QUICK_ONBOARD_FLOW_LOCAL,
  QUICK_ONBOARD_FLOW_CONNECT_HOST,
  QUICK_ONBOARD_FLOW_INSTALL_HOST
} quick_onboarding_flow_t;

typedef enum {
  QUICK_ONBOARD_STATE_IDLE = 0,
  QUICK_ONBOARD_STATE_WELCOME,
  QUICK_ONBOARD_STATE_LOCAL_DIRECTORY,
  QUICK_ONBOARD_STATE_LOCAL_IDENTITY,
  QUICK_ONBOARD_STATE_LOCAL_TEMPLATE,
  QUICK_ONBOARD_STATE_LOCAL_REVIEW,
  QUICK_ONBOARD_STATE_LOCAL_APPLY,
  QUICK_ONBOARD_STATE_LOCAL_PREVIEW,
  QUICK_ONBOARD_STATE_HOST_FIELDS,
  QUICK_ONBOARD_STATE_HOST_REVIEW,
  QUICK_ONBOARD_STATE_HOST_VERIFY,
  QUICK_ONBOARD_STATE_HOST_INSTALL,
  QUICK_ONBOARD_STATE_HOST_SAVE,
  QUICK_ONBOARD_STATE_COMPLETE,
  QUICK_ONBOARD_STATE_CANCELLED,
  QUICK_ONBOARD_STATE_FAILED
} quick_onboarding_state_t;

typedef enum {
  QUICK_ONBOARD_RETURN_DASHBOARD = 0,
  QUICK_ONBOARD_RETURN_DEPLOY,
  QUICK_ONBOARD_RETURN_SERVE,
  QUICK_ONBOARD_RETURN_WELCOME
} quick_onboarding_return_t;

typedef enum {
  QUICK_ONBOARD_EVENT_SHOW_WELCOME = 0,
  QUICK_ONBOARD_EVENT_CHOOSE_LOCAL,
  QUICK_ONBOARD_EVENT_CHOOSE_ADOPT,
  QUICK_ONBOARD_EVENT_CHOOSE_CONNECT,
  QUICK_ONBOARD_EVENT_CHOOSE_INSTALL,
  QUICK_ONBOARD_EVENT_NEXT,
  QUICK_ONBOARD_EVENT_BACK,
  QUICK_ONBOARD_EVENT_CONFIRM,
  QUICK_ONBOARD_EVENT_BEGIN_MUTATION,
  QUICK_ONBOARD_EVENT_VERIFY_OK,
  QUICK_ONBOARD_EVENT_VERIFY_FAILED,
  QUICK_ONBOARD_EVENT_RETRY,
  QUICK_ONBOARD_EVENT_COMPLETE,
  QUICK_ONBOARD_EVENT_CANCEL
} quick_onboarding_event_t;

typedef enum {
  QUICK_ONBOARD_CHECK_LOCAL_TOOLS = 0,
  QUICK_ONBOARD_CHECK_PROJECT,
  QUICK_ONBOARD_CHECK_SSH,
  QUICK_ONBOARD_CHECK_REMOTE_COMPAT,
  QUICK_ONBOARD_CHECK_SUDO,
  QUICK_ONBOARD_CHECK_BACKUP,
  QUICK_ONBOARD_CHECK_DOCTOR,
  QUICK_ONBOARD_CHECK_COUNT
} quick_onboarding_check_t;

typedef struct {
  char project_dir[QUICK_ONBOARDING_PATH_MAX];
  char site_name[QUICK_ONBOARDING_NAME_MAX];
  char template_name[QUICK_ONBOARDING_IAP_MAX];
  bool adopt_existing;
  char profile[QUICK_ONBOARDING_NAME_MAX];
  char host[QUICK_ONBOARDING_HOST_MAX];
  char remote_root[QUICK_ONBOARDING_PATH_MAX];
  char domain[QUICK_ONBOARDING_HOST_MAX];
  char iap_type[QUICK_ONBOARDING_IAP_MAX];
  char iap_mode[QUICK_ONBOARDING_IAP_MAX];
  char team_domain[QUICK_ONBOARDING_HOST_MAX];
  char audience[QUICK_ONBOARDING_HOST_MAX];
} quick_onboarding_values_t;

typedef struct {
  bool completed[QUICK_ONBOARD_CHECK_COUNT];
  bool passed[QUICK_ONBOARD_CHECK_COUNT];
} quick_onboarding_checks_t;

typedef struct {
  app_error code;
  bool valid;
  char message[256];
} quick_onboarding_validation_t;

typedef struct {
  bool started;
  bool backup_created;
  bool rollback_attempted;
  bool rollback_ok;
  bool partial_cleanup_remains;
  char backup_path[QUICK_ONBOARDING_PATH_MAX];
  char cleanup_detail[256];
} quick_onboarding_mutation_t;

typedef struct {
  quick_onboarding_state_t state;
  quick_onboarding_flow_t flow;
  quick_onboarding_return_t return_destination;
  quick_onboarding_values_t values;
  quick_onboarding_checks_t checks;
  quick_onboarding_validation_t validation;
  quick_onboarding_mutation_t mutation;
} quick_onboarding_model_t;

void quick_onboarding_model_init(quick_onboarding_model_t *model);
void quick_onboarding_model_reset(
    quick_onboarding_model_t *model,
    quick_onboarding_return_t return_destination);
bool quick_onboarding_transition(quick_onboarding_model_t *model,
                                 quick_onboarding_event_t event);
const char *quick_onboarding_state_label(quick_onboarding_state_t state);
void quick_onboarding_set_validation(quick_onboarding_model_t *model,
                                     app_error code, const char *message);
void quick_onboarding_clear_validation(quick_onboarding_model_t *model);
void quick_onboarding_note_check(quick_onboarding_model_t *model,
                                 quick_onboarding_check_t check, bool passed);
void quick_onboarding_note_install_result(
    quick_onboarding_model_t *model, const quick_install_result_t *result);

#ifdef __cplusplus
}
#endif
