/*
 * tui_app.c - main menu entry point for this template's TUI.
 *
 * This file is the starter shell. The functions below are organized so you can:
 *
 *   1. Edit `main_menu[]` to replace the seed items with your commands.
 *   2. Replace or delete the `app_show_*` handlers that you don't need.
 *   3. Keep `tui_run_app()` as-is - the resize loop, exit confirmation, and
 *      cancel/interrupt handling are part of the reference implementation
 *      and Just Work for any item list you supply.
 *
 * The handlers below also serve as worked examples of every TUI primitive
 * (message dialog, confirm, input dialog, progress bar, custom layout,
 * sub-menu). Treat them as documentation you can delete or copy from.
 */
#include <stdio.h>

#include "../core/app_info.h"
#include "../ui/action_item.h"
#include "tui.h"
#include "tui_internal.h"
#include "tui_menu_adapter.h"

enum {
  /* Tall enough to show the full main menu - now ten entries plus two
   * separators - without scrolling at the documented 80x24 baseline. */
  MAIN_MENU_FRAME_HEIGHT = 20,
  MAIN_MENU_FRAME_WIDTH = 72,
};

/* ============================================================
 * Section 1: Handlers - replace these with your app's actions.
 * ============================================================ */

static void app_show_overview(void) {
  tui_show_message(
      "Starter Overview",
      "This template gives you a production-shaped starting point:\n\n"
      "  C23 modules with explicit error codes\n"
      "  Zig build graph with optional ncurses support\n"
      "  CLI output modes for humans and automation\n"
      "  Reusable TUI windows, menus, dialogs, and progress bars");
}

static void app_show_system_info(void) {
  const app_build_info_t *build = app_build_info();
  char info[512];
  snprintf(info, sizeof(info),
           "Application: %s\n"
           "Version: %s\n"
           "Git Commit: %s\n"
           "Build Date: %s\n"
           "Terminal Size: %dx%d\n"
           "Colors Supported: %s",
           build->name, build->version, build->git_commit, build->build_date,
           tui_get_max_x(), tui_get_max_y(),
           tui_colors_enabled() ? "yes" : "no");
  tui_show_message("System Information", info);
}

static void app_show_input(void) {
  char buf[128] = {0};
  if (tui_input_dialog("Input Dialog", "Enter a display name:", buf,
                       sizeof(buf)) != APP_SUCCESS) {
    return;
  }
  char msg[256];
  snprintf(msg, sizeof(msg),
           "Hello, %s.\n\n"
           "Use this pattern for short prompts. For secrets, keep echo "
           "and cursor state scoped inside a dedicated helper.",
           buf[0] != '\0' ? buf : "World");
  tui_show_message("Input Captured", msg);
}

static void app_show_progress(void) {
  tui_progress_t *progress = tui_progress_create("Progress Pattern", 100);
  if (!progress) {
    tui_show_message("Progress",
                     "The terminal is too small for the progress example.");
    return;
  }
  for (int i = 0; i <= 100; i += 5) {
    char status[80];
    snprintf(status, sizeof(status), "Processing step %d of 20", i / 5);
    tui_progress_update(progress, i, status);
    napms(35);
  }
  tui_progress_destroy(progress);
  tui_show_message("Progress Complete",
                   "The progress helper clamps values, renders a "
                   "percentage, and owns its window lifecycle.");
}

typedef enum {
  APP_CONFIG_OUTPUT_MODE = 1,
  APP_CONFIG_LOG_LEVEL,
  APP_CONFIG_TERMINAL_SETTINGS,
  APP_CONFIG_RESET_DEFAULTS,
  APP_CONFIG_EXPORT,
  APP_CONFIG_BACK,
} app_config_menu_id_t;

typedef enum {
  APP_MENU_OVERVIEW = 1,
  APP_MENU_SYSTEM_INFO,
  APP_MENU_INPUT,
  APP_MENU_PROGRESS,
  APP_MENU_LAYOUT,
  APP_MENU_CONFIGURATION,
  APP_MENU_COMMANDS,
  APP_MENU_EXIT,
} app_main_menu_id_t;

