#include "tui_app_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/deploy_plan.h"
#include "../core/site_config.h"
#include "tui.h"
#include "tui_internal.h"
#include "tui_panel.h"
#include "tui_product_model.h"

static char *quick_tui_dup_config(const char *value) {
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

static bool quick_tui_set_string(char **slot, const char *value) {
  char *copy = value && value[0] ? quick_tui_dup_config(value) : NULL;
  if (value && value[0] && !copy) {
    return false;
  }
  free(*slot);
  *slot = copy;
  return true;
}

static bool quick_tui_parse_bool(const char *value, bool *out) {
  if (!value || !out) {
    return false;
  }
  if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
      strcmp(value, "yes") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0 ||
      strcmp(value, "no") == 0) {
    *out = false;
    return true;
  }
  return false;
}

typedef struct {
  quick_tui_app_state_t *state;
  quick_profile_t *profile;
  char action;
  bool dirty;
} quick_tui_profile_panel_state_t;

static void quick_tui_profile_panel_redraw(tui_window_t *window,
                                           void *userdata) {
  quick_tui_profile_panel_state_t *panel = userdata;
  quick_profile_t *p = panel->profile;
  tui_draw_border(window);
  char del[16];
  char open_after[16];
  snprintf(del, sizeof(del), "%s",
           (!p->deploy.has_delete || p->deploy.delete) ? "true" : "false");
  snprintf(open_after, sizeof(open_after), "%s",
           p->deploy.has_open_after_deploy && p->deploy.open_after_deploy
               ? "true"
               : "false");
  const struct {
    const char *key;
    const char *value;
  } rows[] = {{"name", p->name},
              {"ssh", p->ssh ? p->ssh : ""},
              {"remote_root", p->remote_root ? p->remote_root : ""},
              {"base_domain", p->base_domain ? p->base_domain : ""},
              {"base_url", p->base_url ? p->base_url : ""},
              {"iap.type", p->iap.type ? p->iap.type : ""},
              {"iap.mode", p->iap.mode ? p->iap.mode : ""},
              {"iap.team_domain", p->iap.team_domain ? p->iap.team_domain : ""},
              {"iap.audience", p->iap.audience ? p->iap.audience : ""},
              {"deploy.delete", del},
              {"deploy.open_after_deploy", open_after}};
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]) && 3 + (int)i < window->height - 3; i++) {
    int y = 3 + (int)i;
    tui_set_color(window->win, TUI_COLOR_DIM);
    mvwaddnstr(window->win, y, 3, rows[i].key, 24);
    tui_unset_color(window->win, TUI_COLOR_DIM);
    mvwaddnstr(window->win, y, 29, rows[i].value ? rows[i].value : "", window->width - 32);
  }
  if (panel->dirty) {
    tui_set_color(window->win, TUI_COLOR_WARNING);
    tui_print_centered(window->win, window->height - 3,
                       "Unsaved changes - write, save on back, or discard");
    tui_unset_color(window->win, TUI_COLOR_WARNING);
  }
  tui_set_color(window->win, TUI_COLOR_INFO);
  tui_print_centered(window->win, window->height - 2,
                     "e:edit field  s:set default  w:write  Esc:back");
  tui_unset_color(window->win, TUI_COLOR_INFO);
}

