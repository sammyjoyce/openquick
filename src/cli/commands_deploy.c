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

app_error app_cmd_deploy(const app_config_t *config, int argc,
                         char *const argv[]);
app_error app_cmd_serve(const app_config_t *config, int argc,
                        char *const argv[]);

static void deploy_print_json_plan(const quick_deploy_plan_t *plan,
                                   bool no_delete, bool checksum) {
  bool comma = false;
  app_json_begin_object(stdout);
  app_json_write_string_field(stdout, "format_version", "1.0", &comma);
  app_json_write_string_field(stdout, "site", plan->site, &comma);
  app_json_write_string_field(stdout, "subdomain", plan->subdomain, &comma);
  app_json_write_string_field(stdout, "profile", plan->profile, &comma);
  app_json_write_string_field(stdout, "ssh", plan->ssh, &comma);
  app_json_write_string_field(stdout, "remote_root", plan->remote_root, &comma);
  app_json_write_string_field(stdout, "output", plan->output_dir, &comma);
  app_json_write_string_field(stdout, "url", plan->url, &comma);
  app_json_write_raw_field(stdout, "dry_run", "true", &comma);
  app_json_write_raw_field(stdout, "activation", "\"quickd deploy activate\"", &comma);
  app_json_write_raw_field(stdout, "rsync", "[", &comma);
  const char *base[] = {"rsync", "-az", "--partial-dir=.rsync-partial",
                        "--safe-links", "--chmod=Dg+s,ug+rwX,o-rwx"};
  bool item = false;
  for (size_t i = 0; i < APP_COUNTOF(base); i++) {
    if (item) {
      fputc(',', stdout);
    }
    item = true;
    app_json_write_string(stdout, base[i]);
  }
  if (!no_delete) {
    fputc(',', stdout);
    app_json_write_string(stdout, "--delete");
  }
  if (checksum) {
    fputc(',', stdout);
    app_json_write_string(stdout, "--checksum");
  }
  fputc(']', stdout);
  app_json_end_object(stdout);
  app_json_end_line(stdout);
}

static void deploy_print_human_plan(const app_config_t *config,
                                    const quick_deploy_plan_t *plan,
                                    bool no_delete, bool checksum) {
  app_output_format(config, false, "quick deploy %s (dry run)", plan->site);
  app_output_format(config, false, "  profile     %s", plan->profile);
  app_output_format(config, false, "  host        %s", plan->ssh ? plan->ssh : "(none)");
  app_output_format(config, false, "  output      %s", plan->output_dir);
  app_output_format(config, false, "  url         %s", plan->url);
  app_output("  prepare     ssh <host> quickd deploy prepare --site <site> --json", config, false);
  app_output_format(config, false,
                    "  rsync       rsync -az %s%s--partial-dir=.rsync-partial --safe-links --chmod=Dg+s,ug+rwX,o-rwx <output>/ <host>:<staging>/",
                    no_delete ? "" : "--delete ", checksum ? "--checksum " : "");
  app_output("  activate    ssh <host> quickd deploy activate --site <site> --deploy-id <id> --json", config, false);
}

typedef struct {
  const app_config_t *config;
} deploy_cli_progress_t;

static void deploy_cli_progress(quick_deploy_phase_t phase,
                                quick_stream_kind_t stream, const char *line,
                                void *userdata) {
  deploy_cli_progress_t *ctx = userdata;
  if (!ctx || !line) {
    return;
  }
  if (phase != QUICK_DEPLOY_PHASE_BUILD) {
    return;
  }
  FILE *out = stream == QUICK_STREAM_STDERR ? stderr : stdout;
  if (app_config_is_quiet(ctx->config) && stream == QUICK_STREAM_STDOUT) {
    return;
  }
  fputs(line, out);
}

static bool deploy_path_is_zip(const char *path) {
  if (!path) {
    return false;
  }
  const size_t len = strlen(path);
  return len >= 4U && strcmp(path + len - 4U, ".zip") == 0;
}

static const char *deploy_profile_iap_type(
    const quick_profile_config_t *profiles, const quick_deploy_plan_t *plan) {
  const quick_profile_t *profile =
      profiles ? quick_profile_config_find(profiles, plan->profile) : NULL;
  if (profile && profile->iap.type && profile->iap.type[0] != '\0') {
    return profile->iap.type;
  }
  return "tailscale";
}

static app_error deploy_run_bootstrap_flow(
    const app_config_t *config, const quick_profile_config_t *profiles,
    const quick_deploy_plan_t *plan) {
  const char *iap = deploy_profile_iap_type(profiles, plan);
  char *argv[12];
  size_t ai = 0;
  argv[ai++] = "install";
  argv[ai++] = "--profile";
  argv[ai++] = (char *)plan->profile;
  argv[ai++] = "--host";
  argv[ai++] = plan->ssh;
  argv[ai++] = "--remote-root";
  argv[ai++] = plan->remote_root;
  if (plan->base_domain && plan->base_domain[0] != '\0') {
    argv[ai++] = "--domain";
    argv[ai++] = plan->base_domain;
  }
  argv[ai++] = "--iap";
  argv[ai++] = (char *)iap;
  argv[ai] = NULL;
  app_error err = app_cmd_serve(config, (int)ai, argv);
  if (err == APP_SUCCESS) {
    quick_print_error(
        config,
        "bootstrap guidance printed; rerun quick deploy after install completes");
    return APP_ERROR_IO;
  }
  return err;
}