static void app_draw_layout_window(tui_window_t *win, void *userdata) {
  (void)userdata;
  tui_draw_border(win);
  tui_set_window_title(win, "Layout Pattern");
  tui_set_color(win->win, TUI_COLOR_TITLE);
  tui_print_centered(win->win, 2, "Composable terminal UI");
  tui_unset_color(win->win, TUI_COLOR_TITLE);
  tui_print_wrapped(win->win, 4, 4, win->width - 8,
                    "Prefer small owned windows, bounded writes, and "
                    "status lines. Keep raw ncurses calls inside src/tui so "
                    "command code can stay easy to test.");
  tui_draw_status_line(win->win, "Enter/Esc closes this panel", APP_NAME);
  tui_refresh_window(win);
}

static tui_modal_decision_t app_handle_layout_key(tui_window_t *window, int ch,
                                                  void *userdata) {
  (void)window;
  (void)userdata;
  return ch == '\n' || ch == KEY_ENTER || ch == 27 || ch == 'q' || ch == 'Q'
             ? TUI_MODAL_DONE
             : TUI_MODAL_CONTINUE;
}

static void app_show_layout(void) {
  (void)tui_modal_run(14, 72, "Layout Pattern", app_draw_layout_window,
                      app_handle_layout_key, NULL);
}

static void app_show_config_menu(void) {
  static const tui_menu_item_t cfg_items[] = {
      {.label = "Output &mode",
       .description = "Plain, JSON, or colorized output",
       .id = APP_CONFIG_OUTPUT_MODE},
      {.label = "&Log level",
       .description = "Quiet, normal, debug verbosity",
       .id = APP_CONFIG_LOG_LEVEL},
      {.label = "&Terminal settings",
       .description = "Color, minimum dimensions, fallbacks",
       .id = APP_CONFIG_TERMINAL_SETTINGS},
      {.label = "&Reset to defaults",
       .description = "Restore all configuration",
       .id = APP_CONFIG_RESET_DEFAULTS},
      {.label = "&Export config",
       .description = "Write current settings to disk",
       .id = APP_CONFIG_EXPORT,
       .disabled = true},
      {.kind = TUI_MENU_ITEM_SEPARATOR},
      {.label = "&Back",
       .description = "Return to the main menu",
       .id = APP_CONFIG_BACK},
  };
  /* Configuration is a secondary menu *screen*, not a modal: same borderless,
   * centered frame as the root menu, so navigating in feels like the root. */
  tui_window_t *config_frame =
      tui_create_centered_window(MAIN_MENU_FRAME_HEIGHT, MAIN_MENU_FRAME_WIDTH);
  if (!config_frame) {
    tui_show_message("Configuration",
                     "The terminal is too small for the configuration menu.");
    return;
  }
  tui_push_background(config_frame);

  bool sub_running = true;
  while (sub_running) {
    tui_menu_result_t r = tui_show_menu(
        config_frame,
        &(tui_menu_config_t){
            .title = "Configuration",
            .subtitle = APP_NAME " · Settings",
            .items = cfg_items,
            .item_count = (int)(sizeof(cfg_items) / sizeof(cfg_items[0])),
            .default_index = 0,
            .frame_height = MAIN_MENU_FRAME_HEIGHT,
            .frame_width = MAIN_MENU_FRAME_WIDTH,
            .enable_search = true,
            .show_numeric_keys = true,
        });
    if (r.status != TUI_MENU_OK) {
      sub_running = false;
      continue;
    }
    switch (r.selected_id) {
    case APP_CONFIG_BACK:
      sub_running = false;
      break;
    case APP_CONFIG_OUTPUT_MODE:
      tui_show_message("Output Mode",
                       "Set via --json, --plain, or --quiet flags.");
      break;
    case APP_CONFIG_LOG_LEVEL:
      tui_show_message("Log Level", "Use --debug for verbose logging.");
      break;
    case APP_CONFIG_TERMINAL_SETTINGS:
      tui_show_message("Terminal Settings", "Minimum terminal: 48 x 12.");
      break;
    case APP_CONFIG_RESET_DEFAULTS:
      tui_show_message("Reset to Defaults", "All settings reverted.");
      break;
    default:
      tui_beep();
      break;
    }
  }
  tui_pop_background();
  tui_destroy_window(config_frame);
}

