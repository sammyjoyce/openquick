/*
 * tui_app.c - OpenQuick product TUI entry point.
 */
#include <stdio.h>
#include <string.h>

#include "../core/app_info.h"
#include "../core/ops.h"
#include "tui.h"
#include "tui_app_state.h"
#include "tui_internal.h"
#include "tui_onboarding.h"
#include "tui_screen_host.h"

/* Tall enough to show the product menu at the documented 80x24 baseline. */
enum {
  MAIN_MENU_FRAME_HEIGHT = 21,
  MAIN_MENU_FRAME_WIDTH = 72,
};

typedef enum {
  APP_MENU_GET_STARTED = 1,
  APP_MENU_SITES,
  APP_MENU_DEPLOY,
  APP_MENU_NEW_SITE,
  APP_MENU_DOCTOR,
  APP_MENU_SERVE,
  APP_MENU_CONNECT_HOST,
  APP_MENU_NEW_HOST,
  APP_MENU_SETTINGS,
  APP_MENU_HELP,
  APP_MENU_EXIT,
  APP_MENU_PREVIEW,
  APP_MENU_OPEN_SITE,
  APP_MENU_FIX_PROJECT,
} app_main_menu_id_t;

static const tui_menu_item_t no_project_menu[] = {
    {.label = "&Get started",
     .description = "Guided setup: local preview, connect or set up a host",
     .id = APP_MENU_GET_STARTED},
    {.label = "&Sites",
     .description = "Remote not checked; CLI: quick list --remote",
     .id = APP_MENU_SITES},
    {.label = "&Deploy",
     .description = "CLI: quick deploy [path]",
     .id = APP_MENU_DEPLOY},
    {.label = "&New site",
     .description = "CLI: quick init [dir]",
     .id = APP_MENU_NEW_SITE},
    {.label = "Do&ctor",
     .description = "CLI: quick doctor",
     .id = APP_MENU_DOCTOR},
    {.label = "Ser&ve",
     .description = "CLI: quick serve --dev / quick serve install",
     .id = APP_MENU_SERVE},
    {.label = "Connec&t host",
     .description = "CLI: quick serve install (save a host connection)",
     .id = APP_MENU_CONNECT_HOST},
    {.label = "Set &up host",
     .description = "CLI: quick serve install --execute",
     .id = APP_MENU_NEW_HOST},
    {.label = "Sett&ings",
     .description = "CLI: quick config show / edit config.json",
     .id = APP_MENU_SETTINGS},
    {.kind = TUI_MENU_ITEM_SEPARATOR},
    {.label = "&Help/About",
     .description = "Key bindings, version, and product notes",
     .id = APP_MENU_HELP},
    {.label = "E&xit",
     .description = "Return to the shell",
     .id = APP_MENU_EXIT},
};

static const tui_menu_item_t valid_project_menu[] = {
    {.label = "&Preview locally", .id = APP_MENU_PREVIEW},
    {.label = "&Deploy",
     .description =
         "Remote checked only after explicit Deploy; CLI: quick deploy [path]",
     .id = APP_MENU_DEPLOY},
    {.label = "&Open deployed site",
     .description =
         "Resolve and open the deployed URL; remote checked by this action",
     .id = APP_MENU_OPEN_SITE},
    {.label = "&Sites",
     .description = "Remote not checked; CLI: quick list --remote",
     .id = APP_MENU_SITES},
    {.label = "&New site",
     .description = "CLI: quick init [dir]",
     .id = APP_MENU_NEW_SITE},
    {.label = "Do&ctor",
     .description = "CLI: quick doctor",
     .id = APP_MENU_DOCTOR},
    {.label = "Ser&ve",
     .description = "CLI: quick serve --dev / quick serve install",
     .id = APP_MENU_SERVE},
    {.label = "Sett&ings",
     .description = "CLI: quick config show / edit config.json",
     .id = APP_MENU_SETTINGS},
    {.label = "&Get started",
     .description = "Guided setup: local preview, connect or set up a host",
     .id = APP_MENU_GET_STARTED},
    {.kind = TUI_MENU_ITEM_SEPARATOR},
    {.label = "&Help/About",
     .description = "Key bindings, version, and product notes",
     .id = APP_MENU_HELP},
    {.label = "E&xit",
     .description = "Return to the shell",
     .id = APP_MENU_EXIT},
};