static void deploy_print_success_json(const quick_deploy_plan_t *plan,
                                      const quick_deploy_result_t *result) {
  bool comma = false;
  char buf[64];
  app_json_begin_object(stdout);
  app_json_write_string_field(stdout, "format_version", "1.0", &comma);
  app_json_write_string_field(stdout, "site", plan->site, &comma);
  app_json_write_string_field(stdout, "profile", plan->profile, &comma);
  app_json_write_string_field(stdout, "release", result->release, &comma);
  app_json_write_string_field(stdout, "url", result->url, &comma);
  snprintf(buf, sizeof(buf), "%ld", result->changed);
  app_json_write_raw_field(stdout, "changed", buf, &comma);
  snprintf(buf, sizeof(buf), "%ld", result->reused);
  app_json_write_raw_field(stdout, "reused", buf, &comma);
  snprintf(buf, sizeof(buf), "%ld", result->deleted);
  app_json_write_raw_field(stdout, "deleted", buf, &comma);
  app_json_end_object(stdout);
  app_json_end_line(stdout);
}

static void deploy_print_success_human(const app_config_t *config,
                                       const quick_deploy_plan_t *plan,
                                       const quick_deploy_result_t *result) {
  app_output_format(config, false, "quick deploy %s", plan->site);
  app_output_format(config, false, "  profile     %s", plan->profile);
  app_output_format(config, false, "  host        %s", plan->ssh);
  app_output_format(config, false, "  files       %ld changed, %ld reused, %ld deleted",
                    result->changed, result->reused, result->deleted);
  app_output_format(config, false, "  release     %s", result->release);
  app_output_format(config, false, "  url         %s", result->url);
}

app_error app_cmd_deploy(const app_config_t *config, int argc,
                         char *const argv[]) {
  const char *value_opts[] = {"--site", "--subdomain", "--profile"};
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to read OpenQuick profile config");
    return err;
  }

  const char *path = quick_cmd_first_positional(argc, argv, value_opts,
                                                APP_COUNTOF(value_opts));
  quick_plan_overrides_t overrides = {
      .site = quick_cmd_value(argc, argv, "--site"),
      .subdomain = quick_cmd_value(argc, argv, "--subdomain"),
      .profile = quick_cmd_value(argc, argv, "--profile"),
      .path = path,
  };
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  err = quick_deploy_plan_resolve(&overrides, &profiles, &plan);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to resolve deploy target");
    quick_profile_config_destroy(&profiles);
    return err;
  }

  const bool dry_run = quick_cmd_flag(argc, argv, "--dry-run");
  const bool no_build = quick_cmd_flag(argc, argv, "--no-build");
  const bool no_delete = quick_cmd_flag(argc, argv, "--no-delete");
  const bool open_after = quick_cmd_flag(argc, argv, "--open");
  const bool bootstrap = quick_cmd_flag(argc, argv, "--bootstrap");
  const bool allow_unpublished = quick_cmd_flag(argc, argv, "--allow-unpublished");
  const bool checksum = quick_cmd_flag(argc, argv, "--checksum");
  const bool yes = quick_cmd_flag(argc, argv, "--yes");
  const bool zip_deploy = deploy_path_is_zip(path);

  if (dry_run) {
    if (app_config_is_json_output(config)) {
      deploy_print_json_plan(&plan, no_delete, checksum);
    } else {
      deploy_print_human_plan(config, &plan, no_delete, checksum);
    }
  } else {
    char *deployer = quick_op_default_deployer_identity();
    if (!deployer) {
      quick_deploy_plan_destroy(&plan);
      quick_profile_config_destroy(&profiles);
      return APP_ERROR_MEMORY;
    }
    quick_deploy_options_t options = {
        .no_build = no_build,
        .no_delete = no_delete,
        .checksum = checksum,
        .bootstrap = bootstrap,
        .allow_unpublished = allow_unpublished,
        .assume_yes = yes,
        .deployer = deployer,
        .ssh_key_id = getenv("QUICK_SSH_KEY_ID"),
        .ssh_principals = getenv("QUICK_SSH_PRINCIPALS"),
        .zip_path = zip_deploy ? path : NULL,
    };
    quick_deploy_result_t result;
    quick_deploy_result_init(&result);
    deploy_cli_progress_t progress = {.config = config};
    err = quick_op_deploy_execute(config, &profiles, &plan, &options,
                                  deploy_cli_progress, &progress, &result);
    if (err != APP_SUCCESS && result.overwrite_confirmation_required) {
      char msg[768];
      snprintf(msg, sizeof(msg),
               "%s\nNon-interactive deploys require --yes to overwrite this site.",
               result.failure_message ? result.failure_message
                                      : "Deploy requires overwrite confirmation.");
      if (quick_cmd_prompt_site_confirmation(config, plan.site,
                                             result.failure_message)) {
        options.overwrite_confirmed = true;
        quick_deploy_result_destroy(&result);
        quick_deploy_result_init(&result);
        err = quick_op_deploy_execute(config, &profiles, &plan, &options,
                                      deploy_cli_progress, &progress, &result);
      } else {
        quick_print_error(config, msg);
        err = APP_ERROR_VALIDATION;
      }
    }
    if (err == APP_SUCCESS) {
      if (app_config_is_json_output(config)) {
        deploy_print_success_json(&plan, &result);
      } else {
        deploy_print_success_human(config, &plan, &result);
      }
      if (open_after) {
        (void)quick_op_open_url(result.url);
      }
    } else if (result.bootstrap_missing && bootstrap) {
      if (result.failure_message) {
        quick_print_error(config, result.failure_message);
      }
      err = deploy_run_bootstrap_flow(config, &profiles, &plan);
    } else if (result.failure_message) {
      quick_print_error(config, result.failure_message);
    }
    quick_deploy_result_destroy(&result);
    free(deployer);
  }

  quick_deploy_plan_destroy(&plan);
  quick_profile_config_destroy(&profiles);
  return err;
}
