#include "tui_app_state.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "../core/deploy_plan.h"
#include "../core/ops.h"
#include "tui.h"
#include "tui_internal.h"
#include "tui_panel.h"

static const quick_profile_t *quick_tui_find_profile_or_default(
    quick_tui_app_state_t *state, const char *name) {
  const char *profile_name = name && name[0] ? name : quick_tui_default_profile_name(state);
  return quick_profile_config_find(&state->profiles, profile_name);
}

typedef struct {
  quick_tui_app_state_t *state;
  char action;
} quick_tui_serve_status_state_t;

static void quick_tui_serve_status_redraw(tui_window_t *window,
                                          void *userdata) {
  quick_tui_serve_status_state_t *panel = userdata;
  quick_tui_app_state_t *state = panel->state;
  quick_tui_poll_serve_child(state);
  tui_draw_border(window);
#ifndef _WIN32
  if (state->serve_pid > 0) {
    char line[256];
    snprintf(line, sizeof(line), "running on %s (pid %ld)", state->serve_url,
             (long)state->serve_pid);
    tui_set_color(window->win, TUI_COLOR_SUCCESS);
    tui_print_centered(window->win, 4, line);
    tui_unset_color(window->win, TUI_COLOR_SUCCESS);
    tui_set_color(window->win, TUI_COLOR_INFO);
    tui_print_centered(window->win, window->height - 2,
                       "s:stop  o:open  Esc:back");
    tui_unset_color(window->win, TUI_COLOR_INFO);
  } else
#endif
  {
    tui_set_color(window->win, TUI_COLOR_WARNING);
    tui_print_centered(window->win, 4, "local dev server is stopped");
    tui_unset_color(window->win, TUI_COLOR_WARNING);
    tui_set_color(window->win, TUI_COLOR_INFO);
    tui_print_centered(window->win, window->height - 2, "Esc:back");
    tui_unset_color(window->win, TUI_COLOR_INFO);
  }
}

static tui_modal_decision_t quick_tui_serve_status_key(tui_window_t *window,
                                                       int ch,
                                                       void *userdata) {
  (void)window;
  quick_tui_serve_status_state_t *panel = userdata;
  if (ch == 's' || ch == 'S') {
    panel->action = 's';
    return TUI_MODAL_DONE;
  }
  if (ch == 'o' || ch == 'O') {
    panel->action = 'o';
    return TUI_MODAL_DONE;
  }
  if (ch == 27 || ch == 'q' || ch == 'Q' || ch == '\n' || ch == KEY_ENTER) {
    return TUI_MODAL_DONE;
  }
  return TUI_MODAL_CONTINUE;
}

static void quick_tui_serve_show_status(quick_tui_app_state_t *state) {
  quick_tui_serve_status_state_t panel = {.state = state, .action = 0};
  (void)tui_modal_run(10, 70, "Local dev server",
                      quick_tui_serve_status_redraw,
                      quick_tui_serve_status_key, &panel);
  if (panel.action == 's') {
    quick_tui_stop_serve_child(state);
    tui_show_message("Local dev server", "Stopped local dev server.");
  } else if (panel.action == 'o' && state->serve_url[0]) {
    (void)quick_op_open_url(state->serve_url);
  }
}

static void quick_tui_serve_start_dev(quick_tui_app_state_t *state) {
  quick_tui_poll_serve_child(state);
#ifndef _WIN32
  if (state->serve_pid > 0) {
    quick_tui_serve_show_status(state);
    return;
  }
#endif
  char port[32] = {0};
  if (tui_input_dialog("Local dev server", "Port (blank for 9366):", port,
                       sizeof(port)) != APP_SUCCESS) {
    return;
  }
  char identity[160] = {0};
  if (tui_input_dialog("Local dev server", "Identity email (optional):",
                       identity, sizeof(identity)) != APP_SUCCESS) {
    return;
  }
  const char *use_port = port[0] ? port : "9366";
  quick_serve_dev_command_t command;
  quick_serve_dev_command_init(&command);
  quick_serve_dev_request_t request = {.profiles = &state->profiles,
                                       .profile = quick_tui_default_profile_name(state),
                                       .port = use_port,
                                       .identity = identity[0] ? identity : NULL};
  app_error err = quick_op_serve_dev_command(&request, &command);
  if (err != APP_SUCCESS) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Could not build dev server command: %s",
             app_strerror(err));
    tui_show_message("Local dev server", msg);
    quick_serve_dev_command_destroy(&command);
    return;
  }
