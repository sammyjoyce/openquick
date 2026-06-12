#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../core/config.h"
#include "../core/deploy_plan.h"
#include "../core/error.h"
#include "../core/process.h"

const char *quick_cmd_value(int argc, char *const argv[], const char *name);
bool quick_cmd_flag(int argc, char *const argv[], const char *name);
const char *quick_cmd_first_positional(int argc, char *const argv[],
                                       const char *const *value_options,
                                       size_t value_option_count);
app_error quick_cmd_load_profiles(quick_profile_config_t *profiles);
char *quick_path_join_cli(const char *a, const char *b);
bool quick_path_exists_cli(const char *path);
bool quick_dir_exists_cli(const char *path);
app_error quick_mkdir_p_cli(const char *path, int mode);
char *quick_find_executable_cli(const char *name);
app_error quick_read_file_cli(const char *path, char **out);
char *quick_json_get_string_field_cli(const char *json, const char *field);
long quick_json_get_long_field_cli(const char *json, const char *field,
                                   long fallback);
bool quick_cmd_prompt_site_confirmation(const app_config_t *config,
                                        const char *site,
                                        const char *message);
void quick_print_error(const app_config_t *config, const char *message);