static const tui_menu_item_t malformed_project_menu[] = {
    {.label = "&Fix project setup",
     .description = "Local quick.json is malformed; remote not checked; edit "
                    "project setup",
     .id = APP_MENU_FIX_PROJECT},
    {.label = "&Get started",
     .description = "Guided setup: local preview, connect or set up a host",
     .id = APP_MENU_GET_STARTED},
    {.label = "&Sites",
     .description = "Remote not checked; CLI: quick list --remote",
     .id = APP_MENU_SITES},
    {.label = "&Deploy",
     .description = "CLI: quick deploy [path]",
     .id = APP_MENU_DEPLOY},
    {.label = "&New site",
     .description = "CLI: quick init [dir]",
     .id = APP_MENU_NEW_SITE},
    {.label = "Do&ctor",
     .description = "CLI: quick doctor",
     .id = APP_MENU_DOCTOR},
    {.label = "Ser&ve",
     .description = "CLI: quick serve --dev / quick serve install",
     .id = APP_MENU_SERVE},
    {.label = "Sett&ings",
     .description = "CLI: quick config show / edit config.json",
     .id = APP_MENU_SETTINGS},
    {.kind = TUI_MENU_ITEM_SEPARATOR},
    {.label = "&Help/About",
     .description = "Key bindings, version, and product notes",
     .id = APP_MENU_HELP},
    {.label = "E&xit",
     .description = "Return to the shell",
     .id = APP_MENU_EXIT},
};

typedef struct {
  const tui_menu_item_t *items;
  int item_count;
  int default_index;
  tui_menu_item_t
      valid_items[sizeof(valid_project_menu) / sizeof(valid_project_menu[0])];
  char preview_description[256];
} app_dashboard_menu_t;

static void app_build_dashboard_menu(const quick_context_result_t *ctx,
                                     bool context_ok,
                                     app_dashboard_menu_t *menu) {
  *menu = (app_dashboard_menu_t){0};
  if (!context_ok || !ctx || ctx->project_state == QUICK_PROJECT_NONE ||
      ctx->project_state == QUICK_PROJECT_ADOPTABLE) {
    menu->items = no_project_menu;
    menu->item_count =
        (int)(sizeof(no_project_menu) / sizeof(no_project_menu[0]));
    menu->default_index = 1;
    return;
  }
  if (ctx->project_state == QUICK_PROJECT_MALFORMED) {
    menu->items = malformed_project_menu;
    menu->item_count = (int)(sizeof(malformed_project_menu) /
                             sizeof(malformed_project_menu[0]));
    menu->default_index = 0;
    return;
  }

  for (size_t i = 0;
       i < sizeof(valid_project_menu) / sizeof(valid_project_menu[0]); i++) {
    menu->valid_items[i] = valid_project_menu[i];
  }
  if (ctx->site_name && ctx->site_name[0]) {
    snprintf(menu->preview_description, sizeof(menu->preview_description),
             "Local preview for %s; remote not checked; CLI: quick serve --dev",
             ctx->site_name);
  } else {
    snprintf(menu->preview_description, sizeof(menu->preview_description),
             "Local project preview; remote not checked; CLI: quick serve "
             "--dev");
  }
  menu->valid_items[0].description = menu->preview_description;
  menu->items = menu->valid_items;
  menu->item_count =
      (int)(sizeof(valid_project_menu) / sizeof(valid_project_menu[0]));
  menu->default_index = 0;
}

typedef enum {
  APP_OVERLAY_KEYS = 1,
  APP_OVERLAY_ABOUT,
  APP_OVERLAY_EXIT,
} app_overlay_menu_id_t;

static void app_show_keybindings(void) {
  tui_show_message(
      "Key Bindings",
      "Up / Down or j / k   Move selection\n"
      "PgUp / PgDn          Jump a page\n"
      "Home / End           First / last item\n"
      "1-9                  Jump to a numbered item\n"
      "/                    Incremental search\n"
      "Enter                Select\n"
      "Esc                  Open this menu / go back\n"
      "q                    Quit or go back\n\n"
      "CLI equivalents: quick list --remote, quick deploy, quick init,\n"
      "quick doctor, quick serve --dev, quick serve install, quick config "
      "show");
}

static void app_show_about(void) {
  const app_build_info_t *build = app_build_info();
  char about[640];
  snprintf(
      about, sizeof(about),
      "%s %s\n\n"
      "OpenQuick deploys static sites to any SSH+rsync host running quickd.\n\n"
      "Core flows: New site, Deploy, Sites, Doctor, Serve, and Settings.\n"
      "The TUI calls the shared OpenQuick ops layer; it does not construct ssh "
      "or rsync commands itself.",
      build->name, build->version);
  tui_show_message("About OpenQuick", about);
}

