#include "tui_product_model.h"

#include <stdio.h>
#include <string.h>

#include "../core/deploy_plan.h"

bool quick_tui_format_site_row(const quick_list_item_t *item, char *buffer,
                               size_t size) {
  if (!item || !buffer || size == 0) {
    return false;
  }
  const char *name = item->name && item->name[0] ? item->name : "(unnamed)";
  const char *url = item->url && item->url[0] ? item->url : "(no url)";
  const char *release = item->release && item->release[0] ? item->release : "no release";
  const char *updated = item->updated_at && item->updated_at[0]
                            ? item->updated_at
                            : "unknown";
  const char *stale = item->stale ? " [stale]" : "";
  int n = snprintf(buffer, size, "%s - %s (%s, %s)%s", name, url, release,
                   updated, stale);
  return n >= 0 && (size_t)n < size;
}

static void quick_tui_validation_message(char *message, size_t size,
                                         const char *text) {
  if (message && size > 0) {
    snprintf(message, size, "%s", text ? text : "invalid value");
  }
}

bool quick_tui_validate_profile_field(const char *field, const char *value,
                                      char *message, size_t message_size) {
  const char *v = value ? value : "";
  if (!field || field[0] == '\0') {
    quick_tui_validation_message(message, message_size, "unknown field");
    return false;
  }
  if (strcmp(field, "name") == 0) {
    if (quick_profile_name_is_safe(v)) {
      return true;
    }
    quick_tui_validation_message(message, message_size,
                                 "profile names may use letters, digits, dot, underscore, and dash");
    return false;
  }
  if (strcmp(field, "ssh") == 0) {
    if (v[0] == '\0' || quick_ssh_target_is_safe(v)) {
      return true;
    }
    quick_tui_validation_message(message, message_size,
                                 "SSH targets may not contain spaces or shell metacharacters");
    return false;
  }
  if (strcmp(field, "remote_root") == 0) {
    if (v[0] == '\0' || quick_remote_path_is_safe(v)) {
      return true;
    }
    quick_tui_validation_message(message, message_size,
                                 "remote_root must be an absolute safe path without .. segments");
    return false;
  }
  if (strcmp(field, "base_domain") == 0) {
    if (v[0] == '\0' || quick_domain_is_safe(v)) {
      return true;
    }
    quick_tui_validation_message(message, message_size,
                                 "base_domain must be a safe DNS name or localhost");
    return false;
  }
  if (strcmp(field, "iap.type") == 0 || strcmp(field, "iap.mode") == 0) {
    if (v[0] == '\0' || quick_profile_name_is_safe(v)) {
      return true;
    }
    quick_tui_validation_message(message, message_size,
                                 "IAP values may use letters, digits, dot, underscore, and dash");
    return false;
  }
  if (strcmp(field, "base_url") == 0 || strcmp(field, "iap.team_domain") == 0 ||
      strcmp(field, "iap.audience") == 0) {
    return true;
  }
  if (strcmp(field, "deploy.delete") == 0 ||
      strcmp(field, "deploy.open_after_deploy") == 0) {
    if (strcmp(v, "true") == 0 || strcmp(v, "false") == 0 ||
        strcmp(v, "1") == 0 || strcmp(v, "0") == 0 || v[0] == '\0') {
      return true;
    }
    quick_tui_validation_message(message, message_size,
                                 "boolean fields accept true/false");
    return false;
  }
  return true;
}

const char *quick_tui_deploy_phase_label(quick_deploy_phase_t phase) {
  switch (phase) {
  case QUICK_DEPLOY_PHASE_BUILD:
    return "build";
  case QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK:
    return "bootstrap check";
  case QUICK_DEPLOY_PHASE_PREPARE:
    return "prepare";
  case QUICK_DEPLOY_PHASE_TRANSFER:
    return "transfer";
  case QUICK_DEPLOY_PHASE_ACTIVATE:
    return "activate";
  case QUICK_DEPLOY_PHASE_RECORD:
    return "record";
  case QUICK_DEPLOY_PHASE_NONE:
  default:
    return "deploy";
  }
}

const char *quick_tui_doctor_status_icon(quick_doctor_status_t status) {
  switch (status) {
  case QUICK_DOCTOR_STATUS_OK:
    return "✓";
  case QUICK_DOCTOR_STATUS_WARN:
    return "!";
  case QUICK_DOCTOR_STATUS_FAIL:
    return "✗";
  case QUICK_DOCTOR_STATUS_SKIP:
    return "-";
  }
  return "!";
}
