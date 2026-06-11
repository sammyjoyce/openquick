/*
 * Shared application/build/feature metadata.
 *
 * This module is intentionally curses-free so CLI and optional TUI code can
 * share facts without making the default build depend on ncurses/PDCurses.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

typedef enum {
  APP_FEATURE_TUI = 0,
  APP_FEATURE_CLI_STYLE,
  APP_FEATURE_TERMINFO,
} app_feature_id_t;

typedef struct {
  const char *name;
  const char *version;
  const char *git_commit;
  const char *build_date;
} app_build_info_t;

typedef struct {
  app_feature_id_t id;
  const char *key;
  const char *label;
  bool compiled;
  bool requires_terminal;
  const char *build_option;
  const char *dependency;
} app_feature_info_t;

const app_build_info_t *app_build_info(void);
const app_feature_info_t *app_feature_table(size_t *count);
const app_feature_info_t *app_feature_find(app_feature_id_t id);
bool app_feature_compiled(app_feature_id_t id);
const char *app_bool_word(bool value);
