#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/config.h"
#include "../core/deploy_plan.h"
#include "../core/error.h"
#include "../core/ops.h"
#include "../io/output.h"
#include "commands.h"
#include "commands_openquick.h"

app_error app_cmd_open(const app_config_t *config, int argc,
                       char *const argv[]);

static void open_warn(const app_config_t *config, const char *message) {
  app_output_format(config, true, "Warning: %s", message);
}

app_error app_cmd_open(const app_config_t *config, int argc,
                       char *const argv[]) {
  const char *value_opts[] = {"--profile"};
  const char *site_arg = quick_cmd_first_positional(argc, argv, value_opts,
                                                    APP_COUNTOF(value_opts));
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    return err;
  }
  quick_plan_overrides_t overrides = {
      .site = site_arg,
      .profile = quick_cmd_value(argc, argv, "--profile"),
  };
  quick_url_result_t result;
  quick_url_result_init(&result);
  err = quick_op_resolve_url(&profiles, &overrides, &result);
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  if (err == APP_SUCCESS) {
    err = quick_deploy_plan_resolve(&overrides, &profiles, &plan);
  }
  quick_profile_config_destroy(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to resolve site URL");
    quick_deploy_plan_destroy(&plan);
    quick_url_result_destroy(&result);
    return err;
  }

  const bool plain = app_config_is_plain_output(config) ||
                     quick_cmd_flag(argc, argv, "--plain") ||
                     app_config_is_json_output(config);
  const bool copy = quick_cmd_flag(argc, argv, "--copy");

  if (app_config_is_json_output(config)) {
    bool comma = false;
    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "format_version", "1.0", &comma);
    app_json_write_string_field(stdout, "site", plan.site, &comma);
    app_json_write_string_field(stdout, "profile", plan.profile, &comma);
    app_json_write_string_field(stdout, "url", result.url, &comma);
    app_json_write_bool_field(stdout, "deployed", result.live, &comma);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
  } else if (plain) {
    printf("%s%s", result.url, result.live ? "\n" : " (not yet deployed)\n");
  } else {
    app_output_format(config, false, "%s%s", result.url,
                      result.live ? "" : " (not yet deployed)");
    if (!copy) {
      (void)quick_op_open_url(result.url);
    }
  }

  if (copy) {
    char *message = NULL;
    (void)quick_op_copy_url(result.url, &message);
    if (message) {
      open_warn(config, message);
      free(message);
    }
  }

  quick_deploy_plan_destroy(&plan);
  quick_url_result_destroy(&result);
  return APP_SUCCESS;
}
