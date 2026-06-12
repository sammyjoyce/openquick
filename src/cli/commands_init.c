#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/config.h"
#include "../core/deploy_plan.h"
#include "../core/error.h"
#include "../core/ops.h"
#include "../core/profile_config.h"
#include "../io/output.h"
#include "commands.h"
#include "commands_openquick.h"

app_error app_cmd_init(const app_config_t *config, int argc,
                       char *const argv[]);

app_error app_cmd_init(const app_config_t *config, int argc,
                       char *const argv[]) {
  const char *value_opts[] = {"--template", "--name", "--profile"};
  const char *dir_arg = quick_cmd_first_positional(argc, argv, value_opts,
                                                   APP_COUNTOF(value_opts));
  const char *dir = dir_arg ? dir_arg : ".";
  const char *template_name = quick_cmd_value(argc, argv, "--template");
  if (!template_name) {
    template_name = "blank";
  }
  quick_init_template_t template_kind = QUICK_INIT_TEMPLATE_BLANK;
  if (strcmp(template_name, "realtime") == 0) {
    template_kind = QUICK_INIT_TEMPLATE_REALTIME;
  } else if (strcmp(template_name, "blank") != 0) {
    quick_print_error(config, "init --template must be blank or realtime");
    return APP_ERROR_VALIDATION;
  }

  char slug[QUICK_SLUG_MAX + 1];
  const char *name_arg = quick_cmd_value(argc, argv, "--name");
  app_error err = quick_slug_normalize(name_arg ? name_arg : dir, slug);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "site name does not normalize to a DNS label");
    return err;
  }

  quick_profile_config_t profiles;
  err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to read OpenQuick profile config");
    return err;
  }
  const char *profile_arg = quick_cmd_value(argc, argv, "--profile");
  const char *profile = profile_arg ? profile_arg : profiles.default_profile;

  err = quick_mkdir_p_cli(dir, 0755);
  if (err != APP_SUCCESS) {
    quick_profile_config_destroy(&profiles);
    quick_print_error(config, "failed to create site directory");
    return err;
  }

  quick_init_result_t result;
  quick_init_result_init(&result);
  quick_init_request_t request = {
      .target_dir = dir,
      .name = name_arg,
      .template_kind = template_kind,
      .profile = profile,
  };
  err = quick_op_init(&request, &result);

  if (err != APP_SUCCESS) {
    quick_print_error(config,
                      err == APP_ERROR_CONFIG_INVALID
                          ? "refusing to overwrite existing scaffold files"
                          : "failed to write scaffold files");
  } else if (app_config_is_json_output(config)) {
    bool comma = false;
    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "format_version", "1.0", &comma);
    app_json_write_string_field(stdout, "site", result.site, &comma);
    app_json_write_string_field(stdout, "path", result.path, &comma);
    app_json_write_string_field(stdout, "profile", profile, &comma);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
  } else {
    app_output_format(config, false, "Initialized OpenQuick site %s", result.site);
    app_output_format(config, false, "  path        %s", result.path);
    app_output_format(config, false, "  profile     %s", profile ? profile : "(unbound)");
    app_output("  next        quick deploy --dry-run", config, false);
  }

  quick_init_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return err;
}
