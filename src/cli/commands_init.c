#include <stdbool.h>
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
app_error app_cmd_templates(const app_config_t *config, int argc,
                            char *const argv[]);

typedef struct {
  const char *name;
  const char *description;
  const char *files;
  bool sdk_demo;
} quick_template_info_t;

static const quick_template_info_t g_templates[] = {
    {.name = "blank",
     .description = "Minimal static HTML site with OpenQuick metadata",
     .files = "index.html, quick.json, AGENTS.md, docs/openquick-api.md, "
              ".quickignore",
     .sdk_demo = false},
    {.name = "realtime",
     .description =
         "Static page demonstrating identity plus the same-origin SDK import",
     .files = "index.html, quick.json, AGENTS.md, docs/openquick-api.md, "
              ".quickignore",
     .sdk_demo = true},
};

static void quick_templates_print(const app_config_t *config) {
  if (app_config_is_json_output(config)) {
    bool comma = false;
    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "format_version", "1.0", &comma);
    app_json_write_raw_field(stdout, "templates", "[", &comma);
    for (size_t i = 0; i < APP_COUNTOF(g_templates); i++) {
      if (i > 0) {
        fputc(',', stdout);
      }
      app_json_begin_object(stdout);
      bool tc = false;
      app_json_write_string_field(stdout, "name", g_templates[i].name, &tc);
      app_json_write_string_field(stdout, "description",
                                  g_templates[i].description, &tc);
      app_json_write_string_field(stdout, "files", g_templates[i].files, &tc);
      app_json_write_bool_field(stdout, "sdk_demo", g_templates[i].sdk_demo,
                                &tc);
      app_json_end_object(stdout);
    }
    fputc(']', stdout);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
    return;
  }
  app_output("OpenQuick templates", config, false);
  for (size_t i = 0; i < APP_COUNTOF(g_templates); i++) {
    app_output_format(config, false, "  %-10s %s", g_templates[i].name,
                      g_templates[i].description);
    app_output_format(config, false, "             files: %s",
                      g_templates[i].files);
    app_output_format(config, false, "             SDK demo: %s",
                      g_templates[i].sdk_demo ? "yes" : "no");
  }
}

app_error app_cmd_templates(const app_config_t *config, int argc,
                            char *const argv[]) {
  (void)argc;
  (void)argv;
  quick_templates_print(config);
  return APP_SUCCESS;
}

static const char *quick_template_suggestion(const char *name) {
  if (!name || name[0] == '\0') {
    return "blank";
  }
  if (name[0] == 'r') {
    return "realtime";
  }
  return "blank";
}

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
  if (strcmp(template_name, "list") == 0) {
    quick_templates_print(config);
    return APP_SUCCESS;
  }
  quick_init_template_t template_kind = QUICK_INIT_TEMPLATE_BLANK;
  if (strcmp(template_name, "realtime") == 0) {
    template_kind = QUICK_INIT_TEMPLATE_REALTIME;
  } else if (strcmp(template_name, "blank") != 0) {
    char msg[192];
    snprintf(msg, sizeof(msg),
             "init --template must be blank or realtime; did you mean '%s'? "
             "Run `quick templates` or `quick init --template list`.",
             quick_template_suggestion(template_name));
    quick_print_error(config, msg);
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
  const bool first_run_unbound = !profile_arg && !profiles.default_profile;
  const bool adopt_existing = quick_cmd_flag(argc, argv, "--adopt");

  err = quick_mkdir_p_cli(dir, 0755);
  if (err != APP_SUCCESS) {
    quick_profile_config_destroy(&profiles);
    quick_print_error(config, "failed to create site directory");
    return err;
  }

  if (!adopt_existing) {
    const char *relative[] = {"index.html", "quick.json", "AGENTS.md",
                              "docs/openquick-api.md", ".quickignore"};
    bool conflict = false;
    for (size_t i = 0; i < APP_COUNTOF(relative); i++) {
      char *path = quick_path_join_cli(dir, relative[i]);
      if (path && quick_path_exists_cli(path)) {
        if (!conflict) {
          quick_print_error(config,
                            "init would overwrite existing files; pass --adopt "
                            "to write only missing OpenQuick metadata");
          conflict = true;
        }
        app_output_format(config, true, "  conflict   %s", path);
      }
      free(path);
    }
    if (conflict) {
      quick_profile_config_destroy(&profiles);
      return APP_ERROR_CONFIG_INVALID;
    }
  }

  quick_init_result_t result;
  quick_init_result_init(&result);
  quick_init_request_t request = {
      .target_dir = dir,
      .name = name_arg,
      .template_kind = template_kind,
      .profile = profile,
      .adopt_existing = adopt_existing,
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
    app_json_write_bool_field(stdout, "first_run_guidance", first_run_unbound,
                              &comma);
    app_json_write_bool_field(stdout, "adopted", adopt_existing, &comma);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
  } else {
    app_output_format(config, false, "Initialized OpenQuick site %s",
                      result.site);
    app_output_format(config, false, "  path        %s", result.path);
    app_output_format(config, false, "  profile     %s",
                      profile ? profile : "(unbound)");
    if (first_run_unbound) {
      app_output("  next        quick serve --dev", config, false);
      app_output(
          "  setup       quick serve install --profile <profile> --host "
          "<user@host> --remote-root /srv/quick --domain quick.example.com "
          "--iap tailscale",
          config, false);
    }
    app_output("  next        quick deploy --dry-run", config, false);
  }

  quick_init_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return err;
}
