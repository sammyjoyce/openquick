#include "tui_onboarding_model.h"

#include <stdio.h>
#include <string.h>

static void quick_onboarding_copy_string(char *destination, size_t size,
                                         const char *source) {
  if (!destination || size == 0) {
    return;
  }
  snprintf(destination, size, "%s", source ? source : "");
}

void quick_onboarding_model_init(quick_onboarding_model_t *model) {
  quick_onboarding_model_reset(model, QUICK_ONBOARD_RETURN_DASHBOARD);
}

void quick_onboarding_model_reset(
    quick_onboarding_model_t *model,
    quick_onboarding_return_t return_destination) {
  if (!model) {
    return;
  }
  memset(model, 0, sizeof(*model));
  model->return_destination = return_destination;
  quick_onboarding_clear_validation(model);
}

void quick_onboarding_set_validation(quick_onboarding_model_t *model,
                                     app_error code, const char *message) {
  if (!model) {
    return;
  }
  model->validation.code = code;
  model->validation.valid = code == APP_SUCCESS;
  quick_onboarding_copy_string(model->validation.message,
                               sizeof(model->validation.message), message);
}

void quick_onboarding_clear_validation(quick_onboarding_model_t *model) {
  quick_onboarding_set_validation(model, APP_SUCCESS, NULL);
}

static bool quick_onboarding_show_welcome_allowed(
    quick_onboarding_state_t state) {
  return state == QUICK_ONBOARD_STATE_IDLE ||
         state == QUICK_ONBOARD_STATE_CANCELLED ||
         state == QUICK_ONBOARD_STATE_COMPLETE ||
         state == QUICK_ONBOARD_STATE_FAILED;
}

static bool quick_onboarding_choice_allowed(quick_onboarding_state_t state) {
  return state == QUICK_ONBOARD_STATE_WELCOME ||
         state == QUICK_ONBOARD_STATE_IDLE;
}

