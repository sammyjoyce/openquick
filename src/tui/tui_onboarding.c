#include "tui_onboarding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/deploy_plan.h"
#include "../core/ops.h"
#include "tui.h"
#include "tui_internal.h"
#include "tui_panel.h"
#include "tui_screen_host.h"

static char *quick_ob_strdup(const char *value) {
  if (!value) {
    return NULL;
  }
  size_t length = strlen(value);
  char *copy = malloc(length + 1U);
  if (copy) {
    memcpy(copy, value, length + 1U);
  }
  return copy;
}

static void quick_ob_copy(char *destination, size_t size, const char *source) {
  if (destination && size > 0) {
    snprintf(destination, size, "%s", source ? source : "");
  }
}

quick_onboard_choice_t quick_tui_welcome(quick_tui_app_state_t *state,
                                         const quick_context_result_t *ctx) {
  if (!state || !quick_onboarding_transition(
                    &state->onboarding, QUICK_ONBOARD_EVENT_SHOW_WELCOME)) {
    return QUICK_ONBOARD_DISMISSED;
  }

  char subtitle[200];
  if (ctx && ctx->adoptable_folder) {
    snprintf(subtitle, sizeof(subtitle),
             "Deploy static sites to your own host. This folder has files you "
             "can adopt.");
  } else {
    snprintf(subtitle, sizeof(subtitle),
             "Deploy static sites to your own host. Start with a local preview "
             "\xE2\x80\x94 no host needed.");
  }
  const tui_menu_item_t items[] = {
      {.label = "&Try OpenQuick locally",
       .description =
           "Recommended: create a site and preview it in your browser",
       .id = QUICK_ONBOARD_LOCAL},
      {.label = "&Use an existing project",
       .description = "Adopt files already in a folder, then preview locally",
       .id = QUICK_ONBOARD_USE_EXISTING},
      {.label = "&Connect a deployment host",
       .description = "Save a connection to a host that already runs OpenQuick",
       .id = QUICK_ONBOARD_CONNECT_HOST},
      {.label = "&Set up a new host",
       .description = "Install OpenQuick on a Linux server over SSH",
       .id = QUICK_ONBOARD_NEW_HOST},
      {.kind = TUI_MENU_ITEM_SEPARATOR},
      {.label = "Open the &advanced dashboard",
       .description = "Sites, Deploy, Doctor, Serve, Settings (expert view)",
       .id = QUICK_ONBOARD_DASHBOARD},
  };
  tui_menu_result_t result = tui_show_menu(
      NULL, &(tui_menu_config_t){
                .title = "Welcome to OpenQuick",
                .subtitle = subtitle,
                .items = items,
                .item_count = (int)(sizeof(items) / sizeof(items[0])),
                .default_index = 0,
                .frame_height = 18,
                .frame_width = 74,
                .show_numeric_keys = true});
  if (result.status != TUI_MENU_OK) {
    return QUICK_ONBOARD_DISMISSED;
  }
  return (quick_onboard_choice_t)result.selected_id;
}

/* ---- Local quickstart ---- */

typedef struct {
  const quick_init_result_t *result;
  const char *url;
  bool serving;
  char action; /* 'o' open, 'c' connect host, 0 finish */
} quick_ob_done_state_t;

