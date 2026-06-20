#include <signal.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "../src/core/deploy_plan.h"
#include "../src/core/ops.h"
#include "../src/core/process.h"
#include "../src/core/profile_config.h"
#include "../src/core/site_config.h"
#include "../src/tui/tui_product_model.h"
#include "unit_support.h"

#ifndef _WIN32
static bool write_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  bool ok = fwrite(content, 1, strlen(content), f) == strlen(content);
  return fclose(f) == 0 && ok;
}

static bool write_executable_file(const char *path, const char *content) {
  return write_file(path, content) && chmod(path, 0755) == 0;
}

static bool file_contains(const char *path, const char *needle) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    return false;
  }
  rewind(f);
  char *buf = malloc((size_t)size + 1U);
  if (!buf) {
    fclose(f);
    return false;
  }
  size_t n = fread(buf, 1, (size_t)size, f);
  buf[n] = '\0';
  bool ok = n == (size_t)size && strstr(buf, needle) != NULL;
  free(buf);
  fclose(f);
  return ok;
}

static bool make_temp_dir(char *tmpl) {
  return mkdtemp(tmpl) != NULL;
}
#endif

static bool test_slug_normalization(void) {
  char out[QUICK_SLUG_MAX + 1];
  return quick_slug_normalize("Lunch Vote!!", out) == APP_SUCCESS &&
         strcmp(out, "lunch-vote") == 0 && quick_slug_is_valid(out) &&
         !quick_slug_is_valid("-bad") && !quick_slug_is_valid("Bad");
}

