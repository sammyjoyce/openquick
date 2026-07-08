#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/deploy_plan.h"
#include "../core/ops.h"
#include "tui.h"
#include "tui_app_state.h"
#include "tui_internal.h"

static char *quick_tui_dup(const char *value) {
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

static quick_init_template_t quick_tui_choose_template(void) {
  const tui_menu_item_t items[] = {
      {.label = "&Blank", .description = "Static HTML starter", .id = 1},
      {.label = "&Realtime",
       .description = "Starter page that imports /_quick/sdk.js",
       .id = 2},
  };
  tui_menu_result_t r =
      tui_show_menu(NULL, &(tui_menu_config_t){.title = "New site template",
                                               .subtitle = "Choose scaffold",
                                               .items = items,
                                               .item_count = 2,
                                               .default_index = 0,
                                               .frame_height = 12,
                                               .frame_width = 66,
                                               .show_numeric_keys = true});
  if (r.status == TUI_MENU_OK && r.selected_id == 2) {
    return QUICK_INIT_TEMPLATE_REALTIME;
  }
  return QUICK_INIT_TEMPLATE_BLANK;
}

static char *quick_tui_choose_init_profile(quick_tui_app_state_t *state) {
  if (!state || state->profiles.profile_count == 0) {
    return quick_tui_dup("");
  }
  const size_t count = state->profiles.profile_count + 1U;
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
  items[state->profiles.profile_count] =
      (tui_menu_item_t){.label = "&None",
                        .description = "Leave profile unset in quick.json",
                        .id = 900};
  tui_menu_result_t r = tui_show_menu(
      NULL,
      &(tui_menu_config_t){.title = "New site profile",
                           .subtitle = "Default profile can be changed later",
                           .items = items,
                           .item_count = (int)count,
                           .default_index = 0,
                           .frame_height = 16,
                           .frame_width = 70,
                           .enable_search = true,
                           .show_numeric_keys = true});
  char *profile = NULL;
  if (r.status == TUI_MENU_OK && r.selected_id >= 1 &&
      (size_t)r.selected_id <= state->profiles.profile_count) {
    profile = quick_tui_dup(
        state->profiles.profiles[(size_t)r.selected_id - 1U].name);
  } else if (r.status == TUI_MENU_OK && r.selected_id == 900) {
    profile = quick_tui_dup("");
  }
  free(items);
  return profile;
}

typedef struct {
  const quick_init_result_t *result;
  bool deploy;
} quick_tui_created_state_t;

static void quick_tui_created_redraw(tui_window_t *window, void *userdata) {
  quick_tui_created_state_t *state = userdata;
  const quick_init_result_t *result = state->result;
  tui_draw_border(window);
  char header[256];
  snprintf(header, sizeof(header), "Created site %s in %s",
           result->site ? result->site : "(unknown)",
           result->path ? result->path : "(unknown)");
  tui_set_color(window->win, TUI_COLOR_SUCCESS);
  mvwaddnstr(window->win, 3, 3, header, window->width - 6);
  tui_unset_color(window->win, TUI_COLOR_SUCCESS);
  mvwaddnstr(window->win, 5, 3, "Files created:", window->width - 6);
  for (size_t i = 0; i < result->file_count && 6 + (int)i < window->height - 3;
       i++) {
    mvwprintw(window->win, 6 + (int)i, 5, "- %.*s", window->width - 9,
              result->files_created[i] ? result->files_created[i] : "");
  }
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2,
                     "d:deploy now  Enter/Esc:done");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_tui_created_key(tui_window_t *window, int ch,
                                                  void *userdata) {
  (void)window;
  quick_tui_created_state_t *state = userdata;
  if (ch == 'd' || ch == 'D') {
    state->deploy = true;
    return TUI_MODAL_DONE;
  }
  if (ch == '\n' || ch == KEY_ENTER || ch == 27 || ch == 'q' || ch == 'Q') {
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

void quick_tui_screen_init(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
  (void)quick_tui_reload_profiles(state);
  char dir[512] = {0};
  if (tui_input_dialog(
          "New site", "Directory to create (blank for current directory):", dir,
          sizeof(dir)) != APP_SUCCESS) {
    return;
  }
  char name_input[160] = {0};
  if (tui_input_dialog("New site", "Site name:", name_input,
                       sizeof(name_input)) != APP_SUCCESS) {
    return;
  }
  char slug[QUICK_SLUG_MAX + 1];
  if (quick_slug_normalize(name_input[0] ? name_input : dir, slug) !=
          APP_SUCCESS ||
      !quick_slug_is_valid(slug)) {
    tui_show_message("New site",
                     "The site name cannot be normalized to a DNS label.");
    return;
  }
  if (name_input[0] && strcmp(name_input, slug) != 0) {
    char msg[160];
    snprintf(msg, sizeof(msg), "Using normalized DNS label: %s", slug);
    tui_show_message("New site", msg);
  }
  quick_init_template_t template_kind = quick_tui_choose_template();
  char *profile = quick_tui_choose_init_profile(state);
  if (!profile) {
    return;
  }

  quick_init_result_t result;
  quick_init_result_init(&result);
  quick_init_request_t request = {.target_dir = dir[0] ? dir : ".",
                                  .name = slug,
                                  .template_kind = template_kind,
                                  .profile = profile[0] ? profile : NULL};
  app_error err = quick_op_init(&request, &result);
  free(profile);
  if (err != APP_SUCCESS) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Could not create site: %s", app_strerror(err));
    tui_show_message("New site", msg);
    quick_init_result_destroy(&result);
    return;
  }

  quick_tui_created_state_t panel = {.result = &result, .deploy = false};
  (void)tui_modal_run(14, 78, "Site created", quick_tui_created_redraw,
                      quick_tui_created_key, &panel);
  if (panel.deploy) {
    quick_tui_screen_deploy_site(state, result.site);
  }
  quick_init_result_destroy(&result);
}
