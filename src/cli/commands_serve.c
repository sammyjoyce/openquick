#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "../core/config.h"
#include "../core/deploy_plan.h"
#include "../core/error.h"
#include "../core/ops.h"
#include "../io/output.h"
#include "commands.h"
#include "commands_openquick.h"

app_error app_cmd_serve(const app_config_t *config, int argc,
                        char *const argv[]);

static app_error serve_dev(const app_config_t *config, int argc,
                           char *const argv[]) {
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    return err;
  }
  quick_serve_dev_request_t request = {
      .profiles = &profiles,
      .profile = quick_cmd_value(argc, argv, "--profile"),
      .port = quick_cmd_value(argc, argv, "--port"),
      .identity = quick_cmd_value(argc, argv, "--identity"),
      .remote_api_profile = quick_cmd_value(argc, argv, "--remote-api"),
  };
  quick_serve_dev_command_t command;
  quick_serve_dev_command_init(&command);
  err = quick_op_serve_dev_command(&request, &command);
  quick_profile_config_destroy(&profiles);
  if (err == APP_ERROR_NOT_FOUND) {
    quick_serve_dev_command_destroy(&command);
    quick_print_error(
        config, "quickd not found; set QUICK_QUICKD or install quickd on PATH");
    return err;
  }
  if (err != APP_SUCCESS) {
    quick_serve_dev_command_destroy(&command);
    return err;
  }
#ifdef _WIN32
  quick_serve_dev_command_destroy(&command);
  return APP_ERROR_FEATURE_BASE;
#else
  execvp(command.argv[0], command.argv);
  quick_serve_dev_command_destroy(&command);
  quick_print_error(config, "failed to exec quickd serve --dev");
  return APP_ERROR_IO;
#endif
}

static app_error serve_validate_install_inputs(
    const app_config_t *config, const char *profile_name, const char *host,
    const char *remote_root, const char *domain, const char *iap) {
  if (!quick_profile_name_is_safe(profile_name)) {
    quick_print_error(config, "profile contains unsafe characters");
    return APP_ERROR_VALIDATION;
  }
  if (host && !quick_ssh_target_is_safe(host)) {
    quick_print_error(config, "SSH host contains unsafe characters");
    return APP_ERROR_VALIDATION;
  }
  if (!quick_remote_path_is_safe(remote_root)) {
    quick_print_error(config,
                      "remote root must be an absolute safe path without shell "
                      "metacharacters");
    return APP_ERROR_VALIDATION;
  }
  if (domain && !quick_domain_is_safe(domain)) {
    quick_print_error(config,
                      "domain must be a DNS name without shell metacharacters");
    return APP_ERROR_VALIDATION;
  }
  if (!quick_iap_is_supported(iap)) {
    quick_print_error(
        config,
        "iap must be tailscale, tailscale-localapi, tailscale-serve, "
        "tailscale-tsnet, cloudflare, cloudflare-access, or none");
    return APP_ERROR_VALIDATION;
  }
  return APP_SUCCESS;
}

static void serve_install_progress(quick_install_phase_t phase,
                                   quick_stream_kind_t stream, const char *line,
                                   void *userdata) {
  (void)stream;
  const app_config_t *config = userdata;
  if (!line || !line[0]) {
    return;
  }
  if (app_config_is_json_output(config)) {
    return;
  }
  app_output_format(config, false, "  %-14s %s",
                    quick_install_phase_label(phase), line);
}