/* Maximum command rows surfaced in the Commands screen. The CLI command table
 * is small; this cap also bounds the local stack buffers below. */
enum { APP_COMMANDS_MAX = 16 };

/* "Back" sits above the command rows in id space. app_actions_from_commands
 * assigns ids 1..N to the command rows, so reserve a high id for Back. */
enum { APP_COMMANDS_BACK_ID = 1000 };

/* Show one command's metadata. This deliberately does NOT execute the CLI
 * command from inside the TUI; surfacing the descriptor (name, summary, and
 * whether it needs an interactive terminal) is the safe demonstration of the
 * shared action seam. */
// Append src to dst at *used without overflowing cap, advancing *used by the
// number of bytes actually written. Keeps the running detail string safe even
// when a command carries many long examples.
static void app_detail_append(char *dst, size_t cap, size_t *used,
                              const char *src) {
  if (!src || *used >= cap - 1) {
    return;
  }
  const int n = snprintf(dst + *used, cap - *used, "%s", src);
  if (n < 0) {
    return;
  }
  const size_t room = cap - 1 - *used;
  *used += (size_t)n > room ? room : (size_t)n;
}

static void app_show_command_detail(const app_action_item_t *action) {
  const bool interactive =
      (action->capabilities & APP_ACTION_CAP_INTERACTIVE_TERMINAL) != 0;
  char detail[1024];
  const int header =
      snprintf(detail, sizeof(detail),
               "Command: %s\n\n"
               "%s\n\n"
               "Requires interactive terminal: %s",
               action->command_name ? action->command_name : action->label,
               action->description ? action->description : "(no description)",
               interactive ? "yes" : "no");
  size_t used = header < 0 ? 0 : (size_t)header;
  if (used >= sizeof(detail)) {
    used = sizeof(detail) - 1;
  }

  // Mirror the CLI `--help` layout (options, then arguments, then examples) so
  // the Commands browser is a real command reference, not just a name and
  // summary. All three blocks read the borrowed metadata the shared action seam
  // carries from the same command table `--help` renders.
  if (action->options && action->option_count > 0) {
    app_detail_append(detail, sizeof(detail), &used, "\n\nOptions:");
    for (size_t i = 0; i < action->option_count; i++) {
      if (!action->options[i].name) {
        continue;
      }
      char line[192];
      snprintf(
          line, sizeof(line), "\n  %-16s%s", action->options[i].name,
          action->options[i].description ? action->options[i].description : "");
      app_detail_append(detail, sizeof(detail), &used, line);
    }
  }

  if (action->arguments && action->argument_count > 0) {
    app_detail_append(detail, sizeof(detail), &used, "\n\nArguments:");
    for (size_t i = 0; i < action->argument_count; i++) {
      const app_command_arg_t *arg = &action->arguments[i];
      if (!arg->name) {
        continue;
      }
      char line[192];
      snprintf(line, sizeof(line), "\n  %-16s%s%s", arg->name,
               arg->required ? "(required) " : "",
               arg->description ? arg->description : "");
      app_detail_append(detail, sizeof(detail), &used, line);
    }
  }

  if (action->examples && action->example_count > 0) {
    app_detail_append(detail, sizeof(detail), &used, "\n\nExamples:");
    for (size_t i = 0; i < action->example_count; i++) {
      if (!action->examples[i]) {
        continue;
      }
      char line[160];
      snprintf(line, sizeof(line), "\n  %s", action->examples[i]);
      app_detail_append(detail, sizeof(detail), &used, line);
    }
  }

  tui_show_message("Command Details", detail);
}

