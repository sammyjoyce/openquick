#pragma once

#include <stdbool.h>
#include <stddef.h>
#ifndef _WIN32
#include <sys/types.h>
#endif

#include "../core/error.h"
#include "../core/profile_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  quick_profile_config_t profiles;
  char *profile_config_path;
  bool profiles_loaded;
#ifndef _WIN32
  pid_t serve_pid;
#else
  int serve_pid;
#endif
  int serve_port;
  char serve_url[160];
  char status[256];
} quick_tui_app_state_t;

void quick_tui_app_state_init(quick_tui_app_state_t *state);
void quick_tui_app_state_destroy(quick_tui_app_state_t *state);
app_error quick_tui_reload_profiles(quick_tui_app_state_t *state);
const char *quick_tui_default_profile_name(const quick_tui_app_state_t *state);
const char *quick_tui_profile_config_path(const quick_tui_app_state_t *state);
void quick_tui_set_status(quick_tui_app_state_t *state, const char *message);
void quick_tui_poll_serve_child(quick_tui_app_state_t *state);
void quick_tui_stop_serve_child(quick_tui_app_state_t *state);

void quick_tui_screen_sites(quick_tui_app_state_t *state);
void quick_tui_screen_deploy(quick_tui_app_state_t *state);
void quick_tui_screen_deploy_site(quick_tui_app_state_t *state,
                                  const char *site);
void quick_tui_screen_init(quick_tui_app_state_t *state);
void quick_tui_screen_doctor(quick_tui_app_state_t *state);
void quick_tui_screen_serve(quick_tui_app_state_t *state);
void quick_tui_screen_config(quick_tui_app_state_t *state);

#ifdef __cplusplus
}
#endif
