#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tui_app_state.h"
#ifndef _WIN32
#include <unistd.h>
#endif

#include "../core/deploy_plan.h"
#include "../core/ops.h"
#include "tui.h"
#include "tui_internal.h"
#include "tui_panel.h"
#include "tui_product_model.h"

static bool quick_tui_file_exists(const char *path) {
  if (!path) {
    return false;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  fclose(f);
  return true;
}

static char *quick_tui_strdup_local(const char *value) {
  if (!value) {
    return NULL;
  }
  size_t len = strlen(value);
  char *copy = malloc(len + 1U);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len + 1U);
  return copy;
}

static char *quick_tui_select_profile(quick_tui_app_state_t *state,
                                      bool allow_none) {
  if (!state) {
    return NULL;
  }
  if (state->profiles.profile_count == 0) {
    return allow_none
               ? quick_tui_strdup_local("")
               : quick_tui_strdup_local(quick_tui_default_profile_name(state));
  }
  const size_t extra = allow_none ? 2U : 1U;
  const size_t count = state->profiles.profile_count + extra;
  tui_menu_item_t *items = calloc(count, sizeof(tui_menu_item_t));
  if (!items) {
    return NULL;
  }
  for (size_t i = 0; i < state->profiles.profile_count; i++) {
    const quick_profile_t *p = &state->profiles.profiles[i];
    items[i] =
        (tui_menu_item_t){.label = p->name,
                          .description = p->ssh ? p->ssh : "local/no ssh",
                          .id = (int)i + 1};
  }
  size_t index = state->profiles.profile_count;
  if (allow_none) {
    items[index++] =
        (tui_menu_item_t){.label = "&None",
                          .description = "Do not bind this site to a profile",
                          .id = 900};
  }
  items[index++] = (tui_menu_item_t){
      .label = "&Cancel", .description = "Cancel profile selection", .id = 901};
  tui_menu_result_t r = tui_show_menu(
      NULL, &(tui_menu_config_t){.title = "Profile",
                                 .subtitle = "Select deployment profile",
                                 .items = items,
                                 .item_count = (int)index,
                                 .default_index = 0,
                                 .frame_height = 16,
                                 .frame_width = 70,
                                 .enable_search = true,
                                 .show_numeric_keys = true});
  char *selected = NULL;
  if (r.status == TUI_MENU_OK && r.selected_id >= 1 &&
      (size_t)r.selected_id <= state->profiles.profile_count) {
    selected = quick_tui_strdup_local(
        state->profiles.profiles[(size_t)r.selected_id - 1U].name);
  } else if (r.status == TUI_MENU_OK && r.selected_id == 900) {
    selected = quick_tui_strdup_local("");
  }
  free(items);
  return selected;
}

static bool quick_tui_prompt_slug(const char *title, const char *field,
                                  const char *current,
                                  char out[QUICK_SLUG_MAX + 1]) {
  char prompt[160];
  snprintf(prompt, sizeof(prompt), "%s [%s]:", field,
           current && current[0] ? current : "site");
  for (int tries = 0; tries < 3; tries++) {
    char input[128] = {0};
    if (tui_input_dialog(title, prompt, input, sizeof(input)) != APP_SUCCESS) {
      return false;
    }
    const char *candidate = input[0] ? input : current;
    if (quick_slug_normalize(candidate, out) == APP_SUCCESS &&
        quick_slug_is_valid(out)) {
      return true;
    }
    tui_show_message(title,
                     "That value cannot be normalized to a DNS label. Use "
                     "letters, digits, spaces, underscores, dots, or hyphens.");
  }
  return false;
}

typedef struct {
  const quick_deploy_plan_t *plan;
  bool deploy;
} quick_tui_plan_panel_state_t;