#ifndef _WIN32
  pid_t pid = fork();
  if (pid < 0) {
    char msg[160];
    snprintf(msg, sizeof(msg), "fork failed: %s", strerror(errno));
    tui_show_message("Local dev server", msg);
    quick_serve_dev_command_destroy(&command);
    return;
  }
  if (pid == 0) {
    execvp(command.argv[0], command.argv);
    _exit(errno == ENOENT ? 127 : 126);
  }
  state->serve_pid = pid;
  state->serve_port = atoi(use_port);
  snprintf(state->serve_url, sizeof(state->serve_url), "http://localhost:%s",
           use_port);
  quick_serve_dev_command_destroy(&command);
  quick_tui_serve_show_status(state);
#else
  quick_serve_dev_command_destroy(&command);
  tui_show_message("Local dev server", "Local dev server is not supported on this platform.");
#endif
}

static void quick_tui_prompt_default(const char *title, const char *label,
                                     const char *def, char *out,
                                     size_t out_size) {
  char prompt[256];
  snprintf(prompt, sizeof(prompt), "%s [%s]:", label,
           def && def[0] ? def : "none");
  out[0] = '\0';
  if (tui_input_dialog(title, prompt, out, out_size) != APP_SUCCESS) {
    out[0] = '\0';
    return;
  }
  if (out[0] == '\0' && def) {
    snprintf(out, out_size, "%s", def);
  }
}

static bool quick_tui_serve_validate_install(const char *profile,
                                             const char *host,
                                             const char *remote_root,
                                             const char *domain,
                                             const char *iap) {
  if (!quick_profile_name_is_safe(profile)) {
    tui_show_message("Host install guide", "Profile names may use letters, digits, dot, underscore, and dash.");
    return false;
  }
  if (!quick_ssh_target_is_safe(host)) {
    tui_show_message("Host install guide", "Host is not a safe SSH target.");
    return false;
  }
  if (!quick_remote_path_is_safe(remote_root)) {
    tui_show_message("Host install guide", "Remote root must be an absolute safe path without .. segments.");
    return false;
  }
  if (!quick_domain_is_safe(domain)) {
    tui_show_message("Host install guide", "Domain must be a safe DNS name or localhost.");
    return false;
  }
  if (!quick_profile_name_is_safe(iap)) {
    tui_show_message("Host install guide", "IAP type may use letters, digits, dot, underscore, and dash.");
    return false;
  }
  return true;
}

