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

app_error app_cmd_list(const app_config_t *config, int argc,
                       char *const argv[]);

static void list_print_empty_json(void) {
  fputs("{\"format_version\":\"1.0\",\"sites\":[]}", stdout);
  fputc('\n', stdout);
}

app_error app_cmd_list(const app_config_t *config, int argc,
                       char *const argv[]) {
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    return err;
  }

  quick_list_request_t request = {
      .profiles = &profiles,
      .overrides = {.profile = quick_cmd_value(argc, argv, "--profile")},
      .remote = quick_cmd_flag(argc, argv, "--remote"),
  };
  quick_list_result_t result;
  quick_list_result_init(&result);
  err = quick_op_list(&request, &result);
  quick_profile_config_destroy(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to resolve list profile");
    quick_list_result_destroy(&result);
    return err;
  }

  quick_list_item_t *local = NULL;
  for (size_t i = 0; i < result.count; i++) {
    if (result.items[i].source == QUICK_LIST_SOURCE_LOCAL) {
      local = &result.items[i];
      break;
    }
  }

  if (app_config_is_json_output(config) || quick_cmd_flag(argc, argv, "--json")) {
    if (result.remote_ok && !result.have_local && result.remote_json) {
      fputs(result.remote_json, stdout);
      if (result.remote_json[strlen(result.remote_json) - 1] != '\n') {
        fputc('\n', stdout);
      }
    } else if (local) {
      bool comma = false;
      app_json_begin_object(stdout);
      app_json_write_string_field(stdout, "format_version", "1.0", &comma);
      app_json_write_raw_field(stdout, "sites", "[", &comma);
      fputc('{', stdout);
      bool fc = false;
      app_json_write_string_field(stdout, "name", local->name, &fc);
      app_json_write_string_field(stdout, "url", local->url, &fc);
      app_json_write_string_field(stdout, "release", local->release, &fc);
      app_json_write_string_field(stdout, "updated_at", local->updated_at, &fc);
      app_json_write_string_field(stdout, "deployer", "local", &fc);
      if (local->stale) {
        app_json_write_bool_field(stdout, "stale", true, &fc);
      }
      fputc('}', stdout);
      fputc(']', stdout);
      app_json_end_object(stdout);
      app_json_end_line(stdout);
    } else {
      list_print_empty_json();
    }
  } else {
    app_output("site            url                                      updated              by", config, false);
    if (local) {
      app_output_format(config, false, "%-15s %-40s %-20s %s%s",
                        local->name,
                        local->url,
                        local->updated_at ? local->updated_at : "unknown",
                        "local",
                        local->stale ? " (stale)" : "");
    } else if (result.remote_ok && result.remote_json) {
      app_output(result.remote_json, config, false);
    }
  }

  quick_list_result_destroy(&result);
  return APP_SUCCESS;
}