/* Build the Commands screen rows from the CLI command table via the shared
 * action descriptors and the curses-free tui_menu adapter, then run the same
 * borderless menu screen the rest of the showcase uses. */
static void app_show_commands(void) {
  app_action_item_t actions[APP_COMMANDS_MAX];
  const size_t total = app_actions_from_commands(actions, APP_COMMANDS_MAX);
  const size_t action_count =
      total < APP_COMMANDS_MAX ? total : APP_COMMANDS_MAX;

  /* One menu row per action, plus a separator and a Back row. */
  tui_menu_item_t items[APP_COMMANDS_MAX + 2];
  int item_count = 0;
  for (size_t i = 0; i < action_count; i++) {
    if (!tui_menu_item_from_action(&actions[i], &items[item_count])) {
      continue;
    }
    item_count++;
  }
  items[item_count++] = (tui_menu_item_t){.kind = TUI_MENU_ITEM_SEPARATOR};
  items[item_count++] = (tui_menu_item_t){
      .label = "&Back",
      .description = "Return to the main menu",
      .id = APP_COMMANDS_BACK_ID,
  };

  tui_window_t *commands_frame =
      tui_create_centered_window(MAIN_MENU_FRAME_HEIGHT, MAIN_MENU_FRAME_WIDTH);
  if (!commands_frame) {
    tui_show_message("Commands",
                     "The terminal is too small for the commands menu.");
    return;
  }
  tui_push_background(commands_frame);

  bool sub_running = true;
  while (sub_running) {
    tui_menu_result_t r = tui_show_menu(
        commands_frame, &(tui_menu_config_t){
                            .title = "Commands",
                            .subtitle = APP_NAME " · CLI commands",
                            .items = items,
                            .item_count = item_count,
                            .default_index = 0,
                            .frame_height = MAIN_MENU_FRAME_HEIGHT,
                            .frame_width = MAIN_MENU_FRAME_WIDTH,
                            .enable_search = true,
                            .show_numeric_keys = true,
                        });
    if (r.status != TUI_MENU_OK || r.selected_id == APP_COMMANDS_BACK_ID) {
      sub_running = false;
      continue;
    }
    /* Action ids are 1..action_count, matching actions[id - 1]. */
    if (r.selected_id >= 1 && (size_t)r.selected_id <= action_count) {
      app_show_command_detail(&actions[r.selected_id - 1]);
    } else {
      tui_beep();
    }
  }
  tui_pop_background();
  tui_destroy_window(commands_frame);
}

/* gitlogue-style Esc overlay: a compact menu offering help, about, and exit
 * without leaving the main showcase. */
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
                   "Esc                  Open this menu\n"
                   "q                    Quit");
}

static void app_show_about(void) {
  const app_build_info_t *build = app_build_info();
  char about[384];
  snprintf(about, sizeof(about),
           "%s %s\n\n"
           "A C23 + ncurses starter template: CLI argument\n"
           "parsing, a default TUI with menus, dialogs, and\n"
           "progress bars, plus an end-to-end test harness.\n\n"
           "Menu UI inspired by gitlogue.",
           build->name, build->version);
  tui_show_message("About", about);
}