static void app_show_help_about(void) {
  const tui_menu_item_t items[] = {
      {.label = "&Key bindings",
       .description = "Show keyboard shortcuts",
       .id = APP_OVERLAY_KEYS},
      {.label = "&About OpenQuick",
       .description = "Version and product summary",
       .id = APP_OVERLAY_ABOUT},
      {.kind = TUI_MENU_ITEM_SEPARATOR},
      {.label = "&Back", .description = "Return to OpenQuick", .id = 100},
  };
  bool open = true;
  while (open) {
    tui_menu_result_t r = tui_show_menu(
        NULL, &(tui_menu_config_t){
                  .title = "Help/About",
                  .subtitle = APP_NAME,
                  .items = items,
                  .item_count = (int)(sizeof(items) / sizeof(items[0])),
                  .default_index = 0,
                  .frame_height = 14,
                  .frame_width = MAIN_MENU_FRAME_WIDTH,
                  .show_numeric_keys = true});
    if (r.status != TUI_MENU_OK || r.selected_id == 100) {
      open = false;
    } else if (r.selected_id == APP_OVERLAY_KEYS) {
      app_show_keybindings();
    } else if (r.selected_id == APP_OVERLAY_ABOUT) {
      app_show_about();
    }
  }
}

/* gitlogue-style Esc overlay: compact help/about/exit menu. */
static bool app_show_menu_overlay(void) {
  const tui_menu_item_t overlay_items[] = {
      {.label = "&Key Bindings",
       .description = "Show all keyboard shortcuts",
       .id = APP_OVERLAY_KEYS},
      {.label = "&About",
       .description = "Version and OpenQuick details",
       .id = APP_OVERLAY_ABOUT},
      {.kind = TUI_MENU_ITEM_SEPARATOR},
      {.label = "E&xit",
       .description = "Leave OpenQuick",
       .id = APP_OVERLAY_EXIT},
  };
  bool want_exit = false;
  bool open = true;
  while (open) {
    tui_menu_result_t r = tui_show_menu(
        NULL, &(tui_menu_config_t){
                  .title = "Menu",
                  .subtitle = APP_NAME,
                  .items = overlay_items,
                  .item_count =
                      (int)(sizeof(overlay_items) / sizeof(overlay_items[0])),
                  .default_index = 0,
                  .frame_height = MAIN_MENU_FRAME_HEIGHT,
                  .frame_width = MAIN_MENU_FRAME_WIDTH,
                  .show_numeric_keys = true});
    if (r.status != TUI_MENU_OK) {
      open = false;
      break;
    }
    switch (r.selected_id) {
    case APP_OVERLAY_KEYS:
      app_show_keybindings();
      break;
    case APP_OVERLAY_ABOUT:
      app_show_about();
      break;
    case APP_OVERLAY_EXIT:
      want_exit = true;
      open = false;
      break;
    default:
      break;
    }
  }
  return want_exit;
}

static void app_preview_local(quick_tui_app_state_t *state,
                              const quick_context_result_t *ctx) {
  if (!ctx || !ctx->dir || !ctx->dir[0]) {
    tui_show_message(
        "Local preview",
        "No local project directory is available. Open Get started or run "
        "'quick serve --dev' from the project directory.");
    return;
  }

  app_error err =
      quick_tui_start_serve_child(state, ctx->dir, "9366", NULL);
  if (err != APP_SUCCESS) {
    char message[384];
    snprintf(message, sizeof(message),
             "Could not start the local preview: %s\n\nTry: quick serve "
             "--dev",
             app_strerror(err));
    tui_show_message("Local preview", message);
    return;
  }

  char message[384];
  snprintf(message, sizeof(message),
           "Local preview: %s\nRemote host not checked.\n\nCLI: quick "
           "serve --dev",
           state->serve_url);
  tui_show_message("Local preview", message);

  char status[256];
  snprintf(status, sizeof(status),
           "Local preview running at %s; remote not checked", state->serve_url);
  quick_tui_set_status(state, status);

  char question[256];
  snprintf(question, sizeof(question),
           "Open %s in your browser? The local preview will keep running.",
           state->serve_url);
  if (tui_confirm("Open local preview", question)) {
    app_error open_err = quick_op_open_url(state->serve_url);
    if (open_err != APP_SUCCESS) {
      snprintf(message, sizeof(message),
               "Could not open the local preview: %s\n\nThe preview is still "
               "running at %s",
               app_strerror(open_err), state->serve_url);
      tui_show_message("Open local preview", message);
    }
  }
}

