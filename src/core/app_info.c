/*
 * Shared application/build/feature metadata. Keep this file free of TUI/curses
 * includes so the base CLI can consume it without pulling terminal UI deps.
 */
#include "app_info.h"

#ifndef APP_NAME
#define APP_NAME "myapp"
#endif
#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif
#ifndef APP_GIT_COMMIT
#define APP_GIT_COMMIT "unknown"
#endif
#ifndef APP_BUILD_DATE
#define APP_BUILD_DATE "omitted"
#endif
#ifndef APP_HAVE_TERMINFO
#define APP_HAVE_TERMINFO 0
#endif

static const app_build_info_t APP_BUILD_INFO = {
    .name = APP_NAME,
    .version = APP_VERSION,
    .git_commit = APP_GIT_COMMIT,
    .build_date = APP_BUILD_DATE,
};

static const app_feature_info_t APP_FEATURES[] = {
    {.id = APP_FEATURE_TUI,
     .key = "tui",
     .label = "TUI Support",
#ifdef ENABLE_TUI
     .compiled = true,
#else
     .compiled = false,
#endif
     .requires_terminal = true,
     .build_option = "-Denable-tui=true",
     /* Generic curses dependency name. Kept platform-independent so the
      * OpenCLI `requires` contract (the only consumer) matches the checked-in
      * opencli.json on every platform; the concrete backend is ncursesw on
      * Unix and PDCurses (an ncurses-compatible API) on Windows. */
     .dependency = "ncurses"},
    {.id = APP_FEATURE_CLI_STYLE,
     .key = "cli_style",
     .label = "Styled CLI output",
#ifdef APP_ENABLE_CLI_STYLE
     .compiled = true,
#else
     .compiled = false,
#endif
     .requires_terminal = false,
     .build_option = "-Denable-cli-style=true",
     .dependency = NULL},
    {.id = APP_FEATURE_TERMINFO,
     .key = "terminfo",
     .label = "Terminfo backend",
     .compiled = APP_HAVE_TERMINFO != 0,
     .requires_terminal = false,
     .build_option = "-Dcli-terminfo=auto|required",
     .dependency = "terminfo"},
};

const app_build_info_t *app_build_info(void) {
  return &APP_BUILD_INFO;
}

const app_feature_info_t *app_feature_table(size_t *count) {
  if (count) {
    *count = sizeof(APP_FEATURES) / sizeof(APP_FEATURES[0]);
  }
  return APP_FEATURES;
}

const app_feature_info_t *app_feature_find(app_feature_id_t id) {
  for (size_t i = 0; i < sizeof(APP_FEATURES) / sizeof(APP_FEATURES[0]); i++) {
    if (APP_FEATURES[i].id == id) {
      return &APP_FEATURES[i];
    }
  }
  return NULL;
}

bool app_feature_compiled(app_feature_id_t id) {
  const app_feature_info_t *feature = app_feature_find(id);
  return feature && feature->compiled;
}

const char *app_bool_word(bool value) {
  return value ? "yes" : "no";
}