bool quick_onboarding_transition(quick_onboarding_model_t *model,
                                 quick_onboarding_event_t event) {
  if (!model) {
    return false;
  }

  quick_onboarding_model_t next = *model;
  bool valid = false;

  switch (event) {
  case QUICK_ONBOARD_EVENT_SHOW_WELCOME:
    if (quick_onboarding_show_welcome_allowed(model->state)) {
      next.state = QUICK_ONBOARD_STATE_WELCOME;
      next.flow = QUICK_ONBOARD_FLOW_NONE;
      memset(&next.checks, 0, sizeof(next.checks));
      memset(&next.mutation, 0, sizeof(next.mutation));
      quick_onboarding_clear_validation(&next);
      valid = true;
    }
    break;

  case QUICK_ONBOARD_EVENT_CHOOSE_LOCAL:
  case QUICK_ONBOARD_EVENT_CHOOSE_ADOPT:
    if (quick_onboarding_choice_allowed(model->state)) {
      next.state = QUICK_ONBOARD_STATE_LOCAL_DIRECTORY;
      next.flow = QUICK_ONBOARD_FLOW_LOCAL;
      next.values.adopt_existing = event == QUICK_ONBOARD_EVENT_CHOOSE_ADOPT;
      memset(&next.checks, 0, sizeof(next.checks));
      memset(&next.mutation, 0, sizeof(next.mutation));
      quick_onboarding_clear_validation(&next);
      valid = true;
    }
    break;

  case QUICK_ONBOARD_EVENT_CHOOSE_CONNECT:
  case QUICK_ONBOARD_EVENT_CHOOSE_INSTALL:
    if (quick_onboarding_choice_allowed(model->state) ||
        model->state == QUICK_ONBOARD_STATE_LOCAL_PREVIEW) {
      next.state = QUICK_ONBOARD_STATE_HOST_FIELDS;
      next.flow = event == QUICK_ONBOARD_EVENT_CHOOSE_CONNECT
                      ? QUICK_ONBOARD_FLOW_CONNECT_HOST
                      : QUICK_ONBOARD_FLOW_INSTALL_HOST;
      memset(&next.checks, 0, sizeof(next.checks));
      memset(&next.mutation, 0, sizeof(next.mutation));
      quick_onboarding_clear_validation(&next);
      valid = true;
    }
    break;

  case QUICK_ONBOARD_EVENT_NEXT:
    switch (model->state) {
    case QUICK_ONBOARD_STATE_LOCAL_DIRECTORY:
      next.state = QUICK_ONBOARD_STATE_LOCAL_IDENTITY;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_LOCAL_IDENTITY:
      next.state = model->values.adopt_existing
                       ? QUICK_ONBOARD_STATE_LOCAL_REVIEW
                       : QUICK_ONBOARD_STATE_LOCAL_TEMPLATE;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_LOCAL_TEMPLATE:
      next.state = QUICK_ONBOARD_STATE_LOCAL_REVIEW;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_LOCAL_APPLY:
      next.state = QUICK_ONBOARD_STATE_LOCAL_PREVIEW;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_HOST_FIELDS:
      next.state = QUICK_ONBOARD_STATE_HOST_REVIEW;
      valid = true;
      break;
    default:
      break;
    }
    break;

  case QUICK_ONBOARD_EVENT_BACK:
    if (model->mutation.started) {
      break;
    }
    switch (model->state) {
    case QUICK_ONBOARD_STATE_LOCAL_DIRECTORY:
      next.state = QUICK_ONBOARD_STATE_WELCOME;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_LOCAL_IDENTITY:
      next.state = QUICK_ONBOARD_STATE_LOCAL_DIRECTORY;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_LOCAL_TEMPLATE:
      next.state = QUICK_ONBOARD_STATE_LOCAL_IDENTITY;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_LOCAL_REVIEW:
      next.state = model->values.adopt_existing
                       ? QUICK_ONBOARD_STATE_LOCAL_IDENTITY
                       : QUICK_ONBOARD_STATE_LOCAL_TEMPLATE;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_HOST_FIELDS:
      next.state = QUICK_ONBOARD_STATE_WELCOME;
      valid = true;
      break;
    case QUICK_ONBOARD_STATE_HOST_REVIEW:
      next.state = QUICK_ONBOARD_STATE_HOST_FIELDS;
      valid = true;
      break;
    default:
      break;
    }
    break;

  case QUICK_ONBOARD_EVENT_CONFIRM:
    if (model->state == QUICK_ONBOARD_STATE_LOCAL_REVIEW) {
      next.state = QUICK_ONBOARD_STATE_LOCAL_APPLY;
      valid = true;
    } else if (model->state == QUICK_ONBOARD_STATE_HOST_REVIEW) {
      if (model->flow == QUICK_ONBOARD_FLOW_CONNECT_HOST) {
        next.state = QUICK_ONBOARD_STATE_HOST_VERIFY;
        valid = true;
      } else if (model->flow == QUICK_ONBOARD_FLOW_INSTALL_HOST) {
        next.state = QUICK_ONBOARD_STATE_HOST_INSTALL;
        valid = true;
      }
    }
    break;

  case QUICK_ONBOARD_EVENT_BEGIN_MUTATION:
    if (model->state == QUICK_ONBOARD_STATE_LOCAL_APPLY ||
        model->state == QUICK_ONBOARD_STATE_HOST_INSTALL) {
      next.mutation.started = true;
      valid = true;
    }
    break;

  case QUICK_ONBOARD_EVENT_VERIFY_OK:
    if (model->state == QUICK_ONBOARD_STATE_HOST_VERIFY ||
        model->state == QUICK_ONBOARD_STATE_HOST_INSTALL ||
        (model->state == QUICK_ONBOARD_STATE_FAILED &&
         (model->flow == QUICK_ONBOARD_FLOW_CONNECT_HOST ||
          model->flow == QUICK_ONBOARD_FLOW_INSTALL_HOST))) {
      next.state = QUICK_ONBOARD_STATE_HOST_SAVE;
      valid = true;
    }
    break;

  case QUICK_ONBOARD_EVENT_VERIFY_FAILED:
    if (model->state == QUICK_ONBOARD_STATE_LOCAL_APPLY ||
        model->state == QUICK_ONBOARD_STATE_HOST_VERIFY ||
        model->state == QUICK_ONBOARD_STATE_HOST_INSTALL ||
        model->state == QUICK_ONBOARD_STATE_HOST_SAVE) {
      next.state = QUICK_ONBOARD_STATE_FAILED;
      valid = true;
    }
    break;

  case QUICK_ONBOARD_EVENT_RETRY:
    if (model->state == QUICK_ONBOARD_STATE_FAILED) {
      if (model->flow == QUICK_ONBOARD_FLOW_LOCAL) {
        next.state = QUICK_ONBOARD_STATE_LOCAL_DIRECTORY;
        valid = true;
      } else if (model->flow == QUICK_ONBOARD_FLOW_CONNECT_HOST ||
                 model->flow == QUICK_ONBOARD_FLOW_INSTALL_HOST) {
        next.state = QUICK_ONBOARD_STATE_HOST_FIELDS;
        valid = true;
      }
      if (valid) {
        quick_onboarding_clear_validation(&next);
        memset(&next.checks, 0, sizeof(next.checks));
        memset(&next.mutation, 0, sizeof(next.mutation));
      }
    }
    break;

  case QUICK_ONBOARD_EVENT_COMPLETE:
    if (model->state == QUICK_ONBOARD_STATE_WELCOME ||
        model->state == QUICK_ONBOARD_STATE_LOCAL_PREVIEW ||
        model->state == QUICK_ONBOARD_STATE_HOST_SAVE) {
      next.state = QUICK_ONBOARD_STATE_COMPLETE;
      valid = true;
    }
    break;

  case QUICK_ONBOARD_EVENT_CANCEL:
    if (!model->mutation.started) {
      next.state = QUICK_ONBOARD_STATE_CANCELLED;
      valid = true;
    }
    break;
  }

  if (valid) {
    *model = next;
  }
  return valid;
}

