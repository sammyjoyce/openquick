#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/ops.h"
#include "tui.h"
#include "tui_app_state.h"
#include "tui_internal.h"
#include "tui_panel.h"
#include "tui_product_model.h"

#ifndef ARRAY_LEN
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef enum {
  QUICK_TUI_SITE_BACK_ID = 1000,
} quick_tui_sites_ids_t;

static bool quick_tui_profile_has_ssh(const quick_tui_app_state_t *state) {
  const quick_profile_t *profile = quick_profile_config_find(
      &state->profiles, quick_tui_default_profile_name(state));
  return profile && profile->ssh && profile->ssh[0] != '\0';
}

typedef struct {
  const quick_list_item_t *item;
  char action;
} quick_tui_site_detail_state_t;

static void quick_tui_site_detail_redraw(tui_window_t *window, void *userdata) {
  quick_tui_site_detail_state_t *state = userdata;
  const quick_list_item_t *item = state->item;
  tui_draw_border(window);
  int y = 3;
  const struct {
    const char *key;
    const char *value;
  } rows[] = {
      {"name", item->name},
      {"url", item->url},
      {"release", item->release ? item->release : "(none)"},
      {"updated", item->updated_at ? item->updated_at : "unknown"},
      {"deployer", item->deployer ? item->deployer : "unknown"},
      {"subdomain", item->subdomain ? item->subdomain : item->name},
      {"public",
       item->have_public ? (item->is_public ? "on" : "off") : "unknown"},
      {"source", item->source == QUICK_LIST_SOURCE_REMOTE ? "remote" : "local"},
      {"stale", item->stale ? "yes" : "no"},
  };
  for (size_t i = 0; i < ARRAY_LEN(rows) && y < window->height - 3; i++) {
    tui_set_color(window->win, TUI_COLOR_DIM);
    mvwaddnstr(window->win, y, 3, rows[i].key, 14);
    tui_unset_color(window->win, TUI_COLOR_DIM);
    mvwaddnstr(window->win, y, 18, rows[i].value ? rows[i].value : "",
               window->width - 21);
    y++;
  }
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(
      window->win, window->height - 2,
      "o:open  c:copy  d:deploy  p:public  x:delete  r:refresh  Esc:back");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_tui_site_detail_key(tui_window_t *window,
                                                      int ch, void *userdata) {
  (void)window;
  quick_tui_site_detail_state_t *state = userdata;
  switch (ch) {
  case 'o':
  case 'O':
    state->action = 'o';
    return TUI_MODAL_DONE;
  case 'c':
  case 'C':
    state->action = 'c';
    return TUI_MODAL_DONE;
  case 'd':
  case 'D':
    state->action = 'd';
    return TUI_MODAL_DONE;
  case 'p':
  case 'P':
    state->action = 'p';
    return TUI_MODAL_DONE;
  case 'x':
  case 'X':
    state->action = 'x';
    return TUI_MODAL_DONE;
  case 'r':
  case 'R':
    state->action = 'r';
    return TUI_MODAL_DONE;
  case 27:
  case 'q':
  case 'Q':
    state->action = 0;
    return TUI_MODAL_DONE;
  default:
    return TUI_MODAL_CONTINUE;
  }
}

static char quick_tui_show_site_detail(const quick_list_item_t *item) {
  quick_tui_site_detail_state_t state = {.item = item, .action = 0};
  (void)tui_modal_run(14, 78, "Site details", quick_tui_site_detail_redraw,
                      quick_tui_site_detail_key, &state);
  return state.action;
}

static bool quick_tui_confirm_site_name(const char *title, const char *site,
                                        const char *message) {
  char prompt[256];
  snprintf(prompt, sizeof(prompt), "%s\n\nType %s to confirm:",
           message ? message : "Confirm site action", site ? site : "site");
  char input[128] = {0};
  if (tui_input_dialog(title, prompt, input, sizeof(input)) != APP_SUCCESS) {
    return false;
  }
  return site && strcmp(input, site) == 0;
}

static void quick_tui_delete_site(quick_tui_app_state_t *state,
                                  const char *site) {
  quick_delete_result_t result;
  quick_delete_result_init(&result);
  quick_delete_request_t request = {.profiles = &state->profiles, .site = site};
  app_error err = quick_op_delete(&request, &result);
  if (err == APP_SUCCESS && result.confirmation_required) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Delete remote site '%s'?", site);
    if (quick_tui_confirm_site_name("Delete site", site, msg)) {
      request.confirmed = true;
      quick_delete_result_destroy(&result);
      quick_delete_result_init(&result);
      err = quick_op_delete(&request, &result);
    } else {
      tui_show_message("Delete site",
                       "Confirmation did not match; delete cancelled.");
      quick_delete_result_destroy(&result);
      return;
    }
  }
  if (err == APP_SUCCESS) {
    tui_show_message("Delete site", "Site deleted.");
  } else {
    char msg[256];
    snprintf(msg, sizeof(msg), "Delete failed: %s", app_strerror(err));
    tui_show_message("Delete site", msg);
  }
  quick_delete_result_destroy(&result);
}

