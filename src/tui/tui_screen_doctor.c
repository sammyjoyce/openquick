#include "tui_app_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/ops.h"
#include "tui.h"
#include "tui_panel.h"
#include "tui_product_model.h"

static int quick_tui_doctor_choose_scope(void) {
  const tui_menu_item_t items[] = {
      {.label = "&Local only", .description = "Local CLI, tools, quick.json, output", .id = 1},
      {.label = "With &remote", .description = "Also ask selected host quickd", .id = 2},
      {.label = "&Deep", .description = "Remote checks plus temporary deploy/probes", .id = 3},
      {.kind = TUI_MENU_ITEM_SEPARATOR},
      {.label = "&Back", .description = "Return to OpenQuick", .id = 4},
  };
  tui_menu_result_t r = tui_show_menu(
      NULL, &(tui_menu_config_t){.title = "Doctor",
                                 .subtitle = "Choose diagnostic scope",
                                 .items = items,
                                 .item_count = (int)(sizeof(items) / sizeof(items[0])),
                                 .default_index = 0,
                                 .frame_height = 14,
                                 .frame_width = 70,
                                 .show_numeric_keys = true});
  return r.status == TUI_MENU_OK ? r.selected_id : 4;
}

static char *quick_tui_doctor_select_profile(quick_tui_app_state_t *state) {
  if (!state || state->profiles.profile_count <= 1) {
    const char *name = quick_tui_default_profile_name(state);
    char *copy = malloc(strlen(name) + 1U);
    if (copy) {
      strcpy(copy, name);
    }
    return copy;
  }
  tui_menu_item_t *items = calloc(state->profiles.profile_count, sizeof(tui_menu_item_t));
  if (!items) {
    return NULL;
  }
  for (size_t i = 0; i < state->profiles.profile_count; i++) {
    const quick_profile_t *p = &state->profiles.profiles[i];
    items[i] = (tui_menu_item_t){.label = p->name,
                                 .description = p->ssh ? p->ssh : "local/no ssh",
                                 .id = (int)i + 1};
  }
  tui_menu_result_t r = tui_show_menu(
      NULL, &(tui_menu_config_t){.title = "Doctor profile",
                                 .subtitle = "Select profile for remote checks",
                                 .items = items,
                                 .item_count = (int)state->profiles.profile_count,
                                 .default_index = 0,
                                 .frame_height = 16,
                                 .frame_width = 70,
                                 .enable_search = true,
                                 .show_numeric_keys = true});
  char *out = NULL;
  if (r.status == TUI_MENU_OK && r.selected_id >= 1 &&
      (size_t)r.selected_id <= state->profiles.profile_count) {
    const char *name = state->profiles.profiles[(size_t)r.selected_id - 1U].name;
    out = malloc(strlen(name) + 1U);
    if (out) {
      strcpy(out, name);
    }
  }
  free(items);
  return out;
}

static void quick_tui_show_doctor_detail(const quick_doctor_check_t *check) {
  quick_tui_kv_row_t rows[] = {
      {"group", check->group, TUI_COLOR_MENU_NORMAL},
      {"status", quick_doctor_status_string(check->status),
       check->status == QUICK_DOCTOR_STATUS_FAIL
           ? TUI_COLOR_ERROR
           : (check->status == QUICK_DOCTOR_STATUS_WARN ? TUI_COLOR_WARNING
                                                        : TUI_COLOR_SUCCESS)},
      {"name", check->name, TUI_COLOR_MENU_NORMAL},
      {"detail", check->detail, TUI_COLOR_MENU_NORMAL},
      {"remediation", check->remediation, TUI_COLOR_INFO},
  };
  quick_tui_show_keyvalue_panel("Doctor check", rows,
                                sizeof(rows) / sizeof(rows[0]),
                                "Enter/Esc closes");
}

void quick_tui_screen_doctor(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
  (void)quick_tui_reload_profiles(state);
  int scope = quick_tui_doctor_choose_scope();
  if (scope == 4) {
    return;
  }
  char *profile = NULL;
  if (scope == 2 || scope == 3) {
    profile = quick_tui_doctor_select_profile(state);
    if (!profile) {
      return;
    }
  }

  tui_progress_t *progress = tui_progress_create("Doctor", 100);
  if (progress) {
    tui_progress_update(progress, 25, "running checks...");
  }
  quick_doctor_result_t result;
  quick_doctor_result_init(&result);
  quick_doctor_request_t request = {.profiles = &state->profiles,
                                    .profile = profile,
                                    .remote = scope == 2 || scope == 3,
                                    .deep = scope == 3};
  app_error err = quick_op_doctor(&request, &result);
  if (progress) {
    tui_progress_update(progress, 100,
                        err == APP_SUCCESS ? "checks complete" : "doctor failed");
    tui_progress_destroy(progress);
  }
  free(profile);
  if (err != APP_SUCCESS) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Doctor failed: %s", app_strerror(err));
    tui_show_message("Doctor", msg);
    quick_doctor_result_destroy(&result);
    return;
  }

  if (result.count == 0) {
    tui_show_message("Doctor", "No checks were returned.");
    quick_doctor_result_destroy(&result);
    return;
  }

  tui_menu_item_t *items = calloc(result.count + 1U, sizeof(tui_menu_item_t));
  char **labels = calloc(result.count, sizeof(char *));
  if (!items || !labels) {
    free(items);
    free(labels);
    quick_doctor_result_destroy(&result);
    tui_show_message("Doctor", "Out of memory while rendering checks.");
    return;
  }
  for (size_t i = 0; i < result.count; i++) {
    const quick_doctor_check_t *check = &result.checks[i];
    labels[i] = malloc(384);
    if (!labels[i]) {
      continue;
    }
    snprintf(labels[i], 384, "%s %s/%s - %s",
             quick_tui_doctor_status_icon(check->status),
             check->group ? check->group : "group",
             check->name ? check->name : "check",
             check->detail ? check->detail : "");
    items[i] = (tui_menu_item_t){.label = labels[i],
                                 .description = check->remediation,
                                 .id = (int)i + 1};
  }
  items[result.count] = (tui_menu_item_t){.label = "&Back",
                                           .description = "Return to OpenQuick",
                                           .id = 1000};
  char subtitle[96];
  snprintf(subtitle, sizeof(subtitle), "overall %s · Enter opens remediation",
           result.ok ? "ok" : "fail/warn");

  bool open = true;
  while (open && !tui_interrupted()) {
    tui_menu_result_t r = tui_show_menu(
        NULL, &(tui_menu_config_t){.title = "Doctor results",
                                   .subtitle = subtitle,
                                   .items = items,
                                   .item_count = (int)result.count + 1,
                                   .default_index = 0,
                                   .frame_height = 20,
                                   .frame_width = 78,
                                   .enable_search = true,
                                   .show_numeric_keys = true});
    if (r.status != TUI_MENU_OK || r.selected_id == 1000) {
      open = false;
    } else if (r.selected_id >= 1 && (size_t)r.selected_id <= result.count) {
      quick_tui_show_doctor_detail(&result.checks[(size_t)r.selected_id - 1U]);
    }
  }

  for (size_t i = 0; i < result.count; i++) {
    free(labels[i]);
  }
  free(labels);
  free(items);
  quick_doctor_result_destroy(&result);
}