const char *quick_onboarding_state_label(quick_onboarding_state_t state) {
  switch (state) {
  case QUICK_ONBOARD_STATE_IDLE:
    return "idle";
  case QUICK_ONBOARD_STATE_WELCOME:
    return "welcome";
  case QUICK_ONBOARD_STATE_LOCAL_DIRECTORY:
    return "local directory";
  case QUICK_ONBOARD_STATE_LOCAL_IDENTITY:
    return "local identity";
  case QUICK_ONBOARD_STATE_LOCAL_TEMPLATE:
    return "local template";
  case QUICK_ONBOARD_STATE_LOCAL_REVIEW:
    return "local review";
  case QUICK_ONBOARD_STATE_LOCAL_APPLY:
    return "local apply";
  case QUICK_ONBOARD_STATE_LOCAL_PREVIEW:
    return "local preview";
  case QUICK_ONBOARD_STATE_HOST_FIELDS:
    return "host fields";
  case QUICK_ONBOARD_STATE_HOST_REVIEW:
    return "host review";
  case QUICK_ONBOARD_STATE_HOST_VERIFY:
    return "host verify";
  case QUICK_ONBOARD_STATE_HOST_INSTALL:
    return "host install";
  case QUICK_ONBOARD_STATE_HOST_SAVE:
    return "host save";
  case QUICK_ONBOARD_STATE_COMPLETE:
    return "complete";
  case QUICK_ONBOARD_STATE_CANCELLED:
    return "cancelled";
  case QUICK_ONBOARD_STATE_FAILED:
    return "failed";
  }
  return "unknown";
}

void quick_onboarding_note_check(quick_onboarding_model_t *model,
                                 quick_onboarding_check_t check, bool passed) {
  if (!model || check < QUICK_ONBOARD_CHECK_LOCAL_TOOLS ||
      check >= QUICK_ONBOARD_CHECK_COUNT) {
    return;
  }
  model->checks.completed[check] = true;
  model->checks.passed[check] = passed;
}

void quick_onboarding_note_install_result(
    quick_onboarding_model_t *model, const quick_install_result_t *result) {
  if (!model || !result) {
    return;
  }

  model->mutation.started = result->mutation_started;
  model->mutation.backup_created = result->backup_created;
  model->mutation.rollback_attempted = result->rollback_attempted;
  model->mutation.rollback_ok = result->rollback_ok;
  model->mutation.partial_cleanup_remains = result->partial_cleanup_remains;
  quick_onboarding_copy_string(model->mutation.backup_path,
                               sizeof(model->mutation.backup_path),
                               result->backup_path);
  quick_onboarding_copy_string(model->mutation.cleanup_detail,
                               sizeof(model->mutation.cleanup_detail),
                               result->cleanup_detail);

  if (result->completed) {
    model->state = QUICK_ONBOARD_STATE_HOST_SAVE;
  } else if (result->cancelled && !result->mutation_started) {
    model->state = QUICK_ONBOARD_STATE_CANCELLED;
  } else {
    model->state = QUICK_ONBOARD_STATE_FAILED;
  }
}
