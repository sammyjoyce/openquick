/*
 * Thin TAP runner for unit suites that do not need a subprocess.
 */

#include <locale.h>
#include <stdio.h>

#include "unit_support.h"

int main(void) {
  unit_stats_t stats = {0};
  setlocale(LC_ALL, ""); /* required for wchar_t menu model tests */
  printf("TAP version 13\n");

  run_config_unit_tests(&stats);
  run_input_unit_tests(&stats);
  run_tui_menu_unit_tests(&stats);
  run_ui_theme_unit_tests(&stats);
  run_cli_style_unit_tests(&stats);
  run_cli_osc11_unit_tests(&stats);
  run_shared_primitives_unit_tests(&stats);
  run_components_unit_tests(&stats);

  printf("1..%d\n", stats.passed + stats.failed);
  fprintf(stderr, "%d passed, %d failed\n", stats.passed, stats.failed);
  return stats.failed == 0 ? 0 : 1;
}