static void quick_tui_toggle_public_site(quick_tui_app_state_t *state,
                                         const char *site) {
  quick_public_result_t result;
  quick_public_result_init(&result);
  quick_public_request_t request = {.profiles = &state->profiles,
                                    .site = site,
                                    .action = QUICK_PUBLIC_STATUS};
  app_error err = quick_op_public(&request, &result);
  if (err != APP_SUCCESS) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Could not read public status: %s",
             app_strerror(err));
    tui_show_message("Public site", msg);
    quick_public_result_destroy(&result);
    return;
  }
  const bool turn_on = !result.is_public;
  quick_public_result_destroy(&result);
  quick_public_result_init(&result);
  request.action = turn_on ? QUICK_PUBLIC_ON : QUICK_PUBLIC_OFF;
  if (turn_on) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Make site '%s' public?", site);
    if (!quick_tui_confirm_site_name("Public site", site, msg)) {
      tui_show_message("Public site",
                       "Confirmation did not match; public toggle cancelled.");
      quick_public_result_destroy(&result);
      return;
    }
    request.confirmed = true;
  }
  err = quick_op_public(&request, &result);
  if (err == APP_SUCCESS) {
    tui_show_message("Public site", turn_on ? "Site is now public."
                                            : "Site is no longer public.");
  } else {
    char msg[256];
    snprintf(msg, sizeof(msg), "Public toggle failed: %s", app_strerror(err));
    tui_show_message("Public site", msg);
  }
  quick_public_result_destroy(&result);
}

static void quick_tui_free_site_menu(tui_menu_item_t *items, char **labels,
                                     size_t count) {
  if (labels) {
    for (size_t i = 0; i < count; i++) {
      free(labels[i]);
    }
  }
  free(labels);
  free(items);
}

