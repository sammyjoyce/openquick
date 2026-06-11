/*
 * Shared helpers for in-process unit tests.
 *
 * Output is TAP so this runner slots into the same harness as the contract
 * and terminal tests.
 */
#pragma once

#include <stdbool.h>
#include <stdio.h>

/* Windows lacks POSIX setenv/unsetenv; map them onto _putenv_s so the
 * env-driven unit tests compile and run on every platform. _putenv_s(name, "")
 * removes the variable, matching unsetenv(); setenv's overwrite flag is
 * implicit (_putenv_s always overwrites). */
#ifdef _WIN32
#include <stdlib.h>
#define setenv(name, value, overwrite) _putenv_s((name), (value))
#define unsetenv(name) _putenv_s((name), "")
#endif

typedef struct {
  int passed;
  int failed;
} unit_stats_t;

static inline void unit_record(unit_stats_t *stats, bool ok, const char *name) {
  if (ok) {
    stats->passed++;
    printf("ok %d - %s\n", stats->passed + stats->failed, name);
  } else {
    stats->failed++;
    printf("not ok %d - %s\n", stats->passed + stats->failed, name);
  }
}

void run_config_unit_tests(unit_stats_t *stats);
void run_input_unit_tests(unit_stats_t *stats);
void run_tui_menu_unit_tests(unit_stats_t *stats);
void run_ui_theme_unit_tests(unit_stats_t *stats);
void run_cli_style_unit_tests(unit_stats_t *stats);
void run_cli_osc11_unit_tests(unit_stats_t *stats);
void run_shared_primitives_unit_tests(unit_stats_t *stats);
void run_components_unit_tests(unit_stats_t *stats);