static void app_open_deployed_site(quick_tui_app_state_t *state,
                                   const quick_context_result_t *ctx) {
  quick_url_result_t result;
  quick_url_result_init(&result);
  if (!ctx || !ctx->site_name || !ctx->site_name[0]) {
    tui_show_message(
        "Open deployed site",
        "The local project has no site name. Fix quick.json in Settings, then "
        "try again. CLI: quick open");
    quick_url_result_destroy(&result);
    return;
  }

  const char *profile =
      ctx->project_profile && ctx->project_profile[0]
          ? ctx->project_profile
          : (ctx->default_profile && ctx->default_profile[0]
                 ? ctx->default_profile
                 : NULL);
  if (!profile) {
    tui_show_message(
        "Open deployed site",
        "No deployment profile is selected. Set quick.json.profile or a "
        "default profile in Settings, then try again.\n\nCLI: quick open");
    quick_url_result_destroy(&result);
    return;
  }
  if (ctx->project_profile_missing ||
      !quick_profile_config_find(&state->profiles, profile)) {
    char message[384];
    snprintf(message, sizeof(message),
             "Profile '%s' is not configured. Add it in Settings or choose "
             "another profile, then try again.\n\nCLI: quick open",
             profile);
    tui_show_message("Open deployed site", message);
    quick_url_result_destroy(&result);
    return;
  }

  quick_plan_overrides_t overrides = {
      .site = ctx->site_name,
      .profile = profile,
  };
  app_error err =
      quick_op_resolve_url(&state->profiles, &overrides, &result);
  if (err != APP_SUCCESS) {
    char message[512];
    snprintf(message, sizeof(message),
             "Could not resolve the deployed URL: %s\n\nCheck the site and "
             "profile in Settings, or run: quick open",
             app_strerror(err));
    tui_show_message("Open deployed site", message);
    quick_url_result_destroy(&result);
    return;
  }
  if (!result.url || !result.url[0]) {
    tui_show_message(
        "Open deployed site",
        "No deployed URL was found. Deploy the site first, then try again.\n\n"
        "CLI: quick deploy && quick open");
    quick_url_result_destroy(&result);
    return;
  }

  app_error open_err = quick_op_open_url(result.url);
  if (open_err != APP_SUCCESS) {
    char message[384];
    snprintf(message, sizeof(message),
             "Resolved %s, but could not open it: %s\n\nCopy the URL into "
             "your browser or run: quick open",
             result.url, app_strerror(open_err));
    tui_show_message("Open deployed site", message);
  } else {
    char status[256];
    snprintf(status, sizeof(status), "Opened deployed site: %s", result.url);
    quick_tui_set_status(state, status);
  }
  quick_url_result_destroy(&result);
}

static void app_dispatch(quick_tui_app_state_t *state, int id,
                         const quick_context_result_t *ctx) {
  switch (id) {
  case APP_MENU_GET_STARTED: {
    quick_onboard_choice_t choice = quick_tui_welcome(state, ctx);
    (void)quick_tui_onboarding_dispatch_choice(state, choice,
                                               QUICK_ONBOARD_RETURN_DASHBOARD);
    break;
  }
  case APP_MENU_PREVIEW:
    app_preview_local(state, ctx);
    break;
  case APP_MENU_OPEN_SITE:
    app_open_deployed_site(state, ctx);
    break;
  case APP_MENU_FIX_PROJECT:
    quick_tui_screen_site_config(state);
    break;
  case APP_MENU_SITES:
    quick_tui_screen_sites(state);
    break;
  case APP_MENU_DEPLOY:
    quick_tui_screen_deploy(state);
    break;
  case APP_MENU_NEW_SITE:
    quick_tui_screen_init(state);
    break;
  case APP_MENU_DOCTOR:
    quick_tui_screen_doctor(state);
    break;
  case APP_MENU_SERVE:
    quick_tui_screen_serve(state);
    break;
  case APP_MENU_CONNECT_HOST:
    quick_onboarding_model_reset(&state->onboarding,
                                 QUICK_ONBOARD_RETURN_DASHBOARD);
    (void)quick_onboarding_transition(&state->onboarding,
                                      QUICK_ONBOARD_EVENT_CHOOSE_CONNECT);
    (void)quick_tui_screen_connect_host(state);
    break;
  case APP_MENU_NEW_HOST:
    quick_onboarding_model_reset(&state->onboarding,
                                 QUICK_ONBOARD_RETURN_DASHBOARD);
    (void)quick_onboarding_transition(&state->onboarding,
                                      QUICK_ONBOARD_EVENT_CHOOSE_INSTALL);
    (void)quick_tui_screen_new_host(state);
    break;
  case APP_MENU_SETTINGS:
    quick_tui_screen_config(state);
    break;
  case APP_MENU_HELP:
    app_show_help_about();
    break;
  default:
    tui_beep();
    break;
  }
}