static bool test_site_config_parses_quick_json(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-site-XXXXXX";
  if (!make_temp_dir(dir)) return false;
  char path[256];
  snprintf(path, sizeof(path), "%s/quick.json", dir);
  if (!write_file(path,
                  "{\"name\":\"demo\",\"source\":\"src\",\"output\":\"dist\","
                  "\"build\":null,\"profile\":\"lab\",\"subdomain\":\"demo\","
                  "\"routing\":{\"spa_fallback\":\"/index.html\"},"
                  "\"sdk\":{\"enabled\":true,\"import\":\"/_quick/sdk.js\"}}")) {
    return false;
  }
  quick_site_config_t site;
  quick_site_config_init(&site);
  bool ok = quick_site_config_load_file(path, &site) == APP_SUCCESS &&
            site.name && strcmp(site.name, "demo") == 0 &&
            site.output && strcmp(site.output, "dist") == 0 &&
            site.profile && strcmp(site.profile, "lab") == 0 &&
            site.routing.spa_fallback &&
            strcmp(site.routing.spa_fallback, "/index.html") == 0 &&
            site.sdk.enabled;
  quick_site_config_destroy(&site);
  unlink(path);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_profile_config_parses_profiles(void) {
#ifndef _WIN32
  char path[] = "/tmp/openquick-profile-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) return false;
  FILE *f = fdopen(fd, "wb");
  if (!f) return false;
  const char *json =
      "{\"default_profile\":\"lab\",\"profiles\":{\"lab\":{"
      "\"ssh\":\"quick@box\",\"remote_root\":\"/srv/quick\","
      "\"base_domain\":\"quick.example.com\",\"base_url\":\"https://quick.example.com\","
      "\"iap\":{\"type\":\"tailscale\",\"mode\":\"localapi\"},"
      "\"deploy\":{\"delete\":true,\"open_after_deploy\":false}}}}";
  bool wrote = fwrite(json, 1, strlen(json), f) == strlen(json);
  fclose(f);
  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  bool ok = wrote && quick_profile_config_load_file(path, &cfg) == APP_SUCCESS &&
            cfg.default_profile && strcmp(cfg.default_profile, "lab") == 0;
  const quick_profile_t *p = quick_profile_config_find(&cfg, "lab");
  ok = ok && p && p->ssh && strcmp(p->ssh, "quick@box") == 0 &&
       p->base_domain && strcmp(p->base_domain, "quick.example.com") == 0 &&
       p->iap.type && strcmp(p->iap.type, "tailscale") == 0 &&
       p->deploy.has_delete && p->deploy.delete;
  quick_profile_config_destroy(&cfg);
  unlink(path);
  return ok;
#else
  return true;
#endif
}

static bool test_target_resolution_precedence(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-plan-XXXXXX";
  if (!make_temp_dir(dir)) return false;
  char qpath[256];
  snprintf(qpath, sizeof(qpath), "%s/quick.json", dir);
  if (!write_file(qpath,
                  "{\"name\":\"from-file\",\"source\":\".\",\"output\":\".\",\"profile\":\"lab\"}")) {
    return false;
  }
  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  cfg.default_profile = strdup("lab");
  quick_profile_t *p = quick_profile_config_upsert(&cfg, "lab");
  if (!p) return false;
  p->ssh = strdup("quick@box");
  p->remote_root = strdup("/srv/quick");
  p->base_domain = strdup("quick.example.com");
  setenv("QUICK_SITE", "from-env", 1);
  quick_plan_overrides_t overrides = {.path = dir, .site = "from-flag"};
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  bool ok = quick_deploy_plan_resolve(&overrides, &cfg, &plan) == APP_SUCCESS &&
            strcmp(plan.site, "from-flag") == 0 &&
            strcmp(plan.profile, "lab") == 0 && plan.ssh &&
            strcmp(plan.ssh, "quick@box") == 0 && plan.url &&
            strcmp(plan.url, "https://from-flag.quick.example.com") == 0;
  unsetenv("QUICK_SITE");
  quick_deploy_plan_destroy(&plan);
  quick_profile_config_destroy(&cfg);
  unlink(qpath);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_safe_remote_install_validation(void) {
  return quick_profile_name_is_safe("lab.default-1") &&
         !quick_profile_name_is_safe("lab;rm") &&
         quick_ssh_target_is_safe("quick@box.example.com") &&
         quick_ssh_target_is_safe("quick@[fd7a:115c:a1e0::1]") == false &&
         !quick_ssh_target_is_safe("quick@box;touch-x") &&
         quick_remote_path_is_safe("/srv/quick-sites_1") &&
         !quick_remote_path_is_safe("srv/quick") &&
         !quick_remote_path_is_safe("/srv/quick bad") &&
         !quick_remote_path_is_safe("/srv/../etc") &&
         quick_domain_is_safe("quick.example.com") &&
         quick_domain_is_safe("localhost") &&
         !quick_domain_is_safe("quick.example.com;rm") &&
         !quick_domain_is_safe("-bad.example.com");
}

static bool test_quickignore_to_rsync_args(void) {
#ifndef _WIN32
  char path[] = "/tmp/openquick-ignore-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) return false;
  FILE *f = fdopen(fd, "wb");
  if (!f) return false;
  const char *body = "# comment\n.git/\nnode_modules/\n\n.env.*\n";
  bool wrote = fwrite(body, 1, strlen(body), f) == strlen(body);
  fclose(f);
  quick_ignore_t ignore;
  quick_ignore_init(&ignore);
  bool ok = wrote && quick_ignore_load_file(path, &ignore) == APP_SUCCESS &&
            ignore.count == 3;
  size_t argc = 0;
  char **args = quick_ignore_to_rsync_args(&ignore, &argc);
  ok = ok && args && argc == 3 && strcmp(args[0], "--exclude=.git/") == 0 &&
       strcmp(args[2], "--exclude=.env.*") == 0;
  quick_ignore_args_destroy(args, argc);
  quick_ignore_destroy(&ignore);
  unlink(path);
  return ok;
#else
  return true;
#endif
}

typedef struct {
  int stdout_lines;
  int stderr_lines;
  char joined[128];
  volatile sig_atomic_t *cancel;
} stream_test_ctx_t;

static void test_stream_cb(quick_stream_kind_t kind, const char *line,
                           void *userdata) {
  stream_test_ctx_t *ctx = userdata;
  if (kind == QUICK_STREAM_STDOUT) {
    ctx->stdout_lines++;
  } else {
    ctx->stderr_lines++;
  }
  strncat(ctx->joined, line,
          sizeof(ctx->joined) - strlen(ctx->joined) - 1U);
  if (ctx->cancel) {
    *ctx->cancel = 1;
  }
}

static bool test_process_stream_callbacks(void) {
#ifndef _WIN32
  char *const argv[] = {"/usr/bin/printf", "one\\ntwo\\n", NULL};
  quick_process_result_t res = {0};
  stream_test_ctx_t ctx = {0};
  app_error err = quick_process_stream(argv, NULL, NULL, test_stream_cb, &ctx,
                                       &res);
  bool ok = err == APP_SUCCESS && res.exit_code == 0 && ctx.stdout_lines == 2 &&
            ctx.stderr_lines == 0 && strcmp(ctx.joined, "one\ntwo\n") == 0 &&
            res.out && strcmp(res.out, "one\ntwo\n") == 0;
  quick_process_result_destroy(&res);
  return ok;
#else
  return true;
#endif
}

static bool test_process_stream_cancellation(void) {
#ifndef _WIN32
  char *const argv[] = {"/bin/sleep", "2", NULL};
  quick_process_result_t res = {0};
  volatile sig_atomic_t cancel = 1;
  app_error err = quick_process_stream_cancelable(argv, NULL, NULL, NULL, NULL,
                                                 &cancel, &res);
  bool ok = err == APP_ERROR_INTERRUPTED;
  quick_process_result_destroy(&res);
  return ok;
#else
  return true;
#endif
}

static bool test_init_op_scaffolds_temp_dir(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-init-op-XXXXXX";
  if (!make_temp_dir(dir)) return false;
  char index_path[256];
  char quick_path[256];
  char agents[256], ignore[256], docs[256], api[256];
  snprintf(index_path, sizeof(index_path), "%s/index.html", dir);
  snprintf(quick_path, sizeof(quick_path), "%s/quick.json", dir);
  snprintf(agents, sizeof(agents), "%s/AGENTS.md", dir);
  snprintf(ignore, sizeof(ignore), "%s/.quickignore", dir);
  snprintf(docs, sizeof(docs), "%s/docs", dir);
  snprintf(api, sizeof(api), "%s/docs/openquick-api.md", dir);
  quick_init_result_t result;
  quick_init_result_init(&result);
  quick_init_request_t req = {.target_dir = dir,
                              .name = "Lunch Vote",
                              .template_kind = QUICK_INIT_TEMPLATE_BLANK,
                              .profile = "lab"};
  bool ok = quick_op_init(&req, &result) == APP_SUCCESS &&
            result.site && strcmp(result.site, "lunch-vote") == 0 &&
            result.file_count == 5 && access(index_path, F_OK) == 0 &&
            access(quick_path, F_OK) == 0 &&
            file_contains(agents, "OpenQuick site agent guide") &&
            file_contains(agents, "const caps = await quick.capabilities(); if (caps.ai)") &&
            file_contains(api, "quick.warehouse.query(name, params)");
  quick_init_result_destroy(&result);
  unlink(index_path);
  unlink(quick_path);
  unlink(agents);
  unlink(ignore);
  unlink(api);
  rmdir(docs);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_deploy_rsync_count_parser(void) {
  long changed = -1, reused = -1, deleted = -1;
  quick_op_deploy_parse_rsync_counts(">f..t file\ncd++++ dir\n*deleting old\n", &changed,
                                     &reused, &deleted);
  quick_deploy_options_t opts = {.no_build = true,
                                 .no_delete = true,
                                 .checksum = true,
                                 .bootstrap = false,
                                 .allow_unpublished = true};
  return changed == 2 && reused == 0 && deleted == 1 && opts.no_build &&
         opts.no_delete && opts.checksum && opts.allow_unpublished;
}

static bool test_deploy_publication_gate_marks_result(void) {
#ifndef _WIN32
  char site_dir[] = "/tmp/openquick-publication-unit-site-XXXXXX";
  char bin_dir[] = "/tmp/openquick-publication-unit-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const bool had_path = old_path_env != NULL;
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  if (had_path && !old_path) return false;

  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_deploy_result_t result;
  quick_deploy_result_init(&result);
  bool ok = false;

  if (!make_temp_dir(site_dir) || !make_temp_dir(bin_dir)) {
    goto cleanup;
  }
  char ssh_path[256];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  if (!write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = doctor ]; then\n"
          "  printf '%s\\n' '{\"format_version\":\"1.0\",\"ok\":true,\"checks\":[{\"name\":\"domain\",\"group\":\"edge/iap\",\"status\":\"warn\",\"detail\":\"missing\",\"remediation\":\"configure\"}]}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n")) {
    goto cleanup;
  }
  setenv("PATH", bin_dir, 1);

  plan.site_root = strdup(site_dir);
  plan.output_dir = strdup(site_dir);
  plan.ssh = strdup("quick@box");
  plan.remote_root = strdup("/srv/quick");
  plan.base_domain = strdup("quick.example.com");
  snprintf(plan.site, sizeof(plan.site), "demo");
  snprintf(plan.subdomain, sizeof(plan.subdomain), "demo");
  snprintf(plan.profile, sizeof(plan.profile), "lab");
  if (!plan.site_root || !plan.output_dir || !plan.ssh || !plan.remote_root ||
      !plan.base_domain) {
    goto cleanup;
  }

  quick_deploy_options_t options = {.no_build = true,
                                    .allow_unpublished = false};
  app_error err = quick_op_deploy_execute(NULL, &profiles, &plan, &options,
                                          NULL, NULL, &result);
  ok = err == APP_ERROR_VALIDATION && result.publication_issue &&
       !result.bootstrap_missing &&
       result.failure_phase == QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK &&
       result.failure_message && strstr(result.failure_message,
                                        "--allow-unpublished") != NULL;

cleanup:
  if (had_path) {
    setenv("PATH", old_path, 1);
  } else {
    unsetenv("PATH");
  }
  free(old_path);
  quick_deploy_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  quick_deploy_plan_destroy(&plan);
  char ssh_path_cleanup[256];
  snprintf(ssh_path_cleanup, sizeof(ssh_path_cleanup), "%s/ssh", bin_dir);
  unlink(ssh_path_cleanup);
  rmdir(bin_dir);
  rmdir(site_dir);
  return ok;
#else
  return true;
#endif
}

static bool test_list_op_reads_local_record(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-list-op-XXXXXX";
  if (!make_temp_dir(dir)) return false;
  char qpath[256];
  snprintf(qpath, sizeof(qpath), "%s/quick.json", dir);
  if (!write_file(qpath,
                  "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\",\"profile\":\"lab\"}")) {
    return false;
  }
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  profiles.default_profile = strdup("lab");
  (void)quick_local_state_write_deployment(dir, "lab", "demo",
                                           "https://demo.quick.example.com",
                                           "rel1");
  quick_list_result_t result;
  quick_list_result_init(&result);
  quick_list_request_t req = {.profiles = &profiles,
                              .overrides = {.path = dir, .profile = "lab"},
                              .remote = false};
  bool ok = quick_op_list(&req, &result) == APP_SUCCESS && result.count == 1 &&
            result.items[0].source == QUICK_LIST_SOURCE_LOCAL &&
            strcmp(result.items[0].name, "demo") == 0;
  quick_list_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  char depdir[256], depfile[256], quickdir[256];
  snprintf(quickdir, sizeof(quickdir), "%s/.quick", dir);
  snprintf(depdir, sizeof(depdir), "%s/.quick/deployments", dir);
  snprintf(depfile, sizeof(depfile), "%s/.quick/deployments/lab.json", dir);
  unlink(depfile);
  rmdir(depdir);
  rmdir(quickdir);
  unlink(qpath);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_delete_op_reports_confirmation_metadata(void) {
#ifndef _WIN32
  char bin_dir[] = "/tmp/openquick-delete-unit-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const char *old_remote_env = getenv("QUICK_REMOTE");
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  char *old_remote = old_remote_env ? strdup(old_remote_env) : NULL;
  if ((old_path_env && !old_path) || (old_remote_env && !old_remote)) return false;
  bool ok = false;
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_delete_result_t result;
  quick_delete_result_init(&result);
  if (!make_temp_dir(bin_dir)) goto cleanup;
  char ssh_path[256];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  if (!write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = sites ] && [ \"$4\" = get ]; then\n"
          "  printf '%s\\n' '{\"format_version\":\"1.0\",\"name\":\"demo\",\"subdomain\":\"alias\",\"url\":\"https://alias.example.com\",\"deployer\":\"bob\",\"public\":true}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n")) {
    goto cleanup;
  }
  setenv("PATH", bin_dir, 1);
  setenv("QUICK_REMOTE", "quick@box", 1);
  quick_delete_request_t req = {.profiles = &profiles, .site = "demo"};
  ok = quick_op_delete(&req, &result) == APP_SUCCESS &&
       result.confirmation_required && result.site.name &&
       strcmp(result.site.name, "demo") == 0 && result.site.subdomain &&
       strcmp(result.site.subdomain, "alias") == 0 && result.site.have_public &&
       result.site.is_public;
cleanup:
  if (old_path_env) setenv("PATH", old_path, 1); else unsetenv("PATH");
  if (old_remote_env) setenv("QUICK_REMOTE", old_remote, 1); else unsetenv("QUICK_REMOTE");
  free(old_path);
  free(old_remote);
  quick_delete_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  char ssh_cleanup[256];
  snprintf(ssh_cleanup, sizeof(ssh_cleanup), "%s/ssh", bin_dir);
  unlink(ssh_cleanup);
  rmdir(bin_dir);
  return ok;
#else
  return true;
#endif
}

static bool test_public_op_parses_status(void) {
#ifndef _WIN32
  char bin_dir[] = "/tmp/openquick-public-unit-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const char *old_remote_env = getenv("QUICK_REMOTE");
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  char *old_remote = old_remote_env ? strdup(old_remote_env) : NULL;
  if ((old_path_env && !old_path) || (old_remote_env && !old_remote)) return false;
  bool ok = false;
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_public_result_t result;
  quick_public_result_init(&result);
  if (!make_temp_dir(bin_dir)) goto cleanup;
  char ssh_path[256];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  if (!write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = sites ] && [ \"$4\" = get ]; then\n"
          "  printf '%s\\n' '{\"format_version\":\"1.0\",\"name\":\"demo\",\"public\":false}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n")) {
    goto cleanup;
  }
  setenv("PATH", bin_dir, 1);
  setenv("QUICK_REMOTE", "quick@box", 1);
  quick_public_request_t req = {.profiles = &profiles,
                                .site = "demo",
                                .action = QUICK_PUBLIC_STATUS};
  ok = quick_op_public(&req, &result) == APP_SUCCESS && result.have_public &&
       !result.is_public && result.site.name &&
       strcmp(result.site.name, "demo") == 0;
cleanup:
  if (old_path_env) setenv("PATH", old_path, 1); else unsetenv("PATH");
  if (old_remote_env) setenv("QUICK_REMOTE", old_remote, 1); else unsetenv("QUICK_REMOTE");
  free(old_path);
  free(old_remote);
  quick_public_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  char ssh_cleanup[256];
  snprintf(ssh_cleanup, sizeof(ssh_cleanup), "%s/ssh", bin_dir);
  unlink(ssh_cleanup);
  rmdir(bin_dir);
  return ok;
#else
  return true;
#endif
}