void quick_tui_screen_sites(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
  (void)quick_tui_reload_profiles(state);

  bool keep_open = true;
  while (keep_open && !tui_interrupted()) {
    tui_progress_t *progress = NULL;
    if (quick_tui_profile_has_ssh(state)) {
      progress = tui_progress_create("Sites", 100);
      if (progress) {
        tui_progress_update(progress, 10, "querying host...");
      }
    }

    quick_list_result_t result;
    quick_list_result_init(&result);
    quick_list_request_t request = {
        .profiles = &state->profiles,
        .overrides = {0},
        .remote = quick_tui_profile_has_ssh(state),
    };
    app_error err = quick_op_list(&request, &result);
    if (progress) {
      tui_progress_update(progress, 100,
                          err == APP_SUCCESS ? "sites loaded" : "list failed");
      tui_progress_destroy(progress);
    }

    if (err != APP_SUCCESS) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Could not list sites: %s", app_strerror(err));
      tui_show_message("Sites", msg);
      quick_list_result_destroy(&result);
      return;
    }

    if (result.count == 0) {
      tui_show_message("Sites",
                       "No deployed sites are known yet.\n\nUse New site to "
                       "scaffold a folder, then Deploy it.");
      quick_list_result_destroy(&result);
      return;
    }

    const size_t item_count = result.count + 2U;
    tui_menu_item_t *items = calloc(item_count, sizeof(tui_menu_item_t));
    char **labels = calloc(result.count, sizeof(char *));
    if (!items || !labels) {
      quick_tui_free_site_menu(items, labels, result.count);
      quick_list_result_destroy(&result);
      tui_show_message("Sites", "Out of memory while building the site list.");
      return;
    }
    for (size_t i = 0; i < result.count; i++) {
      labels[i] = malloc(512);
      if (!labels[i]) {
        continue;
      }
      (void)quick_tui_format_site_row(&result.items[i], labels[i], 512);
      items[i] = (tui_menu_item_t){
          .label = labels[i],
          .description = result.items[i].stale
                             ? "cached local row; remote refresh failed"
                             : "Open site actions",
          .id = (int)i + 1};
    }
    items[result.count] = (tui_menu_item_t){.kind = TUI_MENU_ITEM_SEPARATOR};
    items[result.count + 1U] =
        (tui_menu_item_t){.label = "&Back",
                          .description = "Return to OpenQuick",
                          .id = QUICK_TUI_SITE_BACK_ID};

    tui_window_t *frame = tui_create_centered_window(20, 78);
    if (!frame) {
      quick_tui_free_site_menu(items, labels, result.count);
      quick_list_result_destroy(&result);
      tui_show_message("Sites",
                       "The terminal is too small for the sites list.");
      return;
    }
    tui_push_background(frame);
    tui_menu_result_t selection = tui_show_menu(
        frame,
        &(tui_menu_config_t){
            .title = "Sites",
            .subtitle = "local rows first; remote rows when a profile has ssh",
            .items = items,
            .item_count = (int)item_count,
            .default_index = 0,
            .frame_height = 20,
            .frame_width = 78,
            .enable_search = true,
            .show_numeric_keys = true});
    tui_pop_background();
    tui_destroy_window(frame);

    if (selection.status != TUI_MENU_OK ||
        selection.selected_id == QUICK_TUI_SITE_BACK_ID) {
      keep_open = false;
    } else if (selection.selected_id > 0 &&
               (size_t)selection.selected_id <= result.count) {
      quick_list_item_t *item =
          &result.items[(size_t)selection.selected_id - 1U];
      char action = quick_tui_show_site_detail(item);
      if (action == 'o') {
        app_error open_err = quick_op_open_url(item->url);
        if (open_err != APP_SUCCESS) {
          char msg[256];
          snprintf(msg, sizeof(msg), "Could not open URL: %s",
                   app_strerror(open_err));
          tui_show_message("Open URL", msg);
        }
      } else if (action == 'c') {
        char *copy_msg = NULL;
        app_error copy_err = quick_op_copy_url(item->url, &copy_msg);
        if (copy_err == APP_SUCCESS) {
          tui_show_message("Copy URL", "URL copied to the clipboard.");
        } else {
          tui_show_message("Copy URL",
                           copy_msg ? copy_msg : app_strerror(copy_err));
        }
        free(copy_msg);
      } else if (action == 'd') {
        quick_tui_screen_deploy_site(state, item->name);
      } else if (action == 'p') {
        quick_tui_toggle_public_site(state, item->name);
      } else if (action == 'x') {
        quick_tui_delete_site(state, item->name);
      } else if (action == 'r') {
        keep_open = true;
      }
    }

    quick_tui_free_site_menu(items, labels, result.count);
    quick_list_result_destroy(&result);
  }
}
