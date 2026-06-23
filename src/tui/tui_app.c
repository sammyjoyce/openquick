/*
 * tui_app.c - OpenQuick product TUI entry point.
 */
#include <stdio.h>

#include "../core/app_info.h"
#include "tui.h"
#include "tui_app_state.h"
#include "tui_internal.h"

/* Tall enough to show the product menu at the documented 80x24 baseline. */
enum {
  MAIN_MENU_FRAME_HEIGHT = 20,
  MAIN_MENU_FRAME_WIDTH = 72,
};

typedef enum {
  APP_MENU_SITES = 1,
  APP_MENU_DEPLOY,
  APP_MENU_NEW_SITE,
  APP_MENU_DOCTOR,
  APP_MENU_SERVE,
  APP_MENU_SETTINGS,
  APP_MENU_HELP,
  APP_MENU_EXIT,
} app_main_menu_id_t;

static const tui_menu_item_t main_menu[] = {
    {.label = "&Sites",
     .description = "CLI: quick list --remote / quick delete / quick public",
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

typedef enum {
  APP_OVERLAY_KEYS = 1,
  APP_OVERLAY_ABOUT,
  APP_OVERLAY_EXIT,
} app_overlay_menu_id_t;

static void app_show_keybindings(void) {
  tui_show_message("Key Bindings",
                   "Up / Down or j / k   Move selection\n"
                   "PgUp / PgDn          Jump a page\n"
                   "Home / End           First / last item\n"
                   "1-9                  Jump to a numbered item\n"
                   "/                    Incremental search\n"
                   "Enter                Select\n"
                   "Esc                  Open this menu / go back\n"
                   "q                    Quit or go back\n\n"
                   "CLI equivalents: quick list --remote, quick deploy, quick init,\n"
                   "quick doctor, quick serve --dev, quick serve install, quick config show");
}

static void app_show_about(void) {
  const app_build_info_t *build = app_build_info();
  char about[640];
  snprintf(about, sizeof(about),
           "%s %s\n\n"
           "OpenQuick deploys static sites to any SSH+rsync host running quickd.\n\n"
           "Core flows: New site, Deploy, Sites, Doctor, Serve, and Settings.\n"
           "The TUI calls the shared OpenQuick ops layer; it does not construct ssh or rsync commands itself.",
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
        NULL, &(tui_menu_config_t){.title = "Help/About",
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
        NULL, &(tui_menu_config_t){.title = "Menu",
                                   .subtitle = APP_NAME,
                                   .items = overlay_items,
                                   .item_count = (int)(sizeof(overlay_items) /
                                                       sizeof(overlay_items[0])),
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

static void app_dispatch(quick_tui_app_state_t *state, int id) {
  switch (id) {
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
  snprintf(out, out_size, "profile %s · %s", quick_tui_default_profile_name(state),
           quick_tui_profile_config_path(state));
}

app_error tui_run_app(void) {
  app_error err = tui_init();
  if (err != APP_SUCCESS) {
    return err;
  }

  quick_tui_app_state_t state;
  quick_tui_app_state_init(&state);
  (void)quick_tui_reload_profiles(&state);

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
    char subtitle[512];
    app_build_subtitle(&state, subtitle, sizeof(subtitle));
    tui_menu_result_t r = tui_show_menu(
        menu_frame,
        &(tui_menu_config_t){.title = "OpenQuick",
                             .subtitle = subtitle,
                             .items = main_menu,
                             .item_count = (int)(sizeof(main_menu) / sizeof(main_menu[0])),
                             .default_index = 0,
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
        app_dispatch(&state, r.selected_id);
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
  }

  tui_pop_background();
  tui_destroy_window(menu_frame);
  quick_tui_app_state_destroy(&state);
  tui_cleanup();
  return err;
}