static bool test_domain_op_rejects_invalid_domain(void) {
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_domain_result_t result;
  quick_domain_result_init(&result);
  quick_domain_request_t req = {.profiles = &profiles,
                                .site = "demo",
                                .domain = "bad;rm",
                                .action = QUICK_DOMAIN_ADD};
  bool ok = quick_op_domain(&req, &result) == APP_ERROR_VALIDATION;
  quick_domain_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return ok;
}

static bool test_doctor_op_returns_structured_checks(void) {
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_doctor_result_t result;
  quick_doctor_result_init(&result);
  quick_doctor_request_t req = {.profiles = &profiles};
  bool ok = quick_op_doctor(&req, &result) == APP_SUCCESS && result.count > 0 &&
            result.checks[0].name && strcmp(result.checks[0].name, "quick_version") == 0;
  quick_doctor_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return ok;
}

#ifndef _WIN32
static bool check_doctor_identity_response(const char *identity_body,
                                           quick_doctor_status_t expected_status,
                                           bool expected_result_ok,
                                           const char *detail_needle) {
  char bin_dir[] = "/tmp/openquick-doctor-identity-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const bool had_path = old_path_env != NULL;
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  if (had_path && !old_path) return false;

  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_doctor_result_t result;
  quick_doctor_result_init(&result);
  bool ok = false;

  if (!make_temp_dir(bin_dir)) {
    goto cleanup;
  }
  char rsync_path[256];
  char ssh_path[256];
  char curl_path[256];
  snprintf(rsync_path, sizeof(rsync_path), "%s/rsync", bin_dir);
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(curl_path, sizeof(curl_path), "%s/curl", bin_dir);

  char curl_script[1024];
  int n = snprintf(
      curl_script, sizeof(curl_script),
      "#!/bin/sh\n"
      "case \"$4\" in\n"
      "  */_quick/health) printf '%%s\\n' '{\"format_version\":\"1.0\",\"ok\":true}' ; exit 0 ;;\n"
      "  */_quick/identity) printf '%%s\\n' '%s' ; exit 0 ;;\n"
      "esac\n"
      "exit 1\n",
      identity_body);
  if (n < 0 || (size_t)n >= sizeof(curl_script)) {
    goto cleanup;
  }

  if (!write_executable_file(rsync_path, "#!/bin/sh\nexit 0\n") ||
      !write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = doctor ]; then\n"
          "  printf '%s\\n' '{\"format_version\":\"1.0\",\"ok\":true}'\n"
          "  exit 0\n"
          "fi\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = admin ]; then\n"
          "  printf '%s\\n' '{\"sites\":1,\"releases\":2}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n") ||
      !write_executable_file(curl_path, curl_script)) {
    goto cleanup;
  }
  setenv("PATH", bin_dir, 1);

  profiles.default_profile = strdup("lab");
  quick_profile_t *p = quick_profile_config_upsert(&profiles, "lab");
  if (!profiles.default_profile || !p) {
    goto cleanup;
  }
  p->ssh = strdup("quick@box");
  p->remote_root = strdup("/srv/quick");
  p->base_url = strdup("https://quick.example.com/~/");
  if (!p->ssh || !p->remote_root || !p->base_url) {
    goto cleanup;
  }

  quick_doctor_request_t req = {.profiles = &profiles,
                                .profile = "lab",
                                .site = "demo",
                                .remote = true};
  if (quick_op_doctor(&req, &result) != APP_SUCCESS ||
      result.ok != expected_result_ok) {
    goto cleanup;
  }
  for (size_t i = 0; i < result.count; i++) {
    if (result.checks[i].name && strcmp(result.checks[i].name, "http_identity") == 0) {
      ok = result.checks[i].status == expected_status &&
           (!detail_needle || (result.checks[i].detail &&
                               strstr(result.checks[i].detail, detail_needle) != NULL));
      break;
    }
  }