static void quick_tui_serve_install_guide(quick_tui_app_state_t *state) {
  char profile[128] = {0};
  char host[256] = {0};
  char remote_root[256] = {0};
  char domain[256] = {0};
  char iap[64] = {0};

  quick_tui_prompt_default("Host install guide", "Profile",
                           quick_tui_default_profile_name(state), profile,
                           sizeof(profile));
  if (profile[0] == '\0') {
    return;
  }
  const quick_profile_t *p = quick_tui_find_profile_or_default(state, profile);
  quick_tui_prompt_default("Host install guide", "SSH host",
                           p && p->ssh ? p->ssh : "quick@host", host,
                           sizeof(host));
  quick_tui_prompt_default("Host install guide", "Remote root",
                           p && p->remote_root ? p->remote_root : "/srv/quick",
                           remote_root, sizeof(remote_root));
  quick_tui_prompt_default("Host install guide", "Base domain",
                           p && p->base_domain ? p->base_domain : "localhost",
                           domain, sizeof(domain));
  quick_tui_prompt_default("Host install guide", "IAP type",
                           p && p->iap.type ? p->iap.type : "tailscale", iap,
                           sizeof(iap));
  if (!quick_tui_serve_validate_install(profile, host, remote_root, domain,
                                        iap)) {
    return;
  }

  quick_serve_install_steps_t steps;
  quick_serve_install_steps_init(&steps);
  quick_serve_install_request_t request = {.profile = profile,
                                           .host = host,
                                           .remote_root = remote_root,
                                           .domain = domain,
                                           .iap = iap};
  app_error err = quick_op_serve_install_steps(&request, &steps);
  if (err != APP_SUCCESS) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Could not build install guide: %s",
             app_strerror(err));
    tui_show_message("Host install guide", msg);
    quick_serve_install_steps_destroy(&steps);
    return;
  }

  const size_t line_count = steps.count + 3U;
  char **lines = calloc(line_count, sizeof(char *));
  if (!lines) {
    quick_serve_install_steps_destroy(&steps);
    tui_show_message("Host install guide", "Out of memory while rendering guide.");
    return;
  }
  lines[0] = malloc(1024);
  if (lines[0]) {
    snprintf(lines[0], 1024,
             "Run when ready: quick serve install --profile %s --host %s --remote-root %s --domain %s --iap %s --execute",
             profile, host, remote_root, domain, iap);
  }
  lines[1] = malloc(2);
  if (lines[1]) {
    strcpy(lines[1], "");
  }
  lines[2] = malloc(64);
  if (lines[2]) {
    strcpy(lines[2], "Guided steps (read-only; nothing executed here):");
  }
  for (size_t i = 0; i < steps.count; i++) {
    lines[i + 3U] = malloc(512);
    if (lines[i + 3U]) {
      snprintf(lines[i + 3U], 512, "%zu. %s", i + 1U,
               steps.steps[i].summary ? steps.steps[i].summary : "");
    }
  }
  quick_tui_show_lines_panel("Host install guide", (const char *const *)lines,
                             line_count, "Esc closes");
  for (size_t i = 0; i < line_count; i++) {
    free(lines[i]);
  }
  free(lines);
  quick_serve_install_steps_destroy(&steps);
}

void quick_tui_screen_serve(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
  (void)quick_tui_reload_profiles(state);
  bool open = true;
  while (open && !tui_interrupted()) {
    quick_tui_poll_serve_child(state);
    char subtitle[180];
#ifndef _WIN32
    if (state->serve_pid > 0) {
      snprintf(subtitle, sizeof(subtitle), "running on %s (pid %ld)",
               state->serve_url, (long)state->serve_pid);
    } else
#endif
    {
      snprintf(subtitle, sizeof(subtitle), "local dev server stopped");
    }
    const tui_menu_item_t items[] = {
        {.label = "&Local dev server",
         .description = "Start/inspect a quickd --dev child process",
         .id = 1},
        {.label = "&Host install guide",
         .description = "Read-only install steps and command to run",
         .id = 2},
        {.kind = TUI_MENU_ITEM_SEPARATOR},
        {.label = "&Back", .description = "Return to OpenQuick", .id = 3},
    };
    tui_menu_result_t r = tui_show_menu(
        NULL, &(tui_menu_config_t){.title = "Serve",
                                   .subtitle = subtitle,
                                   .items = items,
                                   .item_count = (int)(sizeof(items) / sizeof(items[0])),
                                   .default_index = 0,
                                   .frame_height = 14,
                                   .frame_width = 74,
                                   .show_numeric_keys = true});
    if (r.status != TUI_MENU_OK || r.selected_id == 3) {
      open = false;
    } else if (r.selected_id == 1) {
      quick_tui_serve_start_dev(state);
    } else if (r.selected_id == 2) {
      quick_tui_serve_install_guide(state);
    }
  }
}