static tui_modal_decision_t quick_tui_profile_panel_key(tui_window_t *window,
                                                        int ch,
                                                        void *userdata) {
  (void)window;
  quick_tui_profile_panel_state_t *panel = userdata;
  if (ch == 'e' || ch == 'E' || ch == 's' || ch == 'S' || ch == 'w' ||
      ch == 'W') {
    panel->action = (char)((ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch);
    return TUI_MODAL_DONE;
  }
  if (ch == 27 || ch == 'q' || ch == 'Q') {
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

static const char *quick_tui_profile_field_value(quick_profile_t *p,
                                                 const char *field,
                                                 char *scratch,
                                                 size_t scratch_size) {
  if (strcmp(field, "ssh") == 0) return p->ssh ? p->ssh : "";
  if (strcmp(field, "remote_root") == 0) return p->remote_root ? p->remote_root : "";
  if (strcmp(field, "base_domain") == 0) return p->base_domain ? p->base_domain : "";
  if (strcmp(field, "base_url") == 0) return p->base_url ? p->base_url : "";
  if (strcmp(field, "iap.type") == 0) return p->iap.type ? p->iap.type : "";
  if (strcmp(field, "iap.mode") == 0) return p->iap.mode ? p->iap.mode : "";
  if (strcmp(field, "iap.team_domain") == 0) return p->iap.team_domain ? p->iap.team_domain : "";
  if (strcmp(field, "iap.audience") == 0) return p->iap.audience ? p->iap.audience : "";
  if (strcmp(field, "deploy.delete") == 0) {
    snprintf(scratch, scratch_size, "%s",
             (!p->deploy.has_delete || p->deploy.delete) ? "true" : "false");
    return scratch;
  }
  if (strcmp(field, "deploy.open_after_deploy") == 0) {
    snprintf(scratch, scratch_size, "%s",
             p->deploy.has_open_after_deploy && p->deploy.open_after_deploy
                 ? "true"
                 : "false");
    return scratch;
  }
  return "";
}

static void quick_tui_profile_apply_field(quick_profile_t *p,
                                          const char *field,
                                          const char *value) {
  if (strcmp(field, "ssh") == 0) (void)quick_tui_set_string(&p->ssh, value);
  else if (strcmp(field, "remote_root") == 0) (void)quick_tui_set_string(&p->remote_root, value);
  else if (strcmp(field, "base_domain") == 0) (void)quick_tui_set_string(&p->base_domain, value);
  else if (strcmp(field, "base_url") == 0) (void)quick_tui_set_string(&p->base_url, value);
  else if (strcmp(field, "iap.type") == 0) (void)quick_tui_set_string(&p->iap.type, value);
  else if (strcmp(field, "iap.mode") == 0) (void)quick_tui_set_string(&p->iap.mode, value);
  else if (strcmp(field, "iap.team_domain") == 0) (void)quick_tui_set_string(&p->iap.team_domain, value);
  else if (strcmp(field, "iap.audience") == 0) (void)quick_tui_set_string(&p->iap.audience, value);
  else if (strcmp(field, "deploy.delete") == 0) {
    bool parsed = false;
    if (quick_tui_parse_bool(value, &parsed)) {
      p->deploy.delete = parsed;
      p->deploy.has_delete = true;
    }
  } else if (strcmp(field, "deploy.open_after_deploy") == 0) {
    bool parsed = false;
    if (quick_tui_parse_bool(value, &parsed)) {
      p->deploy.open_after_deploy = parsed;
      p->deploy.has_open_after_deploy = true;
    }
  }
}

static bool quick_tui_edit_profile_field(quick_profile_t *profile) {
  static const char *const fields[] = {"ssh", "remote_root", "base_domain",
                                       "base_url", "iap.type", "iap.mode",
                                       "iap.team_domain", "iap.audience",
                                       "deploy.delete",
                                       "deploy.open_after_deploy"};
  tui_menu_item_t items[sizeof(fields) / sizeof(fields[0])];
  for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
    items[i] = (tui_menu_item_t){.label = fields[i], .id = (int)i + 1};
  }
  tui_menu_result_t r = tui_show_menu(
      NULL, &(tui_menu_config_t){.title = "Edit profile field",
                                 .subtitle = "Select a field to edit",
                                 .items = items,
                                 .item_count = (int)(sizeof(fields) / sizeof(fields[0])),
                                 .default_index = 0,
                                 .frame_height = 18,
                                 .frame_width = 70,
                                 .enable_search = true,
                                 .show_numeric_keys = true});
  if (r.status != TUI_MENU_OK || r.selected_id < 1 ||
      (size_t)r.selected_id > sizeof(fields) / sizeof(fields[0])) {
    return false;
  }
  const char *field = fields[(size_t)r.selected_id - 1U];
  char scratch[32];
  char prompt[256];
  snprintf(prompt, sizeof(prompt), "%s [%s]:", field,
           quick_tui_profile_field_value(profile, field, scratch,
                                         sizeof(scratch)));
  char value[512];
  if (tui_input_dialog("Edit profile", prompt, value, sizeof(value)) !=
      APP_SUCCESS) {
    return false;
  }
  char message[160];
  if (!quick_tui_validate_profile_field(field, value, message,
                                        sizeof(message))) {
    tui_show_message("Edit profile", message);
    return false;
  }
  quick_tui_profile_apply_field(profile, field, value);
  return true;
}

typedef enum {
  QUICK_TUI_UNSAVED_CANCEL = 1,
  QUICK_TUI_UNSAVED_SAVE,
  QUICK_TUI_UNSAVED_DISCARD,
} quick_tui_unsaved_decision_t;

static quick_tui_unsaved_decision_t quick_tui_profile_unsaved_decision(
    const char *profile_name) {
  const tui_menu_item_t items[] = {
      {.label = "&Save changes",
       .description = "Write config.json, then leave the profile editor",
       .id = QUICK_TUI_UNSAVED_SAVE},
      {.label = "&Discard changes",
       .description = "Reload config.json and leave without saving edits",
       .id = QUICK_TUI_UNSAVED_DISCARD},
      {.label = "&Cancel",
       .description = "Return to the profile editor with edits intact",
       .id = QUICK_TUI_UNSAVED_CANCEL},
  };
  char subtitle[192];
  snprintf(subtitle, sizeof(subtitle), "%s has changes not written to config.json",
           profile_name ? profile_name : "Profile");
  tui_menu_result_t r = tui_show_menu(
      NULL, &(tui_menu_config_t){.title = "Unsaved profile changes",
                                 .subtitle = subtitle,
                                 .items = items,
                                 .item_count = (int)(sizeof(items) / sizeof(items[0])),
                                 .default_index = 2,
                                 .frame_height = 12,
                                 .frame_width = 72,
                                 .show_numeric_keys = true});
  if (r.status != TUI_MENU_OK) {
    return QUICK_TUI_UNSAVED_CANCEL;
  }
  if (r.selected_id == QUICK_TUI_UNSAVED_SAVE) {
    return QUICK_TUI_UNSAVED_SAVE;
  }
  if (r.selected_id == QUICK_TUI_UNSAVED_DISCARD) {
    return QUICK_TUI_UNSAVED_DISCARD;
  }
  return QUICK_TUI_UNSAVED_CANCEL;
}

static bool quick_tui_write_profiles(quick_tui_app_state_t *state,
                                     const char *title) {
  app_error err = quick_profile_config_write_file(
      quick_tui_profile_config_path(state), &state->profiles);
  tui_show_message(title,
                   err == APP_SUCCESS ? "Profile config written."
                                      : app_strerror(err));
  return err == APP_SUCCESS;
}

static void quick_tui_show_profile(quick_tui_app_state_t *state,
                                   quick_profile_t *profile) {
  bool open = true;
  bool dirty = false;
  while (open && !tui_interrupted()) {
    quick_tui_profile_panel_state_t panel = {.state = state,
                                             .profile = profile,
                                             .action = 0,
                                             .dirty = dirty};
    (void)tui_modal_run(18, 80, "Profile", quick_tui_profile_panel_redraw,
                        quick_tui_profile_panel_key, &panel);
    if (panel.action == 'e') {
      if (quick_tui_edit_profile_field(profile)) {
        dirty = true;
      }
    } else if (panel.action == 's') {
      (void)quick_tui_set_string(&state->profiles.default_profile,
                                 profile->name);
      dirty = true;
      tui_show_message("Profile", "Default profile updated in memory. Press w to write config.json.");
    } else if (panel.action == 'w') {
      if (tui_confirm("Write profiles", "Write profile config to disk?")) {
        if (quick_tui_write_profiles(state, "Write profiles")) {
          dirty = false;
        }
      }
    } else if (dirty) {
      quick_tui_unsaved_decision_t decision =
          quick_tui_profile_unsaved_decision(profile->name);
      if (decision == QUICK_TUI_UNSAVED_SAVE) {
        if (quick_tui_write_profiles(state, "Unsaved profile changes")) {
          dirty = false;
          open = false;
        }
      } else if (decision == QUICK_TUI_UNSAVED_DISCARD) {
        (void)quick_tui_reload_profiles(state);
        dirty = false;
        open = false;
      }
    } else {
      open = false;
    }
  }
}

static void quick_tui_new_profile(quick_tui_app_state_t *state) {
  char name[128] = {0};
  if (tui_input_dialog("New profile", "Profile name:", name, sizeof(name)) !=
      APP_SUCCESS) {
    return;
  }
  char msg[160];
  if (!quick_tui_validate_profile_field("name", name, msg, sizeof(msg))) {
    tui_show_message("New profile", msg);
    return;
  }
  quick_profile_t *profile = quick_profile_config_upsert(&state->profiles, name);
  if (!profile) {
    tui_show_message("New profile", "Could not create profile (limit reached or out of memory).");
    return;
  }
  quick_tui_show_profile(state, profile);
}

static void quick_tui_profiles_section(quick_tui_app_state_t *state) {
  bool open = true;
  while (open && !tui_interrupted()) {
    (void)quick_tui_reload_profiles(state);
    const size_t count = state->profiles.profile_count + 3U;
    tui_menu_item_t *items = calloc(count, sizeof(tui_menu_item_t));
    char **labels = calloc(state->profiles.profile_count, sizeof(char *));
    if (!items || !labels) {
      free(items);
      free(labels);
      tui_show_message("Profiles", "Out of memory while rendering profiles.");
      return;
    }
    size_t idx = 0;
    for (size_t i = 0; i < state->profiles.profile_count; i++) {
      const quick_profile_t *p = &state->profiles.profiles[i];
      labels[i] = malloc(384);
      if (labels[i]) {
        snprintf(labels[i], 384, "%s%s - %s %s", p->name ? p->name : "profile",
                 state->profiles.default_profile && p->name &&
                         strcmp(state->profiles.default_profile, p->name) == 0
                     ? " (default)"
                     : "",
                 p->ssh ? p->ssh : "local/no-ssh",
                 p->base_domain ? p->base_domain : "");
      }
      items[idx++] = (tui_menu_item_t){.label = labels[i] ? labels[i] : p->name,
                                       .description = "Open profile details",
                                       .id = (int)i + 1};
    }
    items[idx++] = (tui_menu_item_t){.kind = TUI_MENU_ITEM_SEPARATOR};
    items[idx++] = (tui_menu_item_t){.label = "&New profile",
                                     .description = "Create a profile in memory",
                                     .id = 900};
    items[idx++] = (tui_menu_item_t){.label = "&Back",
                                     .description = "Return to Settings",
                                     .id = 901};
    tui_menu_result_t r = tui_show_menu(
        NULL, &(tui_menu_config_t){.title = "Profiles",
                                   .subtitle = quick_tui_profile_config_path(state),
                                   .items = items,
                                   .item_count = (int)idx,
                                   .default_index = state->profiles.profile_count > 0 ? 0 : (int)state->profiles.profile_count + 1,
                                   .frame_height = 18,
                                   .frame_width = 78,
                                   .enable_search = true,
                                   .show_numeric_keys = true});
    if (r.status != TUI_MENU_OK || r.selected_id == 901) {
      open = false;
    } else if (r.selected_id == 900) {
      quick_tui_new_profile(state);
    } else if (r.selected_id >= 1 &&
               (size_t)r.selected_id <= state->profiles.profile_count) {
      quick_tui_show_profile(state,
                             &state->profiles.profiles[(size_t)r.selected_id - 1U]);
    }
    for (size_t i = 0; i < state->profiles.profile_count; i++) {
      free(labels[i]);
    }
    free(labels);
    free(items);
  }
}

static bool quick_tui_load_site_config_path(char *path, size_t path_size,
                                            quick_site_config_t *site) {
  snprintf(path, path_size, "quick.json");
  app_error err = quick_site_config_load_file(path, site);
  if (err == APP_SUCCESS) {
    return true;
  }
  char input[512] = {0};
  if (tui_input_dialog("Site config", "Path to quick.json:", input,
                       sizeof(input)) != APP_SUCCESS) {
    return false;
  }
  if (input[0] == '\0') {
    return false;
  }
  snprintf(path, path_size, "%s", input);
  err = quick_site_config_load_file(path, site);
  if (err != APP_SUCCESS) {
    tui_show_message("Site config", app_strerror(err));
    return false;
  }
  return true;
}

static const char *quick_tui_site_field_value(quick_site_config_t *site,
                                              const char *field) {
  if (strcmp(field, "name") == 0) return site->name ? site->name : "";
  if (strcmp(field, "subdomain") == 0) return site->subdomain ? site->subdomain : "";
  if (strcmp(field, "source") == 0) return site->source ? site->source : ".";
  if (strcmp(field, "output") == 0) return site->output ? site->output : ".";
  if (strcmp(field, "build") == 0) return site->build ? site->build : "";
  if (strcmp(field, "profile") == 0) return site->profile ? site->profile : "";
  return "";
}

static void quick_tui_site_apply_field(quick_site_config_t *site,
                                       const char *field,
                                       const char *value) {
  if (strcmp(field, "name") == 0) (void)quick_tui_set_string(&site->name, value);
  else if (strcmp(field, "subdomain") == 0) (void)quick_tui_set_string(&site->subdomain, value);
  else if (strcmp(field, "source") == 0) (void)quick_tui_set_string(&site->source, value[0] ? value : ".");
  else if (strcmp(field, "output") == 0) (void)quick_tui_set_string(&site->output, value[0] ? value : ".");
  else if (strcmp(field, "build") == 0) (void)quick_tui_set_string(&site->build, value);
  else if (strcmp(field, "profile") == 0) (void)quick_tui_set_string(&site->profile, value);
}

static void quick_tui_site_config_section(void) {
  quick_site_config_t site;
  quick_site_config_init(&site);
  char path[512];
  if (!quick_tui_load_site_config_path(path, sizeof(path), &site)) {
    quick_site_config_destroy(&site);
    return;
  }
  static const char *const fields[] = {"name", "subdomain", "source",
                                       "output", "build", "profile"};
  bool open = true;
  while (open && !tui_interrupted()) {
    tui_menu_item_t items[sizeof(fields) / sizeof(fields[0]) + 2U];
    char labels[sizeof(fields) / sizeof(fields[0])][256];
    size_t idx = 0;
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
      snprintf(labels[i], sizeof(labels[i]), "%s - %s", fields[i],
               quick_tui_site_field_value(&site, fields[i]));
      items[idx++] = (tui_menu_item_t){.label = labels[i], .id = (int)i + 1};
    }
    items[idx++] = (tui_menu_item_t){.kind = TUI_MENU_ITEM_SEPARATOR};
    items[idx++] = (tui_menu_item_t){.label = "&Write quick.json",
                                     .description = "Validate and write site config",
                                     .id = 900};
    tui_menu_result_t r = tui_show_menu(
        NULL, &(tui_menu_config_t){.title = "Site config",
                                   .subtitle = path,
                                   .items = items,
                                   .item_count = (int)idx,
                                   .default_index = 0,
                                   .frame_height = 18,
                                   .frame_width = 78,
                                   .enable_search = true,
                                   .show_numeric_keys = true});
    if (r.status != TUI_MENU_OK) {
      open = false;
    } else if (r.selected_id == 900) {
      if ((!site.name || !quick_slug_is_valid(site.name)) ||
          (site.subdomain && !quick_slug_is_valid(site.subdomain))) {
        tui_show_message("Site config", "Name and subdomain must be valid DNS labels before writing.");
      } else if (tui_confirm("Write site config", "Write quick.json?")) {
        app_error err = quick_site_config_write_file(path, &site);
        tui_show_message("Write site config",
                         err == APP_SUCCESS ? "quick.json written."
                                            : app_strerror(err));
      }
    } else if (r.selected_id >= 1 &&
               (size_t)r.selected_id <= sizeof(fields) / sizeof(fields[0])) {
      const char *field = fields[(size_t)r.selected_id - 1U];
      char prompt[256];
      snprintf(prompt, sizeof(prompt), "%s [%s]:", field,
               quick_tui_site_field_value(&site, field));
      char value[512] = {0};
      if (tui_input_dialog("Edit site config", prompt, value, sizeof(value)) ==
          APP_SUCCESS) {
        if ((strcmp(field, "name") == 0 || strcmp(field, "subdomain") == 0) &&
            value[0]) {
          char slug[QUICK_SLUG_MAX + 1];
          if (quick_slug_normalize(value, slug) != APP_SUCCESS ||
              !quick_slug_is_valid(slug)) {
            tui_show_message("Edit site config", "Value is not a valid DNS label.");
            continue;
          }
          quick_tui_site_apply_field(&site, field, slug);
        } else if (strcmp(field, "profile") == 0 && value[0] &&
                   !quick_profile_name_is_safe(value)) {
          tui_show_message("Edit site config", "Profile name is not safe.");
        } else {
          quick_tui_site_apply_field(&site, field, value);
        }
      }
    }
  }
  quick_site_config_destroy(&site);
}

void quick_tui_screen_config(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
  (void)quick_tui_reload_profiles(state);
  bool open = true;
  while (open && !tui_interrupted()) {
    const tui_menu_item_t items[] = {
        {.label = "&Profiles",
         .description = "List, edit, and write user profile config",
         .id = 1},
        {.label = "&Site config",
         .description = "Edit quick.json for current or chosen site",
         .id = 2},
        {.kind = TUI_MENU_ITEM_SEPARATOR},
        {.label = "&Back", .description = "Return to OpenQuick", .id = 3},
    };
    tui_menu_result_t r = tui_show_menu(
        NULL, &(tui_menu_config_t){.title = "Settings",
                                   .subtitle = quick_tui_profile_config_path(state),
                                   .items = items,
                                   .item_count = (int)(sizeof(items) / sizeof(items[0])),
                                   .default_index = 0,
                                   .frame_height = 14,
                                   .frame_width = 74,
                                   .show_numeric_keys = true});
    if (r.status != TUI_MENU_OK || r.selected_id == 3) {
      open = false;
    } else if (r.selected_id == 1) {
      quick_tui_profiles_section(state);
    } else if (r.selected_id == 2) {
      quick_tui_site_config_section();
    }
  }
}