static app_error serve_install(const app_config_t *config, int argc,
                               char *const argv[]) {
  const char *profile_name = quick_cmd_value(argc, argv, "--profile");
  const char *host = quick_cmd_value(argc, argv, "--host");
  const char *remote_root = quick_cmd_value(argc, argv, "--remote-root");
  const char *domain = quick_cmd_value(argc, argv, "--domain");
  const char *iap_arg = quick_cmd_value(argc, argv, "--iap");
  const char *iap = iap_arg;
  const bool execute = quick_cmd_flag(argc, argv, "--execute");
  const bool unsafe = quick_cmd_flag(argc, argv, "--allow-public-unsafe");
  if (!profile_name) {
    profile_name = "default";
  }

  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    return err;
  }
  const quick_profile_t *profile =
      quick_profile_config_find(&profiles, profile_name);
  if (!host && profile && profile->ssh && profile->ssh[0]) {
    host = profile->ssh;
  }
  if (!remote_root && profile && profile->remote_root &&
      profile->remote_root[0]) {
    remote_root = profile->remote_root;
  }
  if (!domain && profile && profile->base_domain && profile->base_domain[0]) {
    domain = profile->base_domain;
  }
  if (!iap && profile && profile->iap.type && profile->iap.type[0]) {
    iap = profile->iap.type;
  }
  if (!remote_root) {
    remote_root = "/srv/quick";
  }
  if (!iap) {
    iap = "tailscale";
  }

  quick_iap_config_t install_iap = {
      .type = (char *)iap,
      .mode = (char *)quick_iap_default_mode(iap),
  };
  if (profile && !iap_arg) {
    install_iap.mode = profile->iap.mode && profile->iap.mode[0] != '\0'
                           ? profile->iap.mode
                           : (char *)quick_iap_default_mode(iap);
    install_iap.team_domain = profile->iap.team_domain;
    install_iap.audience = profile->iap.audience;
  } else if (profile && quick_iap_is_cloudflare(iap) && profile->iap.type &&
             quick_iap_is_cloudflare(profile->iap.type)) {
    install_iap.team_domain = profile->iap.team_domain;
    install_iap.audience = profile->iap.audience;
  }
  if (!quick_iap_is_cloudflare(iap)) {
    install_iap.team_domain = NULL;
    install_iap.audience = NULL;
  }
  if (strcmp(iap, "none") == 0) {
    install_iap.mode = NULL;
  }
  if (quick_iap_is_cloudflare(iap) &&
      (!install_iap.team_domain || install_iap.team_domain[0] == '\0' ||
       !install_iap.audience || install_iap.audience[0] == '\0')) {
    quick_print_error(config,
                      "iap=cloudflare requires iap.team_domain and "
                      "iap.audience in the selected profile");
    quick_profile_config_destroy(&profiles);
    return APP_ERROR_VALIDATION;
  }

  err = serve_validate_install_inputs(config, profile_name, host, remote_root,
                                      domain, iap);
  if (err != APP_SUCCESS) {
    quick_profile_config_destroy(&profiles);
    return err;
  }
  if (strcmp(iap, "none") == 0 && domain && strcmp(domain, "localhost") != 0 &&
      strcmp(domain, "127.0.0.1") != 0 && !unsafe) {
    quick_print_error(config,
                      "iap=none is only allowed for loopback unless "
                      "--allow-public-unsafe is passed");
    quick_profile_config_destroy(&profiles);
    return APP_ERROR_VALIDATION;
  }

  quick_serve_install_steps_t steps;
  quick_serve_install_steps_init(&steps);
  quick_serve_install_request_t step_request = {
      .profile = profile_name,
      .host = host,
      .remote_root = remote_root,
      .domain = domain,
      .iap = iap,
  };
  err = quick_op_serve_install_steps(&step_request, &steps);
  if (err != APP_SUCCESS) {
    quick_profile_config_destroy(&profiles);
    quick_serve_install_steps_destroy(&steps);
    return err;
  }

  if (app_config_is_json_output(config)) {
    bool comma = false;
    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "format_version", "1.0", &comma);
    app_json_write_string_field(stdout, "profile", profile_name, &comma);
    app_json_write_string_field(stdout, "host", host, &comma);
    app_json_write_string_field(stdout, "remote_root", remote_root, &comma);
    app_json_write_string_field(stdout, "domain", domain, &comma);
    app_json_write_string_field(stdout, "iap", iap, &comma);
    app_json_write_bool_field(stdout, "execute", execute, &comma);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
  } else {
    app_output("OpenQuick host install plan", config, false);
    app_output_format(config, false, "  profile       %s", profile_name);
    app_output_format(config, false, "  host          %s",
                      host ? host : "(local)");
    app_output_format(config, false, "  remote root   %s", remote_root);
    app_output_format(config, false, "  domain        %s",
                      domain ? domain : "(path fallback)");
    app_output_format(config, false, "  iap           %s", iap);
    app_output("  steps", config, false);
    for (size_t i = 0; i < steps.count; i++) {
      app_output_format(config, false, "    %zu. %s", i + 1U,
                        steps.steps[i].summary);
    }
  }
  quick_serve_install_steps_destroy(&steps);

  if (execute) {
    quick_install_request_t install_request = {
        .host = host,
        .remote_root = remote_root,
        .domain = domain,
        .iap = &install_iap,
        .non_interactive = false,
        .cancel_flag = NULL,
        .allow_public_unsafe = unsafe,
        .connect_timeout_seconds = 10,
    };
    quick_install_result_t result;
    quick_install_result_init(&result);
    err = quick_op_serve_install(&install_request, serve_install_progress,
                                 (void *)config, &result);
    if (err != APP_SUCCESS) {
      quick_print_error(config, result.failure_message ? result.failure_message
                                                       : app_strerror(err));
      if (!app_config_is_json_output(config)) {
        if (result.remediation) {
          app_output_format(config, true, "  remediation   %s",
                            result.remediation);
        }
        if (result.cancelled && result.mutation_started) {
          const quick_install_phase_t cancellation_phase =
              result.failure_phase != QUICK_INSTALL_PHASE_NONE
                  ? result.failure_phase
                  : result.last_phase;
          const char *rollback_result =
              !result.rollback_attempted
                  ? "was not attempted"
                  : (result.rollback_ok ? "succeeded" : "failed");
          app_output_format(config, true,
                            "  cancellation  after mutation during %s; "
                            "rollback %s",
                            quick_install_phase_label(cancellation_phase),
                            rollback_result);
        }
        if (result.rollback_attempted) {
          if (result.rollback_ok) {
            app_output_format(
                config, true,
                "  rollback      previous quickd/config restored; inspect "
                "backup before removing it");
          } else {
            app_output_format(
                config, true,
                "  rollback      restore failed; manual backup remains at %s",
                result.backup_path ? result.backup_path : "(host)");
          }
        }
        if (result.backup_created && result.backup_path) {
          app_output_format(config, true, "  backup        %s",
                            result.backup_path);
        }
        if (result.partial_cleanup_remains && result.cleanup_detail) {
          app_output_format(config, true, "  cleanup       WARNING: %s",
                            result.cleanup_detail);
        }
      }
      quick_install_result_destroy(&result);
      quick_profile_config_destroy(&profiles);
      return err;
    }
    if (!app_config_is_json_output(config)) {
      if (result.backup_created && result.backup_path) {
        app_output_format(config, false, "  backup        %s",
                          result.backup_path);
      }
      app_output_format(config, false, "Installed quickd on %s",
                        host ? host : "(host)");
    }
    quick_install_result_destroy(&result);
  }

  err = quick_op_serve_write_profile(profile_name, host, remote_root, domain,
                                     &install_iap);
  quick_profile_config_destroy(&profiles);
  return err;
}

app_error app_cmd_serve(const app_config_t *config, int argc,
                        char *const argv[]) {
  if (quick_cmd_flag(argc, argv, "--dev")) {
    return serve_dev(config, argc, argv);
  }
  const char *value_opts[] = {"--profile", "--host", "--remote-root",
                              "--domain",  "--iap",  "--remote-api"};
  const char *sub = quick_cmd_first_positional(argc, argv, value_opts,
                                               APP_COUNTOF(value_opts));
  if (sub && strcmp(sub, "install") == 0) {
    return serve_install(config, argc, argv);
  }
  quick_print_error(config, "serve requires --dev or install");
  return APP_ERROR_MISSING_ARG;
}