static void quick_ob_done_redraw(tui_window_t *window, void *userdata) {
  quick_ob_done_state_t *state = userdata;
  const quick_init_result_t *result = state->result;
  tui_draw_border(window);
  char header[256];
  snprintf(header, sizeof(header), "Your site \"%s\" is ready",
           result && result->site ? result->site : "site");
  tui_set_color(window->win, TUI_COLOR_SUCCESS);
  mvwaddnstr(window->win, 2, 3, header, window->width - 6);
  tui_unset_color(window->win, TUI_COLOR_SUCCESS);

  int y = 4;
  if (result && result->path) {
    char line[300];
    snprintf(line, sizeof(line), "folder   %s", result->path);
    mvwaddnstr(window->win, y++, 3, line, window->width - 6);
  }
  if (state->serving && state->url) {
    char line[300];
    snprintf(line, sizeof(line), "preview  %s", state->url);
    tui_set_color(window->win, TUI_COLOR_INFO);
    mvwaddnstr(window->win, y++, 3, line, window->width - 6);
    tui_unset_color(window->win, TUI_COLOR_INFO);
  } else {
    mvwaddnstr(window->win, y++, 3,
               "preview  not running (start it from Serve)", window->width - 6);
  }
  y++;
  mvwaddnstr(window->win, y++, 3, "Next steps:", window->width - 6);
  mvwaddnstr(window->win, y++, 5,
             "- Edit the files in the folder above, then refresh the browser.",
             window->width - 8);
  mvwaddnstr(window->win, y++, 5,
             "- Deploy later once you connect or set up a host.",
             window->width - 8);
  mvwaddnstr(window->win, y++, 5,
             "- A profile is a saved host connection (set one up anytime).",
             window->width - 8);

  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2,
                     state->serving ? "o:open  c:connect host  Enter/Esc:done"
                                    : "c:connect host  Enter/Esc:done");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_ob_done_key(tui_window_t *window, int ch,
                                              void *userdata) {
  (void)window;
  quick_ob_done_state_t *state = userdata;
  if ((ch == 'o' || ch == 'O') && state->serving) {
    state->action = 'o';
    return TUI_MODAL_DONE;
  }
  if (ch == 'c' || ch == 'C') {
    state->action = 'c';
    return TUI_MODAL_DONE;
  }
  if (ch == '\n' || ch == KEY_ENTER || ch == 27 || ch == 'q' || ch == 'Q') {
    state->action = 0;
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

static bool quick_ob_prompt_value(const char *title, const char *label,
                                  char *value, size_t value_size) {
  char prompt[700];
  snprintf(prompt, sizeof(prompt), "%s [%s]:", label,
           value[0] ? value : "none");
  char input[QUICK_ONBOARDING_PATH_MAX] = {0};
  if (tui_input_dialog(title, prompt, input, sizeof(input)) != APP_SUCCESS) {
    return false;
  }
  if (input[0]) {
    quick_ob_copy(value, value_size, input);
  }
  return true;
}

static void quick_ob_basename(const char *path, char *out, size_t out_size) {
  if (!path || !path[0] || strcmp(path, ".") == 0) {
    quick_ob_copy(out, out_size, "site");
    return;
  }
  char normalized_path[QUICK_ONBOARDING_PATH_MAX];
  quick_ob_copy(normalized_path, sizeof(normalized_path), path);
  size_t length = strlen(normalized_path);
  while (length > 1U && normalized_path[length - 1U] == '/') {
    normalized_path[--length] = '\0';
  }
  const char *slash = strrchr(normalized_path, '/');
  quick_ob_copy(out, out_size, slash && slash[1] ? slash + 1 : normalized_path);
}

static bool quick_ob_choose_template(quick_onboarding_model_t *model) {
  const tui_menu_item_t items[] = {
      {.label = "&Blank", .description = "A simple static HTML page", .id = 1},
      {.label = "&Realtime",
       .description = "Starter that uses the OpenQuick SDK (/_quick/sdk.js)",
       .id = 2},
  };
  int default_index =
      strcmp(model->values.template_name, "realtime") == 0 ? 1 : 0;
  tui_menu_result_t result = tui_show_menu(
      NULL,
      &(tui_menu_config_t){.title = "Choose a starter",
                           .subtitle = "Starter template for the local project",
                           .items = items,
                           .item_count = 2,
                           .default_index = default_index,
                           .frame_height = 12,
                           .frame_width = 72,
                           .show_numeric_keys = true});
  if (result.status != TUI_MENU_OK) {
    return false;
  }
  quick_ob_copy(model->values.template_name,
                sizeof(model->values.template_name),
                result.selected_id == 2 ? "realtime" : "blank");
  return true;
}

typedef struct {
  const quick_onboarding_model_t *model;
  bool confirmed;
} quick_ob_review_state_t;

static void quick_ob_review_redraw(tui_window_t *window, void *userdata) {
  quick_ob_review_state_t *state = userdata;
  const quick_onboarding_model_t *model = state->model;
  tui_draw_border(window);
  tui_set_color(window->win, TUI_COLOR_TITLE);
  tui_print_centered(window->win, 2,
                     model->values.adopt_existing ? "Review: adopt this folder"
                                                  : "Review: create this site");
  tui_unset_color(window->win, TUI_COLOR_TITLE);
  int y = 4;
  char line[600];
  snprintf(line, sizeof(line), "folder      %s", model->values.project_dir);
  mvwaddnstr(window->win, y++, 3, line, window->width - 6);
  snprintf(line, sizeof(line), "site name   %s", model->values.site_name);
  mvwaddnstr(window->win, y++, 3, line, window->width - 6);
  snprintf(line, sizeof(line), "template    %s",
           model->values.adopt_existing ? "adopt existing files"
                                        : model->values.template_name);
  mvwaddnstr(window->win, y++, 3, line, window->width - 6);
  y++;
  mvwaddnstr(window->win, y++, 3,
             model->values.adopt_existing
                 ? "OpenQuick adds metadata only where files are missing."
                 : "OpenQuick creates the reviewed starter files.",
             window->width - 6);
  mvwaddnstr(window->win, y++, 3,
             model->values.adopt_existing
                 ? "Existing files are never overwritten by adopt mode."
                 : "Existing target files make creation stop safely.",
             window->width - 6);
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2,
                     "Enter: apply  Esc/q: back");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_ob_review_key(tui_window_t *window, int ch,
                                                void *userdata) {
  (void)window;
  quick_ob_review_state_t *state = userdata;
  if (ch == '\n' || ch == KEY_ENTER) {
    state->confirmed = true;
    return TUI_MODAL_DONE;
  }
  if (ch == 27 || ch == 'q' || ch == 'Q') {
    state->confirmed = false;
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

static void quick_ob_show_validation(const char *title,
                                     quick_onboarding_model_t *model) {
  if (!model->validation.valid && model->validation.message[0]) {
    tui_show_message(title, model->validation.message);
  }
}

void quick_tui_local_quickstart(quick_tui_app_state_t *state,
                                bool adopt_default) {
  if (!state) {
    return;
  }
  quick_onboarding_model_t *model = &state->onboarding;
  (void)adopt_default;
  if (model->state != QUICK_ONBOARD_STATE_LOCAL_DIRECTORY ||
      model->flow != QUICK_ONBOARD_FLOW_LOCAL) {
    return;
  }
  (void)quick_tui_reload_profiles(state);

  const char *title = "Preview this project locally";
  for (;;) {
    switch (model->state) {
    case QUICK_ONBOARD_STATE_LOCAL_DIRECTORY:
      if (!model->values.project_dir[0]) {
        quick_ob_copy(model->values.project_dir,
                      sizeof(model->values.project_dir), ".");
      }
      quick_ob_show_validation(title, model);
      if (!quick_ob_prompt_value(
              title,
              model->values.adopt_existing ? "Folder to adopt"
                                           : "Folder to create",
              model->values.project_dir, sizeof(model->values.project_dir))) {
        (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_CANCEL);
        return;
      }
      quick_onboarding_clear_validation(model);
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_NEXT);
      break;

    case QUICK_ONBOARD_STATE_LOCAL_IDENTITY: {
      if (!model->values.site_name[0]) {
        char basename[QUICK_ONBOARDING_NAME_MAX];
        char seed[QUICK_SLUG_MAX + 1];
        quick_ob_basename(model->values.project_dir, basename,
                          sizeof(basename));
        if (quick_slug_normalize(basename, seed) == APP_SUCCESS) {
          quick_ob_copy(model->values.site_name,
                        sizeof(model->values.site_name), seed);
        }
      }
      char current[QUICK_ONBOARDING_NAME_MAX];
      quick_ob_copy(current, sizeof(current), model->values.site_name);
      if (!quick_ob_prompt_value(title, "Site name", current,
                                 sizeof(current))) {
        (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_BACK);
        break;
      }
      char slug[QUICK_SLUG_MAX + 1];
      if (!current[0] || quick_slug_normalize(current, slug) != APP_SUCCESS ||
          !quick_slug_is_valid(slug)) {
        quick_onboarding_set_validation(
            model, APP_ERROR_VALIDATION,
            "That name cannot be turned into a web address label. Use letters, "
            "digits, and hyphens.");
        quick_ob_show_validation(title, model);
        break;
      }
      quick_ob_copy(model->values.site_name, sizeof(model->values.site_name),
                    slug);
      quick_onboarding_clear_validation(model);
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_NEXT);
      break;
    }

    case QUICK_ONBOARD_STATE_LOCAL_TEMPLATE:
      if (!quick_ob_choose_template(model)) {
        (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_BACK);
        break;
      }
      quick_onboarding_clear_validation(model);
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_NEXT);
      break;

    case QUICK_ONBOARD_STATE_LOCAL_REVIEW: {
      quick_ob_review_state_t review = {.model = model, .confirmed = false};
      (void)tui_modal_run(16, 76, "Review", quick_ob_review_redraw,
                          quick_ob_review_key, &review);
      (void)quick_onboarding_transition(model, review.confirmed
                                                   ? QUICK_ONBOARD_EVENT_CONFIRM
                                                   : QUICK_ONBOARD_EVENT_BACK);
      break;
    }

    case QUICK_ONBOARD_STATE_LOCAL_APPLY: {
      (void)quick_onboarding_transition(model,
                                        QUICK_ONBOARD_EVENT_BEGIN_MUTATION);
      quick_init_result_t result;
      quick_init_result_init(&result);
      quick_init_request_t request = {
          .target_dir = model->values.project_dir,
          .name = model->values.site_name,
          .template_kind = strcmp(model->values.template_name, "realtime") == 0
                               ? QUICK_INIT_TEMPLATE_REALTIME
                               : QUICK_INIT_TEMPLATE_BLANK,
          .profile = model->values.profile[0] ? model->values.profile : NULL,
          .adopt_existing = model->values.adopt_existing,
      };
      app_error err = quick_op_init(&request, &result);
      if (err != APP_SUCCESS) {
        char message[420];
        if (err == APP_ERROR_CONFIG_INVALID && !model->values.adopt_existing) {
          snprintf(
              message, sizeof(message),
              "This folder already has files OpenQuick would create. Go "
              "back and choose Use an existing project to adopt it safely.");
        } else {
          snprintf(message, sizeof(message), "Could not set up the project: %s",
                   app_strerror(err));
        }
        quick_onboarding_set_validation(model, err, message);
        (void)quick_onboarding_transition(model,
                                          QUICK_ONBOARD_EVENT_VERIFY_FAILED);
        tui_show_message(title, message);
        quick_init_result_destroy(&result);
        if (tui_confirm(title, "Edit the values and retry?")) {
          (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_RETRY);
          quick_onboarding_set_validation(
              model, APP_ERROR_IO,
              "The previous attempt may have created some project files. "
              "Review the folder before retrying; OpenQuick will not overwrite "
              "arbitrary existing files.");
          break;
        }
        return;
      }

      quick_onboarding_note_check(model, QUICK_ONBOARD_CHECK_PROJECT, true);
      (void)quick_onboarding_transition(model, QUICK_ONBOARD_EVENT_NEXT);

      bool serving = false;
      char *url = NULL;
      app_error serve_err =
          quick_tui_start_serve_child(state, result.path, "9366", NULL);
      if (serve_err == APP_SUCCESS) {
        serving = true;
        url = quick_ob_strdup(state->serve_url);
      } else {
        (void)quick_op_serve_local_url(result.site, "9366", &url);
        char message[320];
        snprintf(
            message, sizeof(message),
            "Your site was created, but the preview server could not start "
            "(%s). You can start it later from Serve.",
            app_strerror(serve_err));
        tui_show_message(title, message);
      }

      bool done = false;
      while (!done && model->state == QUICK_ONBOARD_STATE_LOCAL_PREVIEW) {
        quick_ob_done_state_t panel = {
            .result = &result, .url = url, .serving = serving, .action = 0};
        (void)tui_modal_run(16, 78, "Preview ready", quick_ob_done_redraw,
                            quick_ob_done_key, &panel);
        if (panel.action == 'o' && serving && url) {
          (void)quick_op_open_url(url);
          continue; /* Opening the browser does not finish onboarding. */
        }
        if (panel.action == 'c') {
          (void)quick_onboarding_transition(model,
                                            QUICK_ONBOARD_EVENT_CHOOSE_CONNECT);
        } else {
          (void)quick_onboarding_transition(model,
                                            QUICK_ONBOARD_EVENT_COMPLETE);
        }
        done = true;
      }
      free(url);
      quick_init_result_destroy(&result);
      return;
    }

    case QUICK_ONBOARD_STATE_CANCELLED:
    case QUICK_ONBOARD_STATE_COMPLETE:
    case QUICK_ONBOARD_STATE_FAILED:
    default:
      return;
    }
  }
}

bool quick_tui_onboarding_dispatch_choice(
    quick_tui_app_state_t *state, quick_onboard_choice_t choice,
    quick_onboarding_return_t return_destination) {
  if (!state) {
    return false;
  }
  state->onboarding.return_destination = return_destination;

  switch (choice) {
  case QUICK_ONBOARD_LOCAL:
  case QUICK_ONBOARD_USE_EXISTING: {
    quick_onboarding_event_t event = choice == QUICK_ONBOARD_USE_EXISTING
                                         ? QUICK_ONBOARD_EVENT_CHOOSE_ADOPT
                                         : QUICK_ONBOARD_EVENT_CHOOSE_LOCAL;
    if (!quick_onboarding_transition(&state->onboarding, event)) {
      return false;
    }
    quick_tui_local_quickstart(state, choice == QUICK_ONBOARD_USE_EXISTING);
    if (state->onboarding.flow == QUICK_ONBOARD_FLOW_CONNECT_HOST &&
        state->onboarding.state == QUICK_ONBOARD_STATE_HOST_FIELDS) {
      return quick_tui_screen_connect_host(state);
    }
    return true;
  }
  case QUICK_ONBOARD_CONNECT_HOST:
    if (!quick_onboarding_transition(&state->onboarding,
                                     QUICK_ONBOARD_EVENT_CHOOSE_CONNECT)) {
      return false;
    }
    return quick_tui_screen_connect_host(state);
  case QUICK_ONBOARD_NEW_HOST:
    if (!quick_onboarding_transition(&state->onboarding,
                                     QUICK_ONBOARD_EVENT_CHOOSE_INSTALL)) {
      return false;
    }
    return quick_tui_screen_new_host(state);
  case QUICK_ONBOARD_DASHBOARD:
    return quick_onboarding_transition(&state->onboarding,
                                       QUICK_ONBOARD_EVENT_COMPLETE);
  case QUICK_ONBOARD_DISMISSED:
  case QUICK_ONBOARD_NONE:
  default:
    (void)quick_onboarding_transition(&state->onboarding,
                                      QUICK_ONBOARD_EVENT_CANCEL);
    return true; /* Dismissal always returns control to the dashboard. */
  }
}

bool quick_tui_onboarding_launch(quick_tui_app_state_t *state) {
  if (!state) {
    return true;
  }
  (void)quick_tui_reload_profiles(state);
  quick_context_result_t context;
  quick_context_result_init(&context);
  quick_context_request_t request = {.profiles = &state->profiles, .dir = NULL};
  (void)quick_op_classify_context(&request, &context);
  if (!context.show_welcome) {
    quick_context_result_destroy(&context);
    return true;
  }

  bool looping = true;
  while (looping) {
    quick_onboard_choice_t choice = quick_tui_welcome(state, &context);
    (void)quick_tui_onboarding_dispatch_choice(state, choice,
                                               QUICK_ONBOARD_RETURN_DASHBOARD);
    if (choice == QUICK_ONBOARD_DASHBOARD ||
        choice == QUICK_ONBOARD_DISMISSED || choice == QUICK_ONBOARD_NONE) {
      break;
    }

    quick_context_result_destroy(&context);
    quick_context_result_init(&context);
    (void)quick_op_classify_context(&request, &context);
    looping = context.show_welcome;
  }
  quick_context_result_destroy(&context);
  return true;
}