cleanup:
  if (had_path) {
    setenv("PATH", old_path, 1);
  } else {
    unsetenv("PATH");
  }
  free(old_path);
  quick_doctor_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  if (bin_dir[0]) {
    char cleanup_path[256];
    snprintf(cleanup_path, sizeof(cleanup_path), "%s/rsync", bin_dir);
    unlink(cleanup_path);
    snprintf(cleanup_path, sizeof(cleanup_path), "%s/ssh", bin_dir);
    unlink(cleanup_path);
    snprintf(cleanup_path, sizeof(cleanup_path), "%s/curl", bin_dir);
    unlink(cleanup_path);
    rmdir(bin_dir);
  }
  return ok;
}
#endif

static bool test_doctor_private_identity_redirect_is_warning(void) {
#ifndef _WIN32
  return check_doctor_identity_response(
      "<a href=\"/__exe.dev/login\">Temporary Redirect</a>",
      QUICK_DOCTOR_STATUS_WARN, true, "login/redirect");
#else
  return true;
#endif
}

static bool test_doctor_malformed_identity_login_field_is_failure(void) {
#ifndef _WIN32
  return check_doctor_identity_response(
      "{\"authenticated\":true,\"login\":\"sam\"}", QUICK_DOCTOR_STATUS_FAIL,
      false, "identity JSON missing");
#else
  return true;
#endif
}

