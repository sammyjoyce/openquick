/*
 * OpenQuick doctor command.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/app_info.h"
#include "../core/config.h"
#include "../core/deploy_plan.h"
#include "../core/error.h"
#include "../core/ops.h"
#include "../io/output.h"
#include "commands.h"
#include "commands_openquick.h"

app_error app_cmd_doctor(const app_config_t *config, int argc,
                         char *const argv[]);

static void doctor_print_json(const quick_doctor_result_t *result) {
  bool comma = false;
  app_json_begin_object(stdout);
  app_json_write_string_field(stdout, "format_version", "1.0", &comma);
  app_json_write_bool_field(stdout, "ok", result->ok, &comma);
  app_json_write_raw_field(stdout, "checks", "[", &comma);
  for (size_t i = 0; i < result->count; i++) {
    if (i > 0) {
      fputc(',', stdout);
    }
    const quick_doctor_check_t *check = &result->checks[i];
    bool fc = false;
    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "name", check->name, &fc);
    app_json_write_string_field(stdout, "group", check->group, &fc);
    app_json_write_string_field(stdout, "status",
                                quick_doctor_status_string(check->status), &fc);
    app_json_write_string_field(stdout, "detail", check->detail, &fc);
    app_json_write_string_field(stdout, "remediation", check->remediation, &fc);
    app_json_end_object(stdout);
  }
  fputc(']', stdout);
  app_json_end_object(stdout);
  app_json_end_line(stdout);
}

app_error app_cmd_doctor(const app_config_t *config, int argc,
                         char *const argv[]) {
  if (app_config_is_quiet(config)) {
    return APP_SUCCESS;
  }

  quick_profile_config_t profiles;
  quick_cmd_load_profiles(&profiles);
  quick_doctor_request_t request = {
      .profiles = &profiles,
      .site = quick_cmd_value(argc, argv, "--site"),
      .profile = quick_cmd_value(argc, argv, "--profile"),
      .remote = quick_cmd_flag(argc, argv, "--remote"),
      .deep = quick_cmd_flag(argc, argv, "--deep"),
  };

  quick_doctor_result_t result;
  quick_doctor_result_init(&result);
  app_error err = quick_op_doctor(&request, &result);
  if (err == APP_SUCCESS) {
    if (app_config_is_json_output(config) ||
        quick_cmd_flag(argc, argv, "--json")) {
      doctor_print_json(&result);
    } else {
      const app_build_info_t *build = app_build_info();
      app_output_format(config, false, "%s doctor", build->name);
      for (size_t i = 0; i < result.count; i++) {
        app_output_format(config, false, "  %-16s %-8s %-4s %s",
                          result.checks[i].group, result.checks[i].name,
                          quick_doctor_status_string(result.checks[i].status),
                          result.checks[i].detail);
      }
    }
  }

  quick_doctor_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return err == APP_SUCCESS ? APP_SUCCESS : err;
}
