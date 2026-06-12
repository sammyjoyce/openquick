#ifndef TERMINAL_VT_SCENARIOS_H
#define TERMINAL_VT_SCENARIOS_H

#include "terminal_vt_support.h"

int run_tui_menu_test(test_stats_t *stats, const char *binary,
                      bool tui_enabled);
int run_tui_bare_invocation(test_stats_t *stats, const char *binary,
                            bool tui_enabled);
int run_tui_bare_invocation_json(test_stats_t *stats, const char *binary,
                                 bool tui_enabled);
int run_tui_stress_smoke(test_stats_t *stats, const char *binary,
                         bool tui_enabled);
int run_tui_menu_search(test_stats_t *stats, const char *binary,
                        bool tui_enabled);
int run_tui_menu_mnemonic(test_stats_t *stats, const char *binary,
                          bool tui_enabled);
int run_tui_menu_separator(test_stats_t *stats, const char *binary,
                           bool tui_enabled);
int run_tui_menu_resize(test_stats_t *stats, const char *binary,
                        bool tui_enabled);
int run_tui_menu_handler_resize(test_stats_t *stats, const char *binary,
                                bool tui_enabled);
int run_tui_menu_sigint(test_stats_t *stats, const char *binary,
                        bool tui_enabled);
int run_tui_menu_sigterm(test_stats_t *stats, const char *binary,
                         bool tui_enabled);
int run_tui_sites_empty_state(test_stats_t *stats, const char *binary,
                              bool tui_enabled);
int run_tui_new_site_scaffold(test_stats_t *stats, const char *binary,
                              bool tui_enabled);
int run_tui_doctor_local_results(test_stats_t *stats, const char *binary,
                                 bool tui_enabled);
int run_tui_settings_profiles_from_xdg(test_stats_t *stats, const char *binary,
                                       bool tui_enabled);
int run_tui_deploy_plan_panel(test_stats_t *stats, const char *binary,
                              bool tui_enabled);
int run_tui_serve_install_guide(test_stats_t *stats, const char *binary,
                                bool tui_enabled);

#endif