static bool test_tui_product_model_helpers(void) {
  quick_list_item_t item = {.name = "demo",
                            .url = "https://demo.quick.example.com",
                            .release = "rel1",
                            .updated_at = "2026-06-12T00:00:00Z",
                            .deployer = "sam",
                            .stale = true,
                            .source = QUICK_LIST_SOURCE_LOCAL};
  char row[256];
  char message[160];
  return quick_tui_format_site_row(&item, row, sizeof(row)) &&
         strstr(row, "demo - https://demo.quick.example.com") != NULL &&
         strstr(row, "[stale]") != NULL &&
         quick_tui_validate_profile_field("ssh", "quick@box", message,
                                          sizeof(message)) &&
         !quick_tui_validate_profile_field("ssh", "quick@box;rm", message,
                                           sizeof(message)) &&
         strcmp(quick_tui_deploy_phase_label(QUICK_DEPLOY_PHASE_TRANSFER),
                "transfer") == 0;
}

static bool test_mint_dev_token_parses_json(void) {
#ifndef _WIN32
  char bin_dir[] = "/tmp/openquick-dev-token-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const char *old_remote_env = getenv("QUICK_REMOTE");
  const char *old_base_domain_env = getenv("QUICK_BASE_DOMAIN");
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  char *old_remote = old_remote_env ? strdup(old_remote_env) : NULL;
  char *old_base_domain = old_base_domain_env ? strdup(old_base_domain_env) : NULL;
  if ((old_path_env && !old_path) || (old_remote_env && !old_remote) ||
      (old_base_domain_env && !old_base_domain)) {
    free(old_path);
    free(old_remote);
    free(old_base_domain);
    return false;
  }

  bool ok = false;
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_dev_token_result_t result;
  quick_dev_token_result_init(&result);

  if (!make_temp_dir(bin_dir)) goto cleanup;
  char ssh_path[256];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  if (!write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$1\" = quick@box ] && [ \"$2\" = quickd ] && "
          "[ \"$3\" = admin ] && [ \"$4\" = mint-dev-token ] && "
          "[ \"$5\" = --site ] && [ \"$6\" = demo ] && "
          "[ \"$7\" = --ttl ] && [ \"$8\" = 3600 ] && "
          "[ \"$9\" = --json ]; then\n"
          "  printf '%s\\n' '{\"token\":\"dev-token-123\",\"site\":\"demo\",\"expires_at\":\"2026-06-12T01:00:00Z\"}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n")) {
    goto cleanup;
  }
  setenv("PATH", bin_dir, 1);
  setenv("QUICK_REMOTE", "quick@box", 1);
  setenv("QUICK_BASE_DOMAIN", "quick.example.com", 1);

  quick_dev_token_request_t req = {.profiles = &profiles,
                                   .profile = "lab",
                                   .site = "demo",
                                   .ttl_seconds = 3600};
  ok = quick_op_mint_dev_token(&req, &result) == APP_SUCCESS &&
       result.token && strcmp(result.token, "dev-token-123") == 0 &&
       result.expires_at &&
       strcmp(result.expires_at, "2026-06-12T01:00:00Z") == 0 &&
       result.site && strcmp(result.site, "demo") == 0 && result.url &&
       strcmp(result.url, "https://demo.quick.example.com") == 0;