static app_error app_error_from_tui_interrupt(void) {
  return tui_take_interrupt_error();
}

static void app_build_subtitle(const quick_tui_app_state_t *state, char *out,
                               size_t out_size) {
  if (state && state->status[0]) {
    snprintf(out, out_size, "%s · profile %s", state->status,
             quick_tui_default_profile_name(state));
  } else {
    snprintf(out, out_size, "profile %s · %s",
             quick_tui_default_profile_name(state),
             quick_tui_profile_config_path(state));
  }
}

app_error tui_run_app(bool run_onboarding) {
  app_error err = tui_init();
  if (err != APP_SUCCESS) {
    return err;
  }

  quick_tui_app_state_t state;
  quick_tui_app_state_init(&state);
  (void)quick_tui_reload_profiles(&state);
  if (run_onboarding) {
    bool go_dashboard = quick_tui_onboarding_launch(&state);
    if (!go_dashboard) {
      quick_tui_app_state_destroy(&state);
      tui_cleanup();
      return err;
    }
  }

  tui_window_t *menu_frame =
      tui_create_centered_window(MAIN_MENU_FRAME_HEIGHT, MAIN_MENU_FRAME_WIDTH);
  if (!menu_frame) {
    quick_tui_app_state_destroy(&state);
    tui_cleanup();
    return APP_ERROR_OUT_OF_RANGE;
  }
  tui_push_background(menu_frame);

  bool running = true;
  while (running) {
    quick_tui_poll_serve_child(&state);
    (void)quick_tui_reload_profiles(&state);

    quick_context_result_t ctx;
    quick_context_result_init(&ctx);
    quick_context_request_t context_request = {
        .profiles = &state.profiles,
        .dir = NULL,
    };
    app_error context_err =
        quick_op_classify_context(&context_request, &ctx);
    const bool context_ok = context_err == APP_SUCCESS;
    if (!context_ok) {
      char status[256];
      snprintf(status, sizeof(status),
               "Project check failed (%s); use Get started or Settings",
               app_strerror(context_err));
      quick_tui_set_status(&state, status);
    } else if (strncmp(state.status, "Project check failed (", 22) == 0) {
      quick_tui_set_status(&state, NULL);
    }

    app_dashboard_menu_t dashboard_menu;
    app_build_dashboard_menu(&ctx, context_ok, &dashboard_menu);
    char subtitle[512];
    app_build_subtitle(&state, subtitle, sizeof(subtitle));
    tui_menu_result_t r = tui_show_menu(
        menu_frame,
        &(tui_menu_config_t){
            .title = "OpenQuick",
            .subtitle = subtitle,
            .items = dashboard_menu.items,
            .item_count = dashboard_menu.item_count,
            .default_index = dashboard_menu.default_index,
            .frame_height = MAIN_MENU_FRAME_HEIGHT,
            .frame_width = MAIN_MENU_FRAME_WIDTH,
            .enable_search = true,
            .enable_mouse = true,
            .enable_menu_key = true,
            .show_numeric_keys = true});
    switch (r.status) {
    case TUI_MENU_OK:
      if (r.selected_id == APP_MENU_EXIT) {
        running = !tui_confirm("Exit", "Return to the shell?");
      } else {
        app_dispatch(&state, r.selected_id, &ctx);
      }
      break;
    case TUI_MENU_MENU:
      if (app_show_menu_overlay()) {
        running = !tui_confirm("Exit", "Return to the shell?");
      }
      break;
    case TUI_MENU_CANCELLED:
      running = !tui_confirm("Exit", "Return to the shell?");
      break;
    case TUI_MENU_INTERRUPTED:
      running = false;
      err = app_error_from_tui_interrupt();
      break;
    case TUI_MENU_TOO_SMALL:
    case TUI_MENU_INVALID_ARG:
      running = false;
      err = APP_ERROR_OUT_OF_RANGE;
      break;
    case TUI_MENU_NO_MEMORY:
      running = false;
      err = APP_ERROR_MEMORY;
      break;
    }
    quick_context_result_destroy(&ctx);
  }

  tui_pop_background();
  tui_destroy_window(menu_frame);
  quick_tui_app_state_destroy(&state);
  tui_cleanup();
  return err;
}