static void quick_tui_plan_redraw(tui_window_t *window, void *userdata) {
  quick_tui_plan_panel_state_t *state = userdata;
  const quick_deploy_plan_t *plan = state->plan;
  tui_draw_border(window);
  char rsync[160];
  snprintf(
      rsync, sizeof(rsync),
      "archive+compress, delete enabled, safe-links, chmod group-writable");
  quick_tui_kv_row_t rows[] = {
      {"site", plan->site, TUI_COLOR_MENU_NORMAL},
      {"profile", plan->profile, TUI_COLOR_MENU_NORMAL},
      {"host", plan->ssh ? plan->ssh : "(no ssh configured)",
       plan->ssh ? TUI_COLOR_MENU_NORMAL : TUI_COLOR_WARNING},
      {"url", plan->url, TUI_COLOR_MENU_NORMAL},
      {"source", plan->source_dir, TUI_COLOR_MENU_NORMAL},
      {"output", plan->output_dir, TUI_COLOR_MENU_NORMAL},
      {"remote root", plan->remote_root ? plan->remote_root : "(default)",
       TUI_COLOR_MENU_NORMAL},
      {"rsync", rsync, TUI_COLOR_DIM},
  };
  int y = 3;
  for (size_t i = 0;
       i < sizeof(rows) / sizeof(rows[0]) && y < window->height - 3; i++) {
    tui_set_color(window->win, TUI_COLOR_DIM);
    mvwaddnstr(window->win, y, 3, rows[i].key, 14);
    tui_unset_color(window->win, TUI_COLOR_DIM);
    tui_set_color(window->win, rows[i].color);
    mvwaddnstr(window->win, y, 18, rows[i].value ? rows[i].value : "",
               window->width - 21);
    tui_unset_color(window->win, rows[i].color);
    y++;
  }
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2,
                     "Enter: deploy  Esc: cancel");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_tui_plan_key(tui_window_t *window, int ch,
                                               void *userdata) {
  (void)window;
  quick_tui_plan_panel_state_t *state = userdata;
  if (ch == '\n' || ch == KEY_ENTER) {
    state->deploy = true;
    return TUI_MODAL_DONE;
  }
  if (ch == 27 || ch == 'q' || ch == 'Q') {
    state->deploy = false;
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

static bool quick_tui_confirm_plan(const quick_deploy_plan_t *plan) {
  quick_tui_plan_panel_state_t state = {.plan = plan, .deploy = false};
  (void)tui_modal_run(16, 78, "Deploy plan", quick_tui_plan_redraw,
                      quick_tui_plan_key, &state);
  return state.deploy;
}

typedef struct {
  tui_progress_t *progress;
  volatile sig_atomic_t cancel;
  quick_deploy_phase_t phase;
} quick_tui_deploy_progress_state_t;

static int quick_tui_phase_percent(quick_deploy_phase_t phase) {
  switch (phase) {
  case QUICK_DEPLOY_PHASE_BUILD:
    return 12;
  case QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK:
    return 25;
  case QUICK_DEPLOY_PHASE_PREPARE:
    return 40;
  case QUICK_DEPLOY_PHASE_TRANSFER:
    return 65;
  case QUICK_DEPLOY_PHASE_ACTIVATE:
    return 85;
  case QUICK_DEPLOY_PHASE_RECORD:
    return 95;
  case QUICK_DEPLOY_PHASE_NONE:
  default:
    return 5;
  }
}

static void quick_tui_deploy_progress_cb(quick_deploy_phase_t phase,
                                         quick_stream_kind_t stream,
                                         const char *line, void *userdata) {
  (void)stream;
  quick_tui_deploy_progress_state_t *state = userdata;
  if (!state) {
    return;
  }
  if (tui_interrupted()) {
    state->cancel = 1;
  }
  if (phase != QUICK_DEPLOY_PHASE_NONE) {
    state->phase = phase;
  }
  if (state->progress) {
    char status[160];
    const char *label = quick_tui_deploy_phase_label(state->phase);
    if (line && line[0]) {
      snprintf(status, sizeof(status), "%s: %.120s", label, line);
    } else {
      snprintf(status, sizeof(status), "%s...", label);
    }
    char *nl = strchr(status, '\n');
    if (nl) {
      *nl = '\0';
    }
    tui_progress_update(state->progress, quick_tui_phase_percent(state->phase),
                        status);
  }
}

typedef struct {
  const quick_deploy_result_t *result;
  bool open;
} quick_tui_deploy_result_state_t;

static void quick_tui_deploy_result_redraw(tui_window_t *window,
                                           void *userdata) {
  quick_tui_deploy_result_state_t *state = userdata;
  const quick_deploy_result_t *result = state->result;
  tui_draw_border(window);
  char changed[64];
  char reused[64];
  char deleted[64];
  snprintf(changed, sizeof(changed), "%ld", result->changed);
  snprintf(reused, sizeof(reused), "%ld", result->reused);
  snprintf(deleted, sizeof(deleted), "%ld", result->deleted);
  const struct {
    const char *key;
    const char *value;
  } rows[] = {{"release", result->release ? result->release : "(unknown)"},
              {"url", result->url ? result->url : "(unknown)"},
              {"changed", changed},
              {"reused", reused},
              {"deleted", deleted}};
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    int y = 3 + (int)i;
    tui_set_color(window->win, TUI_COLOR_DIM);
    mvwaddnstr(window->win, y, 3, rows[i].key, 12);
    tui_unset_color(window->win, TUI_COLOR_DIM);
    mvwaddnstr(window->win, y, 17, rows[i].value ? rows[i].value : "",
               window->width - 20);
  }
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2,
                     "o:open URL  Enter/Esc:done");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_tui_deploy_result_key(tui_window_t *window,
                                                        int ch,
                                                        void *userdata) {
  (void)window;
  quick_tui_deploy_result_state_t *state = userdata;
  if (ch == 'o' || ch == 'O') {
    state->open = true;
    return TUI_MODAL_DONE;
  }
  if (ch == '\n' || ch == KEY_ENTER || ch == 27 || ch == 'q' || ch == 'Q') {
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

static void quick_tui_show_deploy_cancelled(
    const quick_deploy_result_t *result) {
  char msg[1200];
  const char *cleanup = result && result->cleanup_message
                            ? result->cleanup_message
                            : (result && result->cleanup_ok
                                   ? "remote staging cleaned"
                                   : "remote cleanup status unavailable");
  if (result && result->cleanup_attempted) {
    if (result->cleanup_ok) {
      snprintf(msg, sizeof(msg),
               "Deploy was cancelled before completion.\n\nCleanup: %s.",
               cleanup);
    } else {
      snprintf(msg, sizeof(msg),
               "Deploy was cancelled before completion.\n\nCleanup failed: "
               "%s\n\nStaging remains: %s",
               cleanup,
               result->cleanup_path ? result->cleanup_path : "(unknown)");
    }
  } else {
    snprintf(msg, sizeof(msg),
             "Deploy was cancelled before completion.\n\nNo remote staging "
             "cleanup was needed.");
  }
  tui_show_message("Deploy cancelled", msg);
}

static void quick_tui_show_deploy_failure(app_error err,
                                          const quick_deploy_result_t *result) {
  char msg[1024];
  snprintf(
      msg, sizeof(msg), "Deploy failed in phase: %s\n\n%s\n\nRemediation:\n%s",
      quick_tui_deploy_phase_label(result->failure_phase),
      result->failure_message ? result->failure_message : app_strerror(err),
      result->bootstrap_install_command
          ? result->bootstrap_install_command
          : "Fix the reported issue and run Deploy again.");
  tui_show_message("Deploy failed", msg);
}

static bool quick_tui_confirm_deploy_overwrite(
    const quick_deploy_plan_t *plan, const quick_deploy_result_t *result) {
  char prompt[512];
  snprintf(prompt, sizeof(prompt), "%s\n\nType %s to confirm:",
           result && result->failure_message ? result->failure_message
                                             : "Confirm overwrite",
           plan ? plan->site : "site");
  char input[128] = {0};
  if (tui_input_dialog("Confirm overwrite", prompt, input, sizeof(input)) !=
      APP_SUCCESS) {
    return false;
  }
  return plan && strcmp(input, plan->site) == 0;
}

static app_error quick_tui_run_deploy_attempt(quick_tui_app_state_t *state,
                                              const quick_deploy_plan_t *plan,
                                              bool allow_unpublished,
                                              bool overwrite_confirmed,
                                              quick_deploy_result_t *result,
                                              bool *cancelled) {
  if (cancelled) {
    *cancelled = false;
  }
  tui_progress_t *progress = tui_progress_create("Deploy", 100);
  quick_tui_deploy_progress_state_t progress_state = {
      .progress = progress, .cancel = 0, .phase = QUICK_DEPLOY_PHASE_NONE};
  if (progress) {
    tui_progress_update(progress, 5, "starting deploy...");
  }
  quick_deploy_options_t options = {.allow_unpublished = allow_unpublished,
                                    .overwrite_confirmed = overwrite_confirmed,
                                    .cancel_flag = tui_interrupt_flag()};
  app_error err = quick_op_deploy_execute(
      NULL, &state->profiles, plan, &options, quick_tui_deploy_progress_cb,
      &progress_state, result);
  if (progress) {
    tui_progress_update(
        progress, 100,
        err == APP_SUCCESS ? "deploy complete" : "deploy failed");
    tui_progress_destroy(progress);
    progress_state.progress = NULL;
  }
  if (cancelled) {
    *cancelled = progress_state.cancel != 0;
  }
  return err;
}

static void quick_tui_run_deploy(quick_tui_app_state_t *state,
                                 const quick_deploy_plan_t *plan) {
  quick_deploy_result_t result;
  quick_deploy_result_init(&result);
  bool cancelled = false;
  bool overwrite_confirmed = false;
  app_error err = quick_tui_run_deploy_attempt(
      state, plan, false, overwrite_confirmed, &result, &cancelled);
  if (err != APP_SUCCESS && result.overwrite_confirmation_required &&
      !cancelled) {
    if (quick_tui_confirm_deploy_overwrite(plan, &result)) {
      overwrite_confirmed = true;
      quick_deploy_result_destroy(&result);
      quick_deploy_result_init(&result);
      err = quick_tui_run_deploy_attempt(
          state, plan, false, overwrite_confirmed, &result, &cancelled);
    } else {
      quick_deploy_result_destroy(&result);
      return;
    }
  }
  if (err != APP_SUCCESS && result.publication_issue && !cancelled) {
    const bool deploy_anyway =
        tui_confirm("Publication incomplete",
                    "This host's IAP/domain is not fully configured. Deploy "
                    "anyway? The site may not be reachable at its public URL.");
    if (deploy_anyway) {
      quick_deploy_result_destroy(&result);
      quick_deploy_result_init(&result);
      err = quick_tui_run_deploy_attempt(state, plan, true, overwrite_confirmed,
                                         &result, &cancelled);
    } else {
      quick_deploy_result_destroy(&result);
      return;
    }
  }
  if (err == APP_SUCCESS) {
    quick_tui_deploy_result_state_t panel = {.result = &result, .open = false};
    (void)tui_modal_run(12, 76, "Deploy complete",
                        quick_tui_deploy_result_redraw,
                        quick_tui_deploy_result_key, &panel);
    if (panel.open && result.url) {
      (void)quick_op_open_url(result.url);
    }
  } else if (err == APP_ERROR_INTERRUPTED || cancelled) {
    quick_tui_show_deploy_cancelled(&result);
  } else {
    quick_tui_show_deploy_failure(err, &result);
  }
  quick_deploy_result_destroy(&result);
}

static void quick_tui_deploy_flow(quick_tui_app_state_t *state,
                                  const char *site_override) {
  (void)quick_tui_reload_profiles(state);
  char path_buf[512] = {0};
  const char *path = NULL;
  if (!site_override && !quick_tui_file_exists("quick.json")) {
    if (tui_input_dialog(
            "Deploy", "Path to site directory (blank for current directory):",
            path_buf, sizeof(path_buf)) != APP_SUCCESS) {
      return;
    }
    path = path_buf[0] ? path_buf : ".";
  }

  char *profile = quick_tui_select_profile(state, false);
  if (!profile) {
    return;
  }

  quick_deploy_plan_t preliminary;
  quick_deploy_plan_init(&preliminary);
  quick_plan_overrides_t base_overrides = {
      .path = path, .site = site_override, .profile = profile};
  app_error err = quick_deploy_plan_resolve(&base_overrides, &state->profiles,
                                            &preliminary);
  if (err != APP_SUCCESS) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Could not resolve deploy plan: %s",
             app_strerror(err));
    tui_show_message("Deploy", msg);
    quick_deploy_plan_destroy(&preliminary);
    free(profile);
    return;
  }

  char site[QUICK_SLUG_MAX + 1];
  char subdomain[QUICK_SLUG_MAX + 1];
  bool ok =
      quick_tui_prompt_slug("Deploy", "Site name", preliminary.site, site) &&
      quick_tui_prompt_slug("Deploy", "Subdomain", preliminary.subdomain,
                            subdomain);
  quick_deploy_plan_destroy(&preliminary);
  if (!ok) {
    free(profile);
    return;
  }

  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  quick_plan_overrides_t overrides = {
      .path = path, .site = site, .subdomain = subdomain, .profile = profile};
  err = quick_deploy_plan_resolve(&overrides, &state->profiles, &plan);
  free(profile);
  if (err != APP_SUCCESS) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Could not resolve deploy plan: %s",
             app_strerror(err));
    tui_show_message("Deploy", msg);
    quick_deploy_plan_destroy(&plan);
    return;
  }

  if (quick_tui_confirm_plan(&plan)) {
    quick_tui_run_deploy(state, &plan);
  }
  quick_deploy_plan_destroy(&plan);
}

void quick_tui_screen_deploy(quick_tui_app_state_t *state) {
  quick_tui_deploy_flow(state, NULL);
}

void quick_tui_screen_deploy_site(quick_tui_app_state_t *state,
                                  const char *site) {
  quick_tui_deploy_flow(state, site);
}