cleanup:
  if (old_path_env) setenv("PATH", old_path, 1); else unsetenv("PATH");
  if (old_remote_env) setenv("QUICK_REMOTE", old_remote, 1); else unsetenv("QUICK_REMOTE");
  if (old_base_domain_env) setenv("QUICK_BASE_DOMAIN", old_base_domain, 1); else unsetenv("QUICK_BASE_DOMAIN");
  free(old_path);
  free(old_remote);
  free(old_base_domain);
  quick_dev_token_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  char ssh_cleanup[256];
  snprintf(ssh_cleanup, sizeof(ssh_cleanup), "%s/ssh", bin_dir);
  unlink(ssh_cleanup);
  rmdir(bin_dir);
  return ok;
#else
  return true;
#endif
}

static bool test_serve_install_steps_structure(void) {
  quick_serve_install_steps_t steps;
  quick_serve_install_steps_init(&steps);
  quick_serve_install_request_t req = {.profile = "lab",
                                       .host = "quick@box",
                                       .remote_root = "/srv/quick",
                                       .domain = "quick.example.com",
                                       .iap = "tailscale"};
  bool ok = quick_op_serve_install_steps(&req, &steps) == APP_SUCCESS &&
            steps.count == 6 && steps.steps[0].summary &&
            strcmp(steps.steps[0].summary,
                   "create quick user and quick-deploy group") == 0 &&
            strstr(steps.steps[5].summary, "quickd doctor") != NULL;
  quick_serve_install_steps_destroy(&steps);
  return ok;
}

