/*
 * "info" command - prints build metadata in plain or JSON form.
 */

#include <stdio.h>

#include "../core/app_info.h"
#include "../core/config.h"
#include "../core/error.h"
#include "../core/types.h"
#include "../io/output.h"
#include "commands.h"

app_error app_cmd_info(const app_config_t *config, int argc,
                       char *const argv[]);

app_error app_cmd_info(const app_config_t *config, int argc,
                       char *const argv[]) {
  (void)argc;
  (void)argv;

  if (app_config_is_quiet(config)) {
    return APP_SUCCESS;
  }

  const app_build_info_t *build = app_build_info();
  size_t feature_count = 0;
  const app_feature_info_t *features = app_feature_table(&feature_count);

  if (app_config_is_json_output(config)) {
    bool root_comma = false;
    bool feature_comma = false;

    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "format_version", "1.0", &root_comma);
    app_json_write_string_field(stdout, "app", build->name, &root_comma);
    app_json_write_string_field(stdout, "version", build->version, &root_comma);
    app_json_write_string_field(stdout, "git_commit", build->git_commit,
                                &root_comma);
    app_json_write_string_field(stdout, "build_date", build->build_date,
                                &root_comma);
    app_json_write_raw_field(stdout, "features", "{", &root_comma);
    for (size_t i = 0; i < feature_count; i++) {
      app_json_write_bool_field(stdout, features[i].key, features[i].compiled,
                                &feature_comma);
    }
    app_json_end_object(stdout);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
    return APP_SUCCESS;
  }

  app_output_format(config, false, "Application: %s", build->name);
  app_output_format(config, false, "Version: %s", build->version);
  app_output_format(config, false, "Git Commit: %s", build->git_commit);
  app_output_format(config, false, "Build: %s", build->build_date);
  for (size_t i = 0; i < feature_count; i++) {
    app_output_format(config, false, "%s: %s", features[i].label,
                      app_bool_word(features[i].compiled));
  }
  return APP_SUCCESS;
}