/* Returns true when the user chose Exit from the overlay. */
static bool app_show_menu_overlay(void) {
  static const tui_menu_item_t overlay_items[] = {
      {.label = "&Key Bindings",
       .description = "Show all keyboard shortcuts",
       .id = APP_OVERLAY_KEYS},
      {.label = "&About",
       .description = "Version and template details",
       .id = APP_OVERLAY_ABOUT},
      {.kind = TUI_MENU_ITEM_SEPARATOR},
      {.label = "E&xit",
       .description = "Leave the showcase",
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
                  .show_numeric_keys = true,
              });
    if (r.status != TUI_MENU_OK) {
      open = false; /* Esc / q resumes the showcase */
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

/* ============================================================
 * Section 2: Menu definition - your items + dispatch table.
 * ============================================================ */

static const tui_menu_item_t main_menu[] = {
    {.label = "&Overview",
     .description =
         "See the starter's CLI, TUI foundation, and core architecture",
     .id = APP_MENU_OVERVIEW},
    {.label = "&System Information",
     .description =
         "Inspect build metadata, terminal dimensions, and color support",
     .id = APP_MENU_SYSTEM_INFO},
    {.label = "&Input Dialog",
     .description = "Capture bounded text with scoped echo and cursor state",
     .id = APP_MENU_INPUT},
    {.kind = TUI_MENU_ITEM_SEPARATOR},
    {.label = "&Progress Pattern",
     .description =
         "Show a modal progress indicator with percentage and status",
     .id = APP_MENU_PROGRESS},
    {.label = "&Layout Pattern",
     .description = "Open a reusable bordered panel with a status line",
     .id = APP_MENU_LAYOUT},
    {.label = "&Configuration",
     .description = "Adjust output mode, log level, and terminal settings",
     .id = APP_MENU_CONFIGURATION},
    {.label = "Co&mmands",
     .description = "Browse CLI command metadata surfaced via the shared "
                    "action adapter",
     .id = APP_MENU_COMMANDS},
    {.kind = TUI_MENU_ITEM_SEPARATOR},
    {.label = "E&xit",
     .description = "Return to the shell",
     .id = APP_MENU_EXIT},
};

static void app_dispatch(int id) {
  switch (id) {
  case APP_MENU_OVERVIEW:
    app_show_overview();
    break;
  case APP_MENU_SYSTEM_INFO:
    app_show_system_info();
    break;
  case APP_MENU_INPUT:
    app_show_input();
    break;
  case APP_MENU_PROGRESS:
    app_show_progress();
    break;
  case APP_MENU_LAYOUT:
    app_show_layout();
    break;
  case APP_MENU_CONFIGURATION:
    app_show_config_menu();
    break;
  case APP_MENU_COMMANDS:
    app_show_commands();
    break;
  default:
    tui_beep();
    break;
  }
}

static app_error app_error_from_tui_interrupt(void) {
  return tui_take_interrupt_error();
}

/* ============================================================
 * Section 3: Entry point - usually no edits needed here.
 * ============================================================ */

app_error tui_run_app(void) {
  app_error err = tui_init();
  if (err != APP_SUCCESS)
    return err;

  /* Own the menu frame here so it remains visible behind dialog modals
   * that handlers may open. tui_show_menu restores this caller-owned frame
   * on entry and KEY_RESIZE, and reports TUI_MENU_TOO_SMALL if it no
   * longer fits. */
  tui_window_t *menu_frame =
      tui_create_centered_window(MAIN_MENU_FRAME_HEIGHT, MAIN_MENU_FRAME_WIDTH);
  if (!menu_frame) {
    tui_cleanup();
    return APP_ERROR_OUT_OF_RANGE;
  }
  /* Borderless, centered panel: tui_show_menu paints the flat dawn-style
   * layout itself. */
  tui_push_background(menu_frame);

  bool running = true;
  while (running) {
    tui_menu_result_t r = tui_show_menu(
        menu_frame,
        &(tui_menu_config_t){
            .title = "Starter Showcase",
            .subtitle = APP_NAME " · v" APP_VERSION,
            .items = main_menu,
            .item_count = (int)(sizeof(main_menu) / sizeof(main_menu[0])),
            .default_index = 0,
            .frame_height = MAIN_MENU_FRAME_HEIGHT,
            .frame_width = MAIN_MENU_FRAME_WIDTH,
            .enable_search = true,
            .enable_mouse = true,
            .enable_menu_key = true,
            .show_numeric_keys = true,
        });
    switch (r.status) {
    case TUI_MENU_OK:
      if (r.selected_id == APP_MENU_EXIT) {
        running = !tui_confirm("Exit", "Return to the shell?");
      } else {
        app_dispatch(r.selected_id);
      }
      break;
    case TUI_MENU_MENU:
      if (app_show_menu_overlay())
        running = !tui_confirm("Exit", "Return to the shell?");
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
  tui_cleanup();
  return err;
}
