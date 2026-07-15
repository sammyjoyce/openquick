#include "tui_app_state.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static char *quick_tui_strdup(const char *value) {
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

void quick_tui_app_state_init(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
  *state = (quick_tui_app_state_t){0};
  quick_profile_config_init(&state->profiles);
  quick_onboarding_model_init(&state->onboarding);
#ifndef _WIN32
  state->serve_pid = -1;
#else
  state->serve_pid = 0;
#endif
}

void quick_tui_stop_serve_child(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
#ifndef _WIN32
  if (state->serve_pid > 0) {
    (void)kill(state->serve_pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
      int status = 0;
      pid_t waited = waitpid(state->serve_pid, &status, WNOHANG);
      if (waited == state->serve_pid) {
        break;
      }
      if (waited < 0) {
        break;
      }
      usleep(25000);
    }
    int status = 0;
    if (waitpid(state->serve_pid, &status, WNOHANG) == 0) {
      (void)kill(state->serve_pid, SIGKILL);
      (void)waitpid(state->serve_pid, &status, 0);
    }
  }
  state->serve_pid = -1;
#else
  state->serve_pid = 0;
#endif
  state->serve_port = 0;
  state->serve_url[0] = '\0';
}

void quick_tui_poll_serve_child(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
#ifndef _WIN32
  if (state->serve_pid <= 0) {
    return;
  }
  int status = 0;
  pid_t waited = waitpid(state->serve_pid, &status, WNOHANG);
  if (waited == state->serve_pid) {
    state->serve_pid = -1;
    state->serve_port = 0;
    state->serve_url[0] = '\0';
    snprintf(state->status, sizeof(state->status), "local dev server stopped");
  }
#endif
}

app_error quick_tui_start_serve_child(quick_tui_app_state_t *state,
                                      const char *dir, const char *port,
                                      const char *identity) {
  quick_tui_poll_serve_child(state);
  if (!state) {
    return APP_ERROR_INVALID_ARG;
  }
#ifndef _WIN32
  if (state->serve_pid > 0) {
    return APP_SUCCESS;
  }
#endif

  const char *port_used = (port && port[0]) ? port : "9366";
  quick_serve_dev_request_t request = {
      .profiles = &state->profiles,
      .profile = quick_tui_default_profile_name(state),
      .port = port_used,
      .identity = (identity && identity[0]) ? identity : NULL,
      .dir = (dir && dir[0]) ? dir : NULL,
  };
  quick_serve_dev_command_t command;
  quick_serve_dev_command_init(&command);
  app_error err = quick_op_serve_dev_command(&request, &command);
  if (err != APP_SUCCESS) {
    quick_serve_dev_command_destroy(&command);
    return err;
  }

#ifndef _WIN32
  pid_t pid = fork();
  if (pid < 0) {
    quick_serve_dev_command_destroy(&command);
    return APP_ERROR_IO;
  }
  if (pid == 0) {
    if (dir && dir[0] && chdir(dir) != 0) {
      _exit(126);
    }
    execvp(command.argv[0], command.argv);
    _exit(errno == ENOENT ? 127 : 126);
  }

  state->serve_pid = pid;
  state->serve_port = atoi(port_used);
  char *url = NULL;
  (void)quick_op_serve_local_url(command.site, port_used, &url);
  snprintf(state->serve_url, sizeof(state->serve_url), "%s",
           url ? url : "http://localhost:<port>/");
  free(url);
  quick_serve_dev_command_destroy(&command);
  return APP_SUCCESS;
#else
  quick_serve_dev_command_destroy(&command);
  return APP_ERROR_FEATURE_BASE;
#endif
}

void quick_tui_app_state_destroy(quick_tui_app_state_t *state) {
  if (!state) {
    return;
  }
  quick_tui_stop_serve_child(state);
  quick_profile_config_destroy(&state->profiles);
  free(state->profile_config_path);
  *state = (quick_tui_app_state_t){0};
}

app_error quick_tui_reload_profiles(quick_tui_app_state_t *state) {
  if (!state) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_profile_config_destroy(&state->profiles);
  quick_profile_config_init(&state->profiles);
  free(state->profile_config_path);
  state->profile_config_path = quick_profile_config_default_path();
  if (!state->profile_config_path) {
    state->profile_config_path = quick_tui_strdup("(no config path)");
  }
  app_error err = quick_profile_config_load_default(&state->profiles);
  state->profiles_loaded = err == APP_SUCCESS;
  return err;
}

const char *quick_tui_default_profile_name(const quick_tui_app_state_t *state) {
  if (state && state->profiles.default_profile &&
      state->profiles.default_profile[0] != '\0') {
    return state->profiles.default_profile;
  }
  return "local";
}

const char *quick_tui_profile_config_path(const quick_tui_app_state_t *state) {
  if (state && state->profile_config_path &&
      state->profile_config_path[0] != '\0') {
    return state->profile_config_path;
  }
  return "(no config path)";
}

void quick_tui_set_status(quick_tui_app_state_t *state, const char *message) {
  if (!state) {
    return;
  }
  snprintf(state->status, sizeof(state->status), "%s", message ? message : "");
}