void run_openquick_unit_tests(unit_stats_t *stats) {
  unit_record(stats, test_slug_normalization(),
              "OpenQuick slug normalization validates DNS labels");
  unit_record(stats, test_site_config_parses_quick_json(),
              "quick.json parser reads site settings");
  unit_record(stats, test_profile_config_parses_profiles(),
              "profile config parser reads host profile settings");
  unit_record(stats, test_target_resolution_precedence(),
              "deploy plan resolution honors flag over env over quick.json");
  unit_record(stats, test_safe_remote_install_validation(),
              "remote installer validation rejects unsafe argv-over-ssh values");
  unit_record(stats, test_quickignore_to_rsync_args(),
              ".quickignore patterns become rsync --exclude args");
  unit_record(stats, test_process_stream_callbacks(),
              "process streaming reports stdout lines and captures output");
  unit_record(stats, test_process_stream_cancellation(),
              "process streaming cancellation interrupts child process");
  unit_record(stats, test_init_op_scaffolds_temp_dir(),
              "init op scaffolds a site into a temp directory");
  unit_record(stats, test_deploy_rsync_count_parser(),
              "deploy op parses rsync counts and carries options");
  unit_record(stats, test_deploy_publication_gate_marks_result(),
              "deploy op marks publication gate failures");
  unit_record(stats, test_list_op_reads_local_record(),
              "list op returns structured local deployment records");
  unit_record(stats, test_delete_op_reports_confirmation_metadata(),
              "delete op fetches site metadata before confirmation");
  unit_record(stats, test_public_op_parses_status(),
              "public op parses remote public status");
  unit_record(stats, test_domain_op_rejects_invalid_domain(),
              "domain op rejects unsafe domain values");
  unit_record(stats, test_doctor_op_returns_structured_checks(),
              "doctor op returns structured checks");
  unit_record(stats, test_doctor_private_identity_redirect_is_warning(),
              "doctor treats private identity redirects as warning");
  unit_record(stats, test_doctor_malformed_identity_login_field_is_failure(),
              "doctor fails malformed identity JSON with login field");
  unit_record(stats, test_tui_product_model_helpers(),
              "TUI product model formats rows and validates profile fields");
  unit_record(stats, test_mint_dev_token_parses_json(),
              "serve dev op parses minted remote API token JSON");
  unit_record(stats, test_serve_install_steps_structure(),
              "serve op exposes guided install steps");
}
