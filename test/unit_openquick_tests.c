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
#include "../src/tui/tui_onboarding_model.h"
#include "../src/tui/tui_product_model.h"
#include "unit_support.h"

#ifndef _WIN32
static bool write_file(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return false;
  bool ok = fwrite(content, 1, strlen(content), f) == strlen(content);
  return fclose(f) == 0 && ok;
}

static bool write_executable_file(const char *path, const char *content) {
  return write_file(path, content) && chmod(path, 0755) == 0;
}

static bool file_contains(const char *path, const char *needle) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
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
  if (!make_temp_dir(dir))
    return false;
  char path[256];
  snprintf(path, sizeof(path), "%s/quick.json", dir);
  if (!write_file(
          path,
          "{\"name\":\"demo\",\"source\":\"src\",\"output\":\"dist\","
          "\"build\":null,\"profile\":\"lab\",\"subdomain\":\"demo\","
          "\"routing\":{\"spa_fallback\":\"/index.html\"},"
          "\"sdk\":{\"enabled\":true,\"import\":\"/_quick/sdk.js\"}}")) {
    return false;
  }
  quick_site_config_t site;
  quick_site_config_init(&site);
  bool ok = quick_site_config_load_file(path, &site) == APP_SUCCESS &&
            site.name && strcmp(site.name, "demo") == 0 && site.output &&
            strcmp(site.output, "dist") == 0 && site.profile &&
            strcmp(site.profile, "lab") == 0 && site.routing.spa_fallback &&
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
  if (fd < 0)
    return false;
  FILE *f = fdopen(fd, "wb");
  if (!f)
    return false;
  const char *json =
      "{\"default_profile\":\"lab\",\"profiles\":{\"lab\":{"
      "\"ssh\":\"quick@box\",\"remote_root\":\"/srv/quick\","
      "\"base_domain\":\"quick.example.com\",\"base_url\":\"https://"
      "quick.example.com\","
      "\"iap\":{\"type\":\"tailscale\",\"mode\":\"localapi\"},"
      "\"deploy\":{\"delete\":true,\"open_after_deploy\":false}}}}";
  bool wrote = fwrite(json, 1, strlen(json), f) == strlen(json);
  fclose(f);
  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  bool ok = wrote &&
            quick_profile_config_load_file(path, &cfg) == APP_SUCCESS &&
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
  if (!make_temp_dir(dir))
    return false;
  char qpath[256];
  snprintf(qpath, sizeof(qpath), "%s/quick.json", dir);
  if (!write_file(qpath,
                  "{\"name\":\"from-file\",\"source\":\".\",\"output\":\".\","
                  "\"profile\":\"lab\"}")) {
    return false;
  }
  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  cfg.default_profile = strdup("lab");
  quick_profile_t *p = quick_profile_config_upsert(&cfg, "lab");
  if (!p)
    return false;
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

static bool test_domain_is_loopback(void) {
  return quick_domain_is_loopback("localhost") &&
         quick_domain_is_loopback("127.0.0.1") &&
         !quick_domain_is_loopback(NULL) && !quick_domain_is_loopback("") &&
         !quick_domain_is_loopback("LOCALHOST") &&
         !quick_domain_is_loopback("127.0.0.2") &&
         !quick_domain_is_loopback("example.com");
}

static bool test_restore_and_rollback_input_validation(void) {
  char too_long_release[130];
  memset(too_long_release, 'a', sizeof(too_long_release) - 1U);
  too_long_release[sizeof(too_long_release) - 1U] = '\0';
  return quick_restore_archive_path_is_safe(
             "/srv/quick/.trash/sites/demo-20260612T000000.000000000Z",
             "/srv/quick", "demo") &&
         quick_restore_archive_path_is_safe(
             "/srv/quick/.trash/sites/my-site-20260612T000000.000000000Z", NULL,
             "my-site") &&
         !quick_restore_archive_path_is_safe(
             "/srv/quick/sites/demo-20260612T000000.000000000Z", "/srv/quick",
             "demo") &&
         !quick_restore_archive_path_is_safe(
             "/srv/other/.trash/sites/demo-20260612T000000.000000000Z",
             "/srv/quick", "demo") &&
         !quick_restore_archive_path_is_safe(
             "/srv/quick/.trash/sites/demo-20260612T000000Z-abcdef",
             "/srv/quick", "demo") &&
         !quick_restore_archive_path_is_safe(
             "/srv/quick/.trash/sites/other-20260612T000000.000000000Z",
             "/srv/quick", "demo") &&
         !quick_restore_archive_path_is_safe(
             "/srv/quick/.trash/sites/demo-20260612T000000.000000000Z/site",
             "/srv/quick", "demo") &&
         quick_release_id_is_safe("20260612T000000Z-abcdef") &&
         quick_release_id_is_safe("rel_1.2-ABC") &&
         !quick_release_id_is_safe("-rel") &&
         !quick_release_id_is_safe("../rel") &&
         !quick_release_id_is_safe("rel;rm") && !quick_release_id_is_safe("") &&
         !quick_release_id_is_safe(NULL) &&
         !quick_release_id_is_safe(too_long_release);
}

static bool test_quickignore_to_rsync_args(void) {
#ifndef _WIN32
  char path[] = "/tmp/openquick-ignore-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0)
    return false;
  FILE *f = fdopen(fd, "wb");
  if (!f)
    return false;
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
  strncat(ctx->joined, line, sizeof(ctx->joined) - strlen(ctx->joined) - 1U);
  if (ctx->cancel) {
    *ctx->cancel = 1;
  }
}

static bool test_process_stream_callbacks(void) {
#ifndef _WIN32
  char *const argv[] = {"/usr/bin/printf", "one\\ntwo\\n", NULL};
  quick_process_result_t res = {0};
  stream_test_ctx_t ctx = {0};
  app_error err =
      quick_process_stream(argv, NULL, NULL, test_stream_cb, &ctx, &res);
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
  if (!make_temp_dir(dir))
    return false;
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
  bool ok =
      quick_op_init(&req, &result) == APP_SUCCESS && result.site &&
      strcmp(result.site, "lunch-vote") == 0 && result.file_count == 5 &&
      access(index_path, F_OK) == 0 && access(quick_path, F_OK) == 0 &&
      file_contains(agents, "OpenQuick site agent guide") &&
      file_contains(agents,
                    "const caps = await quick.capabilities(); if (caps.ai)") &&
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
  quick_op_deploy_parse_rsync_counts(">f..t file\ncd++++ dir\n*deleting old\n",
                                     &changed, &reused, &deleted);
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
  if (had_path && !old_path)
    return false;

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
          "  printf '%s\\n' "
          "'{\"format_version\":\"1.0\",\"ok\":true,\"checks\":[{\"name\":"
          "\"domain\",\"group\":\"edge/"
          "iap\",\"status\":\"warn\",\"detail\":\"missing\",\"remediation\":"
          "\"configure\"}]}'\n"
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
       result.failure_message &&
       strstr(result.failure_message, "--allow-unpublished") != NULL;

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
  if (!make_temp_dir(dir))
    return false;
  char qpath[256];
  snprintf(qpath, sizeof(qpath), "%s/quick.json", dir);
  if (!write_file(qpath,
                  "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\","
                  "\"profile\":\"lab\"}")) {
    return false;
  }
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  profiles.default_profile = strdup("lab");
  (void)quick_local_state_write_deployment(
      dir, "lab", "demo", "https://demo.quick.example.com", "rel1");
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
  if ((old_path_env && !old_path) || (old_remote_env && !old_remote))
    return false;
  bool ok = false;
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_delete_result_t result;
  quick_delete_result_init(&result);
  if (!make_temp_dir(bin_dir))
    goto cleanup;
  char ssh_path[256];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  if (!write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = sites ] && [ \"$4\" = get ]; "
          "then\n"
          "  printf '%s\\n' "
          "'{\"format_version\":\"1.0\",\"name\":\"demo\",\"subdomain\":"
          "\"alias\",\"url\":\"https://"
          "alias.example.com\",\"deployer\":\"bob\",\"public\":true}'\n"
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
  if (old_path_env)
    setenv("PATH", old_path, 1);
  else
    unsetenv("PATH");
  if (old_remote_env)
    setenv("QUICK_REMOTE", old_remote, 1);
  else
    unsetenv("QUICK_REMOTE");
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
  if ((old_path_env && !old_path) || (old_remote_env && !old_remote))
    return false;
  bool ok = false;
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_public_result_t result;
  quick_public_result_init(&result);
  if (!make_temp_dir(bin_dir))
    goto cleanup;
  char ssh_path[256];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  if (!write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "if [ \"$2\" = quickd ] && [ \"$3\" = sites ] && [ \"$4\" = get ]; "
          "then\n"
          "  printf '%s\\n' "
          "'{\"format_version\":\"1.0\",\"name\":\"demo\",\"public\":false}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n")) {
    goto cleanup;
  }
  setenv("PATH", bin_dir, 1);
  setenv("QUICK_REMOTE", "quick@box", 1);
  quick_public_request_t req = {
      .profiles = &profiles, .site = "demo", .action = QUICK_PUBLIC_STATUS};
  ok = quick_op_public(&req, &result) == APP_SUCCESS && result.have_public &&
       !result.is_public && result.site.name &&
       strcmp(result.site.name, "demo") == 0;
cleanup:
  if (old_path_env)
    setenv("PATH", old_path, 1);
  else
    unsetenv("PATH");
  if (old_remote_env)
    setenv("QUICK_REMOTE", old_remote, 1);
  else
    unsetenv("QUICK_REMOTE");
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
            result.checks[0].name &&
            strcmp(result.checks[0].name, "quick_version") == 0;
  quick_doctor_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return ok;
}

#ifndef _WIN32
static bool check_doctor_remote_ssh(
    bool non_interactive, int connect_timeout_seconds,
    volatile sig_atomic_t *cancel_flag, app_error expected_error,
    const char *expected_argv, const char *forbidden_argv,
    bool expect_cancelled_check) {
  char bin_dir[] = "/tmp/openquick-doctor-ssh-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const bool had_path = old_path_env != NULL;
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  const char *old_log_env = getenv("QUICK_TEST_SSH_LOG");
  const bool had_log = old_log_env != NULL;
  char *old_log = old_log_env ? strdup(old_log_env) : NULL;
  if ((had_path && !old_path) || (had_log && !old_log)) {
    free(old_path);
    free(old_log);
    return false;
  }

  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_doctor_result_t result;
  quick_doctor_result_init(&result);
  bool made_dir = false;
  bool ok = false;
  char rsync_path[256] = {0};
  char ssh_path[256] = {0};
  char log_path[256] = {0};

  if (!make_temp_dir(bin_dir)) {
    goto cleanup;
  }
  made_dir = true;
  snprintf(rsync_path, sizeof(rsync_path), "%s/rsync", bin_dir);
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(log_path, sizeof(log_path), "%s/ssh.log", bin_dir);
  if (!write_executable_file(rsync_path, "#!/bin/sh\nexit 0\n") ||
      !write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "printf '%s\\n' \"$*\" >> \"$QUICK_TEST_SSH_LOG\"\n"
          "printf '%s\\n' '{\"sites\":1,\"releases\":2}'\n"
          "exit 0\n")) {
    goto cleanup;
  }
  setenv("PATH", bin_dir, 1);
  setenv("QUICK_TEST_SSH_LOG", log_path, 1);

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

  quick_doctor_request_t req = {
      .profiles = &profiles,
      .profile = "lab",
      .site = "demo",
      .remote = true,
      .non_interactive = non_interactive,
      .connect_timeout_seconds = connect_timeout_seconds,
      .cancel_flag = cancel_flag,
  };
  app_error err = quick_op_doctor(&req, &result);
  ok = err == expected_error;
  if (ok && expected_argv) {
    ok = file_contains(log_path, expected_argv);
  }
  if (ok && forbidden_argv) {
    ok = !file_contains(log_path, forbidden_argv);
  }
  if (ok && cancel_flag && *cancel_flag) {
    ok = access(log_path, F_OK) != 0;
  }
  if (ok && expect_cancelled_check) {
    ok = false;
    for (size_t i = 0; i < result.count; i++) {
      if (result.checks[i].name &&
          strcmp(result.checks[i].name, "quickd_doctor") == 0 &&
          result.checks[i].status == QUICK_DOCTOR_STATUS_FAIL &&
          result.checks[i].detail &&
          strstr(result.checks[i].detail, "cancelled")) {
        ok = true;
        break;
      }
    }
  }

cleanup:
  if (had_path) {
    setenv("PATH", old_path, 1);
  } else {
    unsetenv("PATH");
  }
  if (had_log) {
    setenv("QUICK_TEST_SSH_LOG", old_log, 1);
  } else {
    unsetenv("QUICK_TEST_SSH_LOG");
  }
  free(old_path);
  free(old_log);
  quick_doctor_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  if (made_dir) {
    unlink(log_path);
    unlink(rsync_path);
    unlink(ssh_path);
    rmdir(bin_dir);
  }
  return ok;
}
#endif

static bool test_doctor_noninteractive_ssh_argv_and_timeout_bounds(void) {
#ifndef _WIN32
  return check_doctor_remote_ssh(
             true, 0, NULL, APP_SUCCESS,
             "-o BatchMode=yes -o ConnectTimeout=10 -o "
             "ConnectionAttempts=1 quick@box quickd doctor --host --json",
             NULL, false) &&
         check_doctor_remote_ssh(
             true, 1, NULL, APP_SUCCESS,
             "-o BatchMode=yes -o ConnectTimeout=1 -o "
             "ConnectionAttempts=1 quick@box quickd doctor --host --json",
             NULL, false) &&
         check_doctor_remote_ssh(
             true, 121, NULL, APP_SUCCESS,
             "-o BatchMode=yes -o ConnectTimeout=120 -o "
             "ConnectionAttempts=1 quick@box quickd doctor --host --json",
             NULL, false);
#else
  return true;
#endif
}

static bool test_doctor_interactive_ssh_argv_is_unchanged(void) {
#ifndef _WIN32
  return check_doctor_remote_ssh(
      false, 99, NULL, APP_SUCCESS,
      "quick@box quickd doctor --host --json", "BatchMode=yes", false);
#else
  return true;
#endif
}

static bool test_doctor_cancelled_before_remote_ssh(void) {
#ifndef _WIN32
  volatile sig_atomic_t cancel = 1;
  return check_doctor_remote_ssh(true, 0, &cancel, APP_ERROR_INTERRUPTED, NULL,
                                 NULL, true);
#else
  return true;
#endif
}

#ifndef _WIN32
typedef enum {
  DOCTOR_DEEP_FAKE_NORMAL = 0,
  DOCTOR_DEEP_FAKE_CANCEL_RSYNC,
  DOCTOR_DEEP_FAKE_CANCEL_ACTIVATE,
  DOCTOR_DEEP_FAKE_PRE_CANCEL,
} doctor_deep_fake_mode_t;

static volatile sig_atomic_t doctor_deep_alarm_cancel;

static void doctor_deep_alarm_handler(int signum) {
  (void)signum;
  doctor_deep_alarm_cancel = 1;
}

static bool check_doctor_deep_rsync(
    bool non_interactive, int connect_timeout_seconds,
    const char *expected_rsh, doctor_deep_fake_mode_t mode) {
  char bin_dir[] = "/tmp/openquick-doctor-deep-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const char *old_rsync_log_env = getenv("QUICK_TEST_RSYNC_LOG");
  const char *old_ssh_log_env = getenv("QUICK_TEST_SSH_LOG");
  const char *old_mode_env = getenv("QUICK_TEST_DOCTOR_MODE");
  const char *old_remote_env = getenv("QUICK_REMOTE");
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  char *old_rsync_log =
      old_rsync_log_env ? strdup(old_rsync_log_env) : NULL;
  char *old_ssh_log = old_ssh_log_env ? strdup(old_ssh_log_env) : NULL;
  char *old_mode = old_mode_env ? strdup(old_mode_env) : NULL;
  char *old_remote = old_remote_env ? strdup(old_remote_env) : NULL;
  if ((old_path_env && !old_path) ||
      (old_rsync_log_env && !old_rsync_log) ||
      (old_ssh_log_env && !old_ssh_log) || (old_mode_env && !old_mode) ||
      (old_remote_env && !old_remote)) {
    free(old_path);
    free(old_rsync_log);
    free(old_ssh_log);
    free(old_mode);
    free(old_remote);
    return false;
  }

  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  quick_doctor_result_t result;
  quick_doctor_result_init(&result);
  bool made_dir = false;
  bool signal_handler_installed = false;
  bool ok = false;
  struct sigaction old_alarm_action = {0};
  char rsync_path[256] = {0};
  char ssh_path[256] = {0};
  char curl_path[256] = {0};
  char rsync_log_path[256] = {0};
  char ssh_log_path[256] = {0};

  if (!make_temp_dir(bin_dir)) {
    goto cleanup;
  }
  made_dir = true;
  snprintf(rsync_path, sizeof(rsync_path), "%s/rsync", bin_dir);
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(curl_path, sizeof(curl_path), "%s/curl", bin_dir);
  snprintf(rsync_log_path, sizeof(rsync_log_path), "%s/rsync.log", bin_dir);
  snprintf(ssh_log_path, sizeof(ssh_log_path), "%s/ssh.log", bin_dir);

  if (!write_executable_file(
          rsync_path,
          "#!/bin/sh\n"
          "{\n"
          "  printf 'argc=%s\\n' \"$#\"\n"
          "  for arg in \"$@\"; do printf '<%s>\\n' \"$arg\"; done\n"
          "} > \"$QUICK_TEST_RSYNC_LOG\"\n"
          "if [ \"$QUICK_TEST_DOCTOR_MODE\" = cancel-rsync ]; then\n"
          "  kill -ALRM \"$PPID\"\n"
          "  exec /bin/sleep 10\n"
          "fi\n"
          "exit 0\n") ||
      !write_executable_file(
          ssh_path,
          "#!/bin/sh\n"
          "printf '%s\\n' \"$*\" >> \"$QUICK_TEST_SSH_LOG\"\n"
          "if [ \"$1\" = -o ]; then shift 6; fi\n"
          "if [ \"$1\" != quick@box ]; then exit 2; fi\n"
          "shift\n"
          "if [ \"$1\" = quickd ] && [ \"$2\" = doctor ]; then\n"
          "  printf '%s\\n' '{\"format_version\":\"1.0\",\"ok\":true}'\n"
          "  exit 0\n"
          "fi\n"
          "if [ \"$1\" = quickd ] && [ \"$2\" = admin ]; then\n"
          "  printf '%s\\n' '{\"sites\":1,\"releases\":2}'\n"
          "  exit 0\n"
          "fi\n"
          "if [ \"$1\" = quickd ] && [ \"$2\" = deploy ] && "
          "[ \"$3\" = prepare ]; then\n"
          "  printf '%s\\n' "
          "'{\"deploy_id\":\"doctor-deploy\",\"staging_path\":\"/srv/"
          "quick/.doctor-staging\"}'\n"
          "  exit 0\n"
          "fi\n"
          "if [ \"$1\" = quickd ] && [ \"$2\" = deploy ] && "
          "[ \"$3\" = activate ]; then\n"
          "  if [ \"$QUICK_TEST_DOCTOR_MODE\" = cancel-activate ]; then\n"
          "    kill -ALRM \"$PPID\"\n"
          "    exec /bin/sleep 10\n"
          "  fi\n"
          "  printf '%s\\n' "
          "'{\"url\":\"https://doctor.quick.example.com/\"}'\n"
          "  exit 0\n"
          "fi\n"
          "if [ \"$1\" = quickd ] && [ \"$2\" = sites ] && "
          "[ \"$3\" = delete ]; then\n"
          "  printf '%s\\n' '{\"deleted\":true}'\n"
          "  exit 0\n"
          "fi\n"
          "exit 1\n") ||
      !write_executable_file(
          curl_path,
          "#!/bin/sh\n"
          "case \"$4\" in\n"
          "  */_quick/identity)\n"
          "    printf '%s\\n' "
          "'{\"authenticated\":true,\"provider\":\"test\",\"subject\":"
          "\"test:doctor\"}' ;;\n"
          "  *) printf '%s\\n' '{\"ok\":true}' ;;\n"
          "esac\n"
          "exit 0\n")) {
    goto cleanup;
  }

  setenv("PATH", bin_dir, 1);
  setenv("QUICK_TEST_RSYNC_LOG", rsync_log_path, 1);
  setenv("QUICK_TEST_SSH_LOG", ssh_log_path, 1);
  setenv("QUICK_REMOTE", "quick@box", 1);
  switch (mode) {
  case DOCTOR_DEEP_FAKE_CANCEL_RSYNC:
    setenv("QUICK_TEST_DOCTOR_MODE", "cancel-rsync", 1);
    break;
  case DOCTOR_DEEP_FAKE_CANCEL_ACTIVATE:
    setenv("QUICK_TEST_DOCTOR_MODE", "cancel-activate", 1);
    break;
  default:
    setenv("QUICK_TEST_DOCTOR_MODE", "normal", 1);
    break;
  }

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

  const bool child_signals_cancel =
      mode == DOCTOR_DEEP_FAKE_CANCEL_RSYNC ||
      mode == DOCTOR_DEEP_FAKE_CANCEL_ACTIVATE;
  doctor_deep_alarm_cancel =
      mode == DOCTOR_DEEP_FAKE_PRE_CANCEL ? 1 : 0;
  if (child_signals_cancel) {
    struct sigaction action = {0};
    action.sa_handler = doctor_deep_alarm_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, &old_alarm_action) != 0) {
      goto cleanup;
    }
    signal_handler_installed = true;
  }
  const volatile sig_atomic_t *cancel_flag =
      mode == DOCTOR_DEEP_FAKE_NORMAL ? NULL : &doctor_deep_alarm_cancel;
  quick_doctor_request_t req = {
      .profiles = &profiles,
      .profile = "lab",
      .site = "demo",
      .remote = true,
      .deep = true,
      .non_interactive = non_interactive,
      .connect_timeout_seconds = connect_timeout_seconds,
      .cancel_flag = cancel_flag,
  };
  app_error err = quick_op_doctor(&req, &result);
  if (signal_handler_installed) {
    (void)sigaction(SIGALRM, &old_alarm_action, NULL);
    signal_handler_installed = false;
  }

  const quick_doctor_check_t *deep_check = NULL;
  for (size_t i = 0; i < result.count; i++) {
    if (result.checks[i].name &&
        strcmp(result.checks[i].name, "deep_temp_deploy") == 0) {
      deep_check = &result.checks[i];
      break;
    }
  }

  if (mode == DOCTOR_DEEP_FAKE_NORMAL) {
    ok = err == APP_SUCCESS && deep_check &&
         deep_check->status == QUICK_DOCTOR_STATUS_OK &&
         access(rsync_log_path, F_OK) == 0 &&
         file_contains(rsync_log_path,
                       expected_rsh ? "argc=6\n<-az>\n<--delete>\n<-e>\n"
                                    : "argc=4\n<-az>\n<--delete>\n") &&
         file_contains(rsync_log_path, "</tmp/openquick-doctor-") &&
         file_contains(rsync_log_path,
                       "<quick@box:/srv/quick/.doctor-staging/>");
    if (ok && expected_rsh) {
      char rsh_line[192];
      snprintf(rsh_line, sizeof(rsh_line), "<%s>\n", expected_rsh);
      ok = file_contains(rsync_log_path, rsh_line) &&
           !file_contains(rsync_log_path,
                          "ConnectionAttempts=1 quick@box");
    } else if (ok) {
      ok = !file_contains(rsync_log_path, "<-e>") &&
           !file_contains(rsync_log_path, "BatchMode=yes");
    }
  } else if (mode == DOCTOR_DEEP_FAKE_CANCEL_RSYNC) {
    ok = err == APP_ERROR_INTERRUPTED && deep_check &&
         deep_check->status == QUICK_DOCTOR_STATUS_FAIL &&
         deep_check->detail &&
         strstr(deep_check->detail, "transfer cancelled") &&
         access(rsync_log_path, F_OK) == 0 &&
         !file_contains(ssh_log_path, "quickd sites delete");
  } else if (mode == DOCTOR_DEEP_FAKE_CANCEL_ACTIVATE) {
    ok = err == APP_ERROR_INTERRUPTED && deep_check &&
         deep_check->status == QUICK_DOCTOR_STATUS_FAIL &&
         deep_check->detail &&
         strstr(deep_check->detail, "activation cancelled") &&
         strstr(deep_check->detail, "cleanup succeeded") &&
         file_contains(ssh_log_path, "quickd sites delete");
  } else {
    ok = err == APP_ERROR_INTERRUPTED &&
         access(rsync_log_path, F_OK) != 0 && access(ssh_log_path, F_OK) != 0;
  }

cleanup:
  if (signal_handler_installed) {
    (void)sigaction(SIGALRM, &old_alarm_action, NULL);
  }
  if (old_path_env) {
    setenv("PATH", old_path, 1);
  } else {
    unsetenv("PATH");
  }
  if (old_rsync_log_env) {
    setenv("QUICK_TEST_RSYNC_LOG", old_rsync_log, 1);
  } else {
    unsetenv("QUICK_TEST_RSYNC_LOG");
  }
  if (old_ssh_log_env) {
    setenv("QUICK_TEST_SSH_LOG", old_ssh_log, 1);
  } else {
    unsetenv("QUICK_TEST_SSH_LOG");
  }
  if (old_mode_env) {
    setenv("QUICK_TEST_DOCTOR_MODE", old_mode, 1);
  } else {
    unsetenv("QUICK_TEST_DOCTOR_MODE");
  }
  if (old_remote_env) {
    setenv("QUICK_REMOTE", old_remote, 1);
  } else {
    unsetenv("QUICK_REMOTE");
  }
  free(old_path);
  free(old_rsync_log);
  free(old_ssh_log);
  free(old_mode);
  free(old_remote);
  quick_doctor_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  if (made_dir) {
    unlink(rsync_log_path);
    unlink(ssh_log_path);
    unlink(rsync_path);
    unlink(ssh_path);
    unlink(curl_path);
    rmdir(bin_dir);
  }
  return ok;
}
#endif

static bool test_doctor_deep_rsync_argv_and_timeout_bounds(void) {
#ifndef _WIN32
  return check_doctor_deep_rsync(false, 99, NULL,
                                 DOCTOR_DEEP_FAKE_NORMAL) &&
         check_doctor_deep_rsync(
             true, 0,
             "ssh -o BatchMode=yes -o ConnectTimeout=10 -o "
             "ConnectionAttempts=1",
             DOCTOR_DEEP_FAKE_NORMAL) &&
         check_doctor_deep_rsync(
             true, 121,
             "ssh -o BatchMode=yes -o ConnectTimeout=120 -o "
             "ConnectionAttempts=1",
             DOCTOR_DEEP_FAKE_NORMAL);
#else
  return true;
#endif
}

static bool test_doctor_deep_rsync_cancellation_is_interrupted(void) {
#ifndef _WIN32
  return check_doctor_deep_rsync(true, 10, NULL,
                                 DOCTOR_DEEP_FAKE_CANCEL_RSYNC);
#else
  return true;
#endif
}

static bool test_doctor_deep_activation_cancel_cleans_remote_site(void) {
#ifndef _WIN32
  return check_doctor_deep_rsync(true, 10, NULL,
                                 DOCTOR_DEEP_FAKE_CANCEL_ACTIVATE);
#else
  return true;
#endif
}

static bool test_doctor_deep_precancel_skips_rsync_spawn(void) {
#ifndef _WIN32
  return check_doctor_deep_rsync(true, 10, NULL,
                                 DOCTOR_DEEP_FAKE_PRE_CANCEL);
#else
  return true;
#endif
}

#ifndef _WIN32
static bool check_doctor_identity_response(
    const char *identity_body, quick_doctor_status_t expected_status,
    bool expected_result_ok, const char *detail_needle) {
  char bin_dir[] = "/tmp/openquick-doctor-identity-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const bool had_path = old_path_env != NULL;
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  if (had_path && !old_path)
    return false;

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
  int n = snprintf(curl_script, sizeof(curl_script),
                   "#!/bin/sh\n"
                   "case \"$4\" in\n"
                   "  */_quick/health) printf '%%s\\n' "
                   "'{\"format_version\":\"1.0\",\"ok\":true}' ; exit 0 ;;\n"
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

  quick_doctor_request_t req = {
      .profiles = &profiles, .profile = "lab", .site = "demo", .remote = true};
  if (quick_op_doctor(&req, &result) != APP_SUCCESS ||
      result.ok != expected_result_ok) {
    goto cleanup;
  }
  for (size_t i = 0; i < result.count; i++) {
    if (result.checks[i].name &&
        strcmp(result.checks[i].name, "http_identity") == 0) {
      ok = result.checks[i].status == expected_status &&
           (!detail_needle ||
            (result.checks[i].detail &&
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

static bool test_onboarding_init_reset_and_return_destination(void) {
  quick_onboarding_model_t model;
  memset(&model, 0xA5, sizeof(model));
  quick_onboarding_model_init(&model);
  if (model.state != QUICK_ONBOARD_STATE_IDLE ||
      model.flow != QUICK_ONBOARD_FLOW_NONE ||
      model.return_destination != QUICK_ONBOARD_RETURN_DASHBOARD ||
      !model.validation.valid || model.validation.code != APP_SUCCESS ||
      model.validation.message[0] != '\0' || model.values.project_dir[0] ||
      model.checks.completed[QUICK_ONBOARD_CHECK_DOCTOR] ||
      model.mutation.started) {
    return false;
  }

  snprintf(model.values.site_name, sizeof(model.values.site_name), "demo");
  model.checks.completed[QUICK_ONBOARD_CHECK_PROJECT] = true;
  model.mutation.backup_created = true;
  quick_onboarding_set_validation(&model, APP_ERROR_VALIDATION, "bad");
  quick_onboarding_model_reset(&model, QUICK_ONBOARD_RETURN_DEPLOY);
  return model.state == QUICK_ONBOARD_STATE_IDLE &&
         model.flow == QUICK_ONBOARD_FLOW_NONE &&
         model.return_destination == QUICK_ONBOARD_RETURN_DEPLOY &&
         model.values.site_name[0] == '\0' &&
         !model.checks.completed[QUICK_ONBOARD_CHECK_PROJECT] &&
         !model.mutation.backup_created && model.validation.valid &&
         model.validation.code == APP_SUCCESS &&
         model.validation.message[0] == '\0';
}

static bool test_onboarding_local_adopt_back_cancel_retry(void) {
  quick_onboarding_model_t model;
  quick_onboarding_model_reset(&model, QUICK_ONBOARD_RETURN_SERVE);
  if (!quick_onboarding_transition(&model,
                                   QUICK_ONBOARD_EVENT_SHOW_WELCOME) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CHOOSE_LOCAL) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_DIRECTORY ||
      model.flow != QUICK_ONBOARD_FLOW_LOCAL || model.values.adopt_existing) {
    return false;
  }
  snprintf(model.values.project_dir, sizeof(model.values.project_dir),
           "/tmp/demo");
  snprintf(model.values.site_name, sizeof(model.values.site_name), "demo");
  if (!quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_IDENTITY ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_TEMPLATE ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_BACK) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_IDENTITY ||
      strcmp(model.values.project_dir, "/tmp/demo") != 0 ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_REVIEW ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CONFIRM) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_APPLY ||
      !quick_onboarding_transition(&model,
                                   QUICK_ONBOARD_EVENT_BEGIN_MUTATION) ||
      !model.mutation.started ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_PREVIEW ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_COMPLETE) ||
      model.state != QUICK_ONBOARD_STATE_COMPLETE) {
    return false;
  }

  if (!quick_onboarding_transition(&model,
                                   QUICK_ONBOARD_EVENT_SHOW_WELCOME) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CHOOSE_ADOPT) ||
      !model.values.adopt_existing ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_REVIEW ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_BACK) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_IDENTITY ||
      !model.values.adopt_existing ||
      strcmp(model.values.site_name, "demo") != 0) {
    return false;
  }

  model.state = QUICK_ONBOARD_STATE_FAILED;
  model.checks.completed[QUICK_ONBOARD_CHECK_PROJECT] = true;
  model.checks.passed[QUICK_ONBOARD_CHECK_PROJECT] = true;
  model.mutation.started = true;
  model.mutation.partial_cleanup_remains = true;
  quick_onboarding_set_validation(&model, APP_ERROR_IO, "try again");
  if (!quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_RETRY) ||
      model.state != QUICK_ONBOARD_STATE_LOCAL_DIRECTORY ||
      strcmp(model.values.site_name, "demo") != 0 ||
      model.return_destination != QUICK_ONBOARD_RETURN_SERVE ||
      !model.validation.valid || model.validation.code != APP_SUCCESS ||
      model.validation.message[0] != '\0' || model.mutation.started ||
      model.mutation.partial_cleanup_remains ||
      model.checks.completed[QUICK_ONBOARD_CHECK_PROJECT] ||
      model.checks.passed[QUICK_ONBOARD_CHECK_PROJECT] ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CANCEL) ||
      model.state != QUICK_ONBOARD_STATE_CANCELLED) {
    return false;
  }
  return quick_onboarding_transition(&model,
                                     QUICK_ONBOARD_EVENT_SHOW_WELCOME) &&
         model.state == QUICK_ONBOARD_STATE_WELCOME &&
         model.flow == QUICK_ONBOARD_FLOW_NONE;
}

static bool test_onboarding_connect_and_install_transitions(void) {
  quick_onboarding_model_t model;
  quick_onboarding_model_init(&model);
  if (!quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CHOOSE_CONNECT) ||
      model.state != QUICK_ONBOARD_STATE_HOST_FIELDS ||
      model.flow != QUICK_ONBOARD_FLOW_CONNECT_HOST) {
    return false;
  }
  snprintf(model.values.host, sizeof(model.values.host), "quick@box");
  if (!quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      model.state != QUICK_ONBOARD_STATE_HOST_REVIEW ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_BACK) ||
      model.state != QUICK_ONBOARD_STATE_HOST_FIELDS ||
      strcmp(model.values.host, "quick@box") != 0 ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CONFIRM) ||
      model.state != QUICK_ONBOARD_STATE_HOST_VERIFY ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_VERIFY_OK) ||
      model.state != QUICK_ONBOARD_STATE_HOST_SAVE ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_COMPLETE) ||
      model.state != QUICK_ONBOARD_STATE_COMPLETE) {
    return false;
  }

  quick_onboarding_model_reset(&model, QUICK_ONBOARD_RETURN_WELCOME);
  if (!quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CHOOSE_INSTALL) ||
      model.flow != QUICK_ONBOARD_FLOW_INSTALL_HOST ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CONFIRM) ||
      model.state != QUICK_ONBOARD_STATE_HOST_INSTALL ||
      !quick_onboarding_transition(&model,
                                   QUICK_ONBOARD_EVENT_BEGIN_MUTATION) ||
      !model.mutation.started ||
      quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CANCEL) ||
      model.state != QUICK_ONBOARD_STATE_HOST_INSTALL ||
      quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_BACK) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_VERIFY_FAILED) ||
      model.state != QUICK_ONBOARD_STATE_FAILED ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_RETRY) ||
      model.state != QUICK_ONBOARD_STATE_HOST_FIELDS || model.mutation.started ||
      model.return_destination != QUICK_ONBOARD_RETURN_WELCOME) {
    return false;
  }

  quick_onboarding_model_init(&model);
  if (!quick_onboarding_transition(&model,
                                   QUICK_ONBOARD_EVENT_CHOOSE_INSTALL) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CONFIRM) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_VERIFY_OK) ||
      model.state != QUICK_ONBOARD_STATE_HOST_SAVE) {
    return false;
  }

  quick_onboarding_model_reset(&model, QUICK_ONBOARD_RETURN_DEPLOY);
  return quick_onboarding_transition(&model,
                                     QUICK_ONBOARD_EVENT_CHOOSE_CONNECT) &&
         quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) &&
         quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CONFIRM) &&
         quick_onboarding_transition(&model,
                                     QUICK_ONBOARD_EVENT_VERIFY_FAILED) &&
         model.state == QUICK_ONBOARD_STATE_FAILED &&
         quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_VERIFY_OK) &&
         model.state == QUICK_ONBOARD_STATE_HOST_SAVE &&
         model.return_destination == QUICK_ONBOARD_RETURN_DEPLOY;
}

static bool test_onboarding_welcome_and_local_failure_semantics(void) {
  quick_onboarding_model_t model;
  quick_onboarding_model_init(&model);
  if (!quick_onboarding_transition(&model,
                                   QUICK_ONBOARD_EVENT_SHOW_WELCOME) ||
      !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_COMPLETE) ||
      model.state != QUICK_ONBOARD_STATE_COMPLETE) {
    return false;
  }

  quick_onboarding_model_reset(&model, QUICK_ONBOARD_RETURN_DASHBOARD);
  return quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CHOOSE_LOCAL) &&
         quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) &&
         quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) &&
         quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) &&
         model.state == QUICK_ONBOARD_STATE_LOCAL_REVIEW &&
         quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_CONFIRM) &&
         quick_onboarding_transition(&model,
                                     QUICK_ONBOARD_EVENT_BEGIN_MUTATION) &&
         quick_onboarding_transition(&model,
                                     QUICK_ONBOARD_EVENT_VERIFY_FAILED) &&
         model.state == QUICK_ONBOARD_STATE_FAILED;
}

static bool test_onboarding_invalid_transition_is_immutable(void) {
  quick_onboarding_model_t model;
  quick_onboarding_model_init(&model);
  snprintf(model.values.site_name, sizeof(model.values.site_name), "sentinel");
  quick_onboarding_model_t before = model;
  if (quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_NEXT) ||
      memcmp(&model, &before, sizeof(model)) != 0) {
    return false;
  }

  model.state = QUICK_ONBOARD_STATE_LOCAL_APPLY;
  before = model;
  return !quick_onboarding_transition(&model, QUICK_ONBOARD_EVENT_BACK) &&
         memcmp(&model, &before, sizeof(model)) == 0 &&
         !quick_onboarding_transition(NULL, QUICK_ONBOARD_EVENT_CANCEL);
}

static bool test_onboarding_checks_and_validation(void) {
  quick_onboarding_model_t model;
  quick_onboarding_model_init(&model);
  quick_onboarding_note_check(&model, QUICK_ONBOARD_CHECK_SSH, false);
  quick_onboarding_note_check(&model, QUICK_ONBOARD_CHECK_DOCTOR, true);
  quick_onboarding_checks_t before = model.checks;
  quick_onboarding_note_check(&model, (quick_onboarding_check_t)-1, true);
  quick_onboarding_note_check(&model, QUICK_ONBOARD_CHECK_COUNT, true);
  if (!model.checks.completed[QUICK_ONBOARD_CHECK_SSH] ||
      model.checks.passed[QUICK_ONBOARD_CHECK_SSH] ||
      !model.checks.completed[QUICK_ONBOARD_CHECK_DOCTOR] ||
      !model.checks.passed[QUICK_ONBOARD_CHECK_DOCTOR] ||
      memcmp(&model.checks, &before, sizeof(before)) != 0) {
    return false;
  }

  quick_onboarding_set_validation(&model, APP_ERROR_VALIDATION, "bad host");
  if (model.validation.valid ||
      model.validation.code != APP_ERROR_VALIDATION ||
      strcmp(model.validation.message, "bad host") != 0) {
    return false;
  }
  quick_onboarding_clear_validation(&model);
  return model.validation.valid && model.validation.code == APP_SUCCESS &&
         model.validation.message[0] == '\0';
}

static bool test_onboarding_install_result_copy_and_states(void) {
  quick_onboarding_model_t model;
  quick_onboarding_model_init(&model);
  char backup_path[] = "/srv/quick/backups/before";
  char cleanup_detail[] = "remove quick-deploy group";
  quick_install_result_t result = {
      .completed = true,
      .mutation_started = true,
      .backup_created = true,
      .backup_path = backup_path,
      .rollback_attempted = true,
      .rollback_ok = false,
      .partial_cleanup_remains = true,
      .cleanup_detail = cleanup_detail,
  };
  quick_onboarding_note_install_result(&model, &result);
  backup_path[0] = 'X';
  cleanup_detail[0] = 'X';
  if (model.state != QUICK_ONBOARD_STATE_HOST_SAVE ||
      !model.mutation.started || !model.mutation.backup_created ||
      !model.mutation.rollback_attempted || model.mutation.rollback_ok ||
      !model.mutation.partial_cleanup_remains ||
      strcmp(model.mutation.backup_path, "/srv/quick/backups/before") != 0 ||
      strcmp(model.mutation.cleanup_detail, "remove quick-deploy group") != 0) {
    return false;
  }

  result = (quick_install_result_t){.cancelled = true};
  quick_onboarding_note_install_result(&model, &result);
  if (model.state != QUICK_ONBOARD_STATE_CANCELLED || model.mutation.started ||
      model.mutation.backup_path[0] != '\0' ||
      model.mutation.cleanup_detail[0] != '\0') {
    return false;
  }

  result = (quick_install_result_t){.cancelled = true,
                                    .mutation_started = true,
                                    .rollback_attempted = true,
                                    .rollback_ok = true};
  quick_onboarding_note_install_result(&model, &result);
  if (model.state != QUICK_ONBOARD_STATE_FAILED || !model.mutation.started ||
      !model.mutation.rollback_attempted || !model.mutation.rollback_ok) {
    return false;
  }

  result = (quick_install_result_t){.failure_phase =
                                        QUICK_INSTALL_PHASE_HOST_DOCTOR,
                                    .partial_cleanup_remains = true,
                                    .cleanup_detail = (char *)"manual cleanup"};
  quick_onboarding_note_install_result(&model, &result);
  return model.state == QUICK_ONBOARD_STATE_FAILED &&
         !model.mutation.started && model.mutation.partial_cleanup_remains &&
         strcmp(model.mutation.cleanup_detail, "manual cleanup") == 0;
}

static bool test_onboarding_state_labels(void) {
  for (int state = QUICK_ONBOARD_STATE_IDLE;
       state <= QUICK_ONBOARD_STATE_FAILED; state++) {
    const char *first =
        quick_onboarding_state_label((quick_onboarding_state_t)state);
    const char *second =
        quick_onboarding_state_label((quick_onboarding_state_t)state);
    if (!first || first[0] == '\0' || first != second ||
        strcmp(first, "unknown") == 0) {
      return false;
    }
  }
  return strcmp(quick_onboarding_state_label((quick_onboarding_state_t)999),
                "unknown") == 0;
}

static bool test_mint_dev_token_parses_json(void) {
#ifndef _WIN32
  char bin_dir[] = "/tmp/openquick-dev-token-bin-XXXXXX";
  const char *old_path_env = getenv("PATH");
  const char *old_remote_env = getenv("QUICK_REMOTE");
  const char *old_base_domain_env = getenv("QUICK_BASE_DOMAIN");
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  char *old_remote = old_remote_env ? strdup(old_remote_env) : NULL;
  char *old_base_domain =
      old_base_domain_env ? strdup(old_base_domain_env) : NULL;
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

  if (!make_temp_dir(bin_dir))
    goto cleanup;
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
          "  printf '%s\\n' "
          "'{\"token\":\"dev-token-123\",\"site\":\"demo\",\"expires_at\":"
          "\"2026-06-12T01:00:00Z\"}'\n"
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
  ok = quick_op_mint_dev_token(&req, &result) == APP_SUCCESS && result.token &&
       strcmp(result.token, "dev-token-123") == 0 && result.expires_at &&
       strcmp(result.expires_at, "2026-06-12T01:00:00Z") == 0 && result.site &&
       strcmp(result.site, "demo") == 0 && result.url &&
       strcmp(result.url, "https://demo.quick.example.com") == 0;

cleanup:
  if (old_path_env)
    setenv("PATH", old_path, 1);
  else
    unsetenv("PATH");
  if (old_remote_env)
    setenv("QUICK_REMOTE", old_remote, 1);
  else
    unsetenv("QUICK_REMOTE");
  if (old_base_domain_env)
    setenv("QUICK_BASE_DOMAIN", old_base_domain, 1);
  else
    unsetenv("QUICK_BASE_DOMAIN");
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
            steps.count == 7 && steps.steps[0].summary &&
            strstr(steps.steps[0].summary, "scp") != NULL &&
            strstr(steps.steps[0].summary, "rsync") != NULL &&
            strcmp(steps.steps[1].summary,
                   "create quick user and quick-deploy group") == 0 &&
            strstr(steps.steps[6].summary, "quickd doctor") != NULL;
  quick_serve_install_steps_destroy(&steps);
  return ok;
}

static bool test_iap_product_model(void) {
  return quick_iap_is_tailscale("tailscale") &&
         quick_iap_is_tailscale("tailscale-serve") &&
         !quick_iap_is_tailscale("cloudflare") &&
         quick_iap_is_cloudflare("cloudflare") &&
         quick_iap_is_cloudflare("cloudflare-access") &&
         quick_iap_is_supported("none") && !quick_iap_is_supported("bogus") &&
         strcmp(quick_iap_default_mode("tailscale"), "localapi") == 0 &&
         strcmp(quick_iap_default_mode("tailscale-serve"), "serve") == 0 &&
         strcmp(quick_iap_default_mode("tailscale-tsnet"), "tsnet") == 0 &&
         strcmp(quick_iap_default_mode("cloudflare"), "access") == 0 &&
         strcmp(quick_iap_default_mode("none"), "") == 0;
}

static bool test_serve_local_url(void) {
  char *u = NULL;
  if (quick_op_serve_local_url("lunch-vote", "9366", &u) != APP_SUCCESS ||
      !u || strcmp(u, "http://localhost:9366/~/lunch-vote/") != 0) {
    free(u);
    return false;
  }
  free(u);

  u = NULL;
  if (quick_op_serve_local_url(NULL, "8080", &u) != APP_SUCCESS || !u ||
      strcmp(u, "http://localhost:8080/") != 0) {
    free(u);
    return false;
  }
  free(u);

  u = NULL;
  bool ok = quick_op_serve_local_url("site", NULL, &u) == APP_SUCCESS && u &&
            strcmp(u, "http://localhost:9366/~/site/") == 0;
  free(u);
  return ok;
}

static bool test_context_classify_no_project(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-context-none-XXXXXX";
  if (!make_temp_dir(dir))
    return false;

  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  quick_context_result_t result;
  quick_context_result_init(&result);
  quick_context_request_t req = {.profiles = &cfg, .dir = dir};
  bool ok = quick_op_classify_context(&req, &result) == APP_SUCCESS &&
            result.project_state == QUICK_PROJECT_NONE &&
            !result.project_valid && !result.has_profiles &&
            result.show_welcome;
  quick_context_result_destroy(&result);
  quick_profile_config_destroy(&cfg);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_context_classify_valid_project(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-context-valid-XXXXXX";
  if (!make_temp_dir(dir))
    return false;
  char path[256];
  snprintf(path, sizeof(path), "%s/quick.json", dir);
  if (!write_file(path,
                  "{\"name\":\"demo\",\"source\":\".\",\"output\":\".\"}")) {
    rmdir(dir);
    return false;
  }

  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  quick_context_result_t result;
  quick_context_result_init(&result);
  quick_context_request_t req = {.profiles = &cfg, .dir = dir};
  bool ok = quick_op_classify_context(&req, &result) == APP_SUCCESS &&
            result.project_state == QUICK_PROJECT_VALID &&
            result.project_valid && result.site_name &&
            strcmp(result.site_name, "demo") == 0 && !result.show_welcome;
  quick_context_result_destroy(&result);
  quick_profile_config_destroy(&cfg);
  unlink(path);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_context_classify_malformed_project(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-context-malformed-XXXXXX";
  if (!make_temp_dir(dir))
    return false;
  char path[256];
  snprintf(path, sizeof(path), "%s/quick.json", dir);
  if (!write_file(path, "{ this is not valid json ")) {
    rmdir(dir);
    return false;
  }

  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  quick_context_result_t result;
  quick_context_result_init(&result);
  quick_context_request_t req = {.profiles = &cfg, .dir = dir};
  bool ok = quick_op_classify_context(&req, &result) == APP_SUCCESS &&
            result.project_state == QUICK_PROJECT_MALFORMED &&
            result.project_malformed && !result.project_valid &&
            !result.show_welcome;
  quick_context_result_destroy(&result);
  quick_profile_config_destroy(&cfg);
  unlink(path);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_context_classify_adoptable_folder(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-context-adoptable-XXXXXX";
  if (!make_temp_dir(dir))
    return false;
  char path[256];
  snprintf(path, sizeof(path), "%s/index.html", dir);
  if (!write_file(path, "<html></html>")) {
    rmdir(dir);
    return false;
  }

  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  quick_context_result_t result;
  quick_context_result_init(&result);
  quick_context_request_t req = {.profiles = &cfg, .dir = dir};
  bool ok = quick_op_classify_context(&req, &result) == APP_SUCCESS &&
            result.project_state == QUICK_PROJECT_ADOPTABLE &&
            result.adoptable_folder && result.show_welcome;
  quick_context_result_destroy(&result);
  quick_profile_config_destroy(&cfg);
  unlink(path);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_context_classify_profile_missing(void) {
#ifndef _WIN32
  char dir[] = "/tmp/openquick-context-profile-XXXXXX";
  if (!make_temp_dir(dir))
    return false;
  char path[256];
  snprintf(path, sizeof(path), "%s/quick.json", dir);
  if (!write_file(path, "{\"name\":\"demo\",\"profile\":\"ghost\"}")) {
    rmdir(dir);
    return false;
  }

  quick_profile_config_t cfg;
  quick_profile_config_init(&cfg);
  if (!quick_profile_config_upsert(&cfg, "lab")) {
    quick_profile_config_destroy(&cfg);
    unlink(path);
    rmdir(dir);
    return false;
  }
  quick_context_result_t result;
  quick_context_result_init(&result);
  quick_context_request_t req = {.profiles = &cfg, .dir = dir};
  bool ok = quick_op_classify_context(&req, &result) == APP_SUCCESS &&
            result.project_valid && result.project_profile_missing &&
            result.has_profiles && !result.show_welcome;
  quick_context_result_destroy(&result);
  quick_profile_config_destroy(&cfg);
  unlink(path);
  rmdir(dir);
  return ok;
#else
  return true;
#endif
}

static bool test_install_phase_labels(void) {
  return strstr(quick_install_phase_label(QUICK_INSTALL_PHASE_SSH_VERIFY),
                "ssh") != NULL &&
         strstr(quick_install_phase_label(QUICK_INSTALL_PHASE_ROLLBACK),
                "rollback") != NULL &&
         strstr(quick_install_phase_label(QUICK_INSTALL_PHASE_HOST_DOCTOR),
                "doctor") != NULL &&
         strcmp(quick_install_phase_label(QUICK_INSTALL_PHASE_DONE), "done") ==
             0;
}

static bool test_install_result_lifecycle(void) {
  quick_install_result_t result;
  quick_install_result_init(&result);
  bool ok = !result.failure_message && !result.completed &&
            !result.mutation_started && !result.backup_created &&
            !result.rollback_attempted && !result.partial_cleanup_remains &&
            !result.cleanup_detail;
  result.failure_message = strdup("x");
  result.backup_path = strdup("/tmp/b");
  result.doctor_detail = strdup("d");
  result.cleanup_detail = strdup("c");
  result.partial_cleanup_remains = true;
  ok = ok && result.failure_message && result.backup_path &&
       result.doctor_detail && result.cleanup_detail;
  quick_install_result_destroy(&result);
  return ok && !result.failure_message && !result.backup_path &&
         !result.cleanup_detail && !result.partial_cleanup_remains;
}

static bool test_install_missing_host_returns_error(void) {
  quick_install_result_t result;
  quick_install_result_init(&result);
  quick_iap_config_t iap = {.type = (char *)"tailscale"};
  quick_install_request_t req = {.host = NULL,
                                 .remote_root = "/srv/quick",
                                 .iap = &iap,
                                 .non_interactive = true};
  app_error err = quick_op_serve_install(&req, NULL, NULL, &result);
  bool ok = err == APP_ERROR_MISSING_ARG &&
            result.failure_phase == QUICK_INSTALL_PHASE_LOCAL_PREFLIGHT &&
            !result.mutation_started;
  quick_install_result_destroy(&result);
  return ok;
}

static bool test_install_public_none_requires_explicit_override(void) {
#ifndef _WIN32
  quick_iap_config_t iap = {.type = (char *)"none"};
  quick_install_request_t req = {
      .host = "quick@box",
      .remote_root = "/srv/quick",
      .domain = "example.com",
      .iap = &iap,
      .non_interactive = true,
  };
  quick_install_result_t result;
  quick_install_result_init(&result);
  app_error err = quick_op_serve_install(&req, NULL, NULL, &result);
  bool ok = err == APP_ERROR_VALIDATION &&
            result.failure_phase == QUICK_INSTALL_PHASE_LOCAL_PREFLIGHT &&
            result.failure_message && strstr(result.failure_message, "loopback") &&
            result.remediation &&
            strstr(result.remediation, "--allow-public-unsafe") &&
            strstr(result.remediation, "TUI override") &&
            !result.mutation_started && !result.backup_created;
  quick_install_result_destroy(&result);
  if (!ok) {
    return false;
  }

  char empty_path[] = "/tmp/openquick-install-public-bin-XXXXXX";
  if (!make_temp_dir(empty_path)) {
    return false;
  }
  const char *old_path_env = getenv("PATH");
  const char *old_quickd_env = getenv("QUICK_QUICKD");
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  char *old_quickd = old_quickd_env ? strdup(old_quickd_env) : NULL;
  if ((old_path_env && !old_path) || (old_quickd_env && !old_quickd)) {
    free(old_path);
    free(old_quickd);
    rmdir(empty_path);
    return false;
  }
  setenv("PATH", empty_path, 1);
  unsetenv("QUICK_QUICKD");
  req.allow_public_unsafe = true;
  quick_install_result_init(&result);
  err = quick_op_serve_install(&req, NULL, NULL, &result);
  ok = err != APP_ERROR_VALIDATION &&
       (!result.failure_message || !strstr(result.failure_message, "loopback"));
  quick_install_result_destroy(&result);
  if (old_path_env) {
    setenv("PATH", old_path, 1);
  } else {
    unsetenv("PATH");
  }
  if (old_quickd_env) {
    setenv("QUICK_QUICKD", old_quickd, 1);
  } else {
    unsetenv("QUICK_QUICKD");
  }
  free(old_path);
  free(old_quickd);
  rmdir(empty_path);
  return ok;
#else
  return true;
#endif
}

static bool test_install_preflight_requires_rsync(void) {
#ifndef _WIN32
  char bin_dir[] = "/tmp/openquick-install-rsync-bin-XXXXXX";
  if (!make_temp_dir(bin_dir)) {
    return false;
  }
  char ssh_path[256], scp_path[256], quickd_path[256];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(scp_path, sizeof(scp_path), "%s/scp", bin_dir);
  snprintf(quickd_path, sizeof(quickd_path), "%s/quickd", bin_dir);
  if (!write_executable_file(ssh_path, "#!/bin/sh\nexit 0\n") ||
      !write_executable_file(scp_path, "#!/bin/sh\nexit 0\n") ||
      !write_executable_file(quickd_path, "#!/bin/sh\nexit 0\n")) {
    unlink(ssh_path);
    unlink(scp_path);
    unlink(quickd_path);
    rmdir(bin_dir);
    return false;
  }
  const char *old_path_env = getenv("PATH");
  const char *old_quickd_env = getenv("QUICK_QUICKD");
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  char *old_quickd = old_quickd_env ? strdup(old_quickd_env) : NULL;
  if ((old_path_env && !old_path) || (old_quickd_env && !old_quickd)) {
    free(old_path);
    free(old_quickd);
    unlink(ssh_path);
    unlink(scp_path);
    unlink(quickd_path);
    rmdir(bin_dir);
    return false;
  }
  setenv("PATH", bin_dir, 1);
  setenv("QUICK_QUICKD", quickd_path, 1);
  quick_iap_config_t iap = {.type = (char *)"tailscale"};
  quick_install_request_t req = {
      .host = "quick@box",
      .remote_root = "/srv/quick",
      .domain = "quick.example.com",
      .iap = &iap,
      .non_interactive = true,
  };
  quick_install_result_t result;
  quick_install_result_init(&result);
  app_error err = quick_op_serve_install(&req, NULL, NULL, &result);
  bool ok = err == APP_ERROR_NOT_FOUND &&
            result.failure_phase == QUICK_INSTALL_PHASE_LOCAL_PREFLIGHT &&
            result.failure_message && strstr(result.failure_message, "rsync") &&
            !result.mutation_started;
  quick_install_result_destroy(&result);
  if (old_path_env) {
    setenv("PATH", old_path, 1);
  } else {
    unsetenv("PATH");
  }
  if (old_quickd_env) {
    setenv("QUICK_QUICKD", old_quickd, 1);
  } else {
    unsetenv("QUICK_QUICKD");
  }
  free(old_path);
  free(old_quickd);
  unlink(ssh_path);
  unlink(scp_path);
  unlink(quickd_path);
  rmdir(bin_dir);
  return ok;
#else
  return true;
#endif
}

static bool test_install_cancel_before_start(void) {
  volatile sig_atomic_t cancel = 1;
  quick_iap_config_t iap = {.type = (char *)"tailscale"};
  quick_install_request_t req = {
      .host = "quick@box",
      .remote_root = "/srv/quick",
      .domain = "quick.example.com",
      .iap = &iap,
      .non_interactive = true,
      .cancel_flag = &cancel,
  };
  quick_install_result_t result;
  quick_install_result_init(&result);
  app_error err = quick_op_serve_install(&req, NULL, NULL, &result);
  bool ok = err == APP_ERROR_INTERRUPTED && result.cancelled &&
            result.failure_phase == QUICK_INSTALL_PHASE_LOCAL_PREFLIGHT &&
            !result.mutation_started && !result.rollback_attempted;
  quick_install_result_destroy(&result);
  return ok;
}

#ifndef _WIN32
typedef enum {
  INSTALL_FAKE_SUCCESS = 0,
  INSTALL_FAKE_SERVICE_FAILURE,
  INSTALL_FAKE_CANCEL_USER_SETUP,
} install_fake_mode_t;

typedef struct {
  quick_install_phase_t phases[64];
  size_t count;
} install_progress_trace_t;

static volatile sig_atomic_t install_alarm_cancel;

static void install_alarm_handler(int signum) {
  (void)signum;
  install_alarm_cancel = 1;
}

static void install_trace_progress(quick_install_phase_t phase,
                                   quick_stream_kind_t stream,
                                   const char *line, void *userdata) {
  (void)stream;
  (void)line;
  install_progress_trace_t *trace = userdata;
  if (trace && trace->count < sizeof(trace->phases) / sizeof(trace->phases[0])) {
    trace->phases[trace->count++] = phase;
  }
}

static bool check_fake_host_install(install_fake_mode_t mode,
                                    int connect_timeout_seconds) {
  char bin_dir[] = "/tmp/openquick-install-fake-bin-XXXXXX";
  if (!make_temp_dir(bin_dir)) {
    return false;
  }
  char ssh_path[256], scp_path[256], rsync_path[256], quickd_path[256];
  char ssh_log[256], scp_log[256], script_log[256];
  snprintf(ssh_path, sizeof(ssh_path), "%s/ssh", bin_dir);
  snprintf(scp_path, sizeof(scp_path), "%s/scp", bin_dir);
  snprintf(rsync_path, sizeof(rsync_path), "%s/rsync", bin_dir);
  snprintf(quickd_path, sizeof(quickd_path), "%s/quickd", bin_dir);
  snprintf(ssh_log, sizeof(ssh_log), "%s/ssh.log", bin_dir);
  snprintf(scp_log, sizeof(scp_log), "%s/scp.log", bin_dir);
  snprintf(script_log, sizeof(script_log), "%s/script.log", bin_dir);

  const char *ssh_script =
      "#!/bin/sh\n"
      "printf '%s\\n' \"$*\" >> \"$QUICK_TEST_INSTALL_SSH_LOG\"\n"
      "while [ \"$#\" -gt 0 ] && [ \"$1\" = -o ]; do\n"
      "  shift\n"
      "  [ \"$#\" -gt 0 ] && shift\n"
      "done\n"
      "[ \"$1\" = quick@box ] || exit 2\n"
      "shift\n"
      "case \"$*\" in\n"
      "  true) exit 0 ;;\n"
      "  'uname -s') printf '%s\\n' Linux; exit 0 ;;\n"
      "  'systemctl --version') printf '%s\\n' 'systemd 249'; exit 0 ;;\n"
      "  'sudo -n true') exit 0 ;;\n"
      "  'mktemp -d /tmp/openquick-install.XXXXXX')\n"
      "    printf '%s\\n' /tmp/openquick-install.Fake123; exit 0 ;;\n"
      "  sh\\ -s\\ --*)\n"
      "    kind=script\n"
      "    while IFS= read -r line; do\n"
      "      printf '%s\\n' \"$line\" >> \"$QUICK_TEST_INSTALL_SCRIPT_LOG\"\n"
      "      case \"$line\" in\n"
      "        '# openquick backup') kind=backup ;;\n"
      "        '# openquick rollback') kind=rollback ;;\n"
      "        '# openquick pre-mutation cleanup') kind=cleanup ;;\n"
      "      esac\n"
      "    done\n"
      "    printf 'EVENT:%s\\n' \"$kind\" >> \"$QUICK_TEST_INSTALL_SCRIPT_LOG\"\n"
      "    exit 0 ;;\n"
      "  'sudo groupadd --system --force quick-deploy')\n"
      "    if [ \"$QUICK_TEST_INSTALL_MODE\" = cancel-user ]; then\n"
      "      kill -ALRM \"$PPID\"\n"
      "      exec /bin/sleep 10\n"
      "    fi\n"
      "    exit 0 ;;\n"
      "  'id -un') printf '%s\\n' deployer; exit 0 ;;\n"
      "  'id -u quick') exit 0 ;;\n"
      "  'sudo systemctl enable openquick.service') exit 0 ;;\n"
      "  'sudo systemctl restart openquick.service')\n"
      "    if [ \"$QUICK_TEST_INSTALL_MODE\" = service-fail ]; then\n"
      "      printf '%s\\n' 'service start failed' >&2\n"
      "      exit 1\n"
      "    fi\n"
      "    exit 0 ;;\n"
      "  'sudo install -d -m 2770 -o quick -g quick-deploy /srv/quick')\n"
      "    exit 0 ;;\n"
      "  'quickd doctor --host --json')\n"
      "    printf '%s\\n' '{\"status\":\"ok\"}'; exit 0 ;;\n"
      "  sudo\\ tee*)\n"
      "    while IFS= read -r line; do :; done\n"
      "    exit 0 ;;\n"
      "  *) exit 0 ;;\n"
      "esac\n";
  const char *scp_script =
      "#!/bin/sh\n"
      "printf '%s\\n' \"$*\" >> \"$QUICK_TEST_INSTALL_SCP_LOG\"\n"
      "exit 0\n";
  if (!write_executable_file(ssh_path, ssh_script) ||
      !write_executable_file(scp_path, scp_script) ||
      !write_executable_file(rsync_path, "#!/bin/sh\nexit 0\n") ||
      !write_executable_file(quickd_path, "#!/bin/sh\nexit 0\n")) {
    unlink(ssh_path);
    unlink(scp_path);
    unlink(rsync_path);
    unlink(quickd_path);
    rmdir(bin_dir);
    return false;
  }

  const char *old_path_env = getenv("PATH");
  const char *old_quickd_env = getenv("QUICK_QUICKD");
  const char *old_ssh_log_env = getenv("QUICK_TEST_INSTALL_SSH_LOG");
  const char *old_scp_log_env = getenv("QUICK_TEST_INSTALL_SCP_LOG");
  const char *old_script_log_env = getenv("QUICK_TEST_INSTALL_SCRIPT_LOG");
  const char *old_mode_env = getenv("QUICK_TEST_INSTALL_MODE");
  const char *old_install_dir_env = getenv("QUICK_INSTALL_DIR");
  char *old_path = old_path_env ? strdup(old_path_env) : NULL;
  char *old_quickd = old_quickd_env ? strdup(old_quickd_env) : NULL;
  char *old_ssh_log = old_ssh_log_env ? strdup(old_ssh_log_env) : NULL;
  char *old_scp_log = old_scp_log_env ? strdup(old_scp_log_env) : NULL;
  char *old_script_log =
      old_script_log_env ? strdup(old_script_log_env) : NULL;
  char *old_mode = old_mode_env ? strdup(old_mode_env) : NULL;
  char *old_install_dir =
      old_install_dir_env ? strdup(old_install_dir_env) : NULL;
  if ((old_path_env && !old_path) || (old_quickd_env && !old_quickd) ||
      (old_ssh_log_env && !old_ssh_log) ||
      (old_scp_log_env && !old_scp_log) ||
      (old_script_log_env && !old_script_log) ||
      (old_mode_env && !old_mode) ||
      (old_install_dir_env && !old_install_dir)) {
    free(old_path);
    free(old_quickd);
    free(old_ssh_log);
    free(old_scp_log);
    free(old_script_log);
    free(old_mode);
    free(old_install_dir);
    unlink(ssh_path);
    unlink(scp_path);
    unlink(rsync_path);
    unlink(quickd_path);
    rmdir(bin_dir);
    return false;
  }

  setenv("PATH", bin_dir, 1);
  setenv("QUICK_QUICKD", quickd_path, 1);
  setenv("QUICK_TEST_INSTALL_SSH_LOG", ssh_log, 1);
  setenv("QUICK_TEST_INSTALL_SCP_LOG", scp_log, 1);
  setenv("QUICK_TEST_INSTALL_SCRIPT_LOG", script_log, 1);
  setenv("QUICK_INSTALL_DIR", "install", 1);
  if (mode == INSTALL_FAKE_SERVICE_FAILURE) {
    setenv("QUICK_TEST_INSTALL_MODE", "service-fail", 1);
  } else if (mode == INSTALL_FAKE_CANCEL_USER_SETUP) {
    setenv("QUICK_TEST_INSTALL_MODE", "cancel-user", 1);
  } else {
    setenv("QUICK_TEST_INSTALL_MODE", "success", 1);
  }

  struct sigaction old_alarm_action = {0};
  bool signal_handler_installed = false;
  bool ok = false;
  install_alarm_cancel = 0;
  if (mode == INSTALL_FAKE_CANCEL_USER_SETUP) {
    struct sigaction action = {0};
    action.sa_handler = install_alarm_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, &old_alarm_action) != 0) {
      goto cleanup;
    }
    signal_handler_installed = true;
  }

  quick_iap_config_t iap = {.type = (char *)"tailscale"};
  quick_install_request_t req = {
      .host = "quick@box",
      .remote_root = "/srv/quick",
      .domain = "quick.example.com",
      .iap = &iap,
      .non_interactive = true,
      .cancel_flag = mode == INSTALL_FAKE_CANCEL_USER_SETUP
                         ? &install_alarm_cancel
                         : NULL,
      .connect_timeout_seconds = connect_timeout_seconds,
  };
  quick_install_result_t result;
  quick_install_result_init(&result);
  install_progress_trace_t trace = {0};
  app_error err = quick_op_serve_install(&req, install_trace_progress, &trace,
                                         &result);
  if (signal_handler_installed) {
    (void)sigaction(SIGALRM, &old_alarm_action, NULL);
    signal_handler_installed = false;
  }

  const char *expected_timeout =
      connect_timeout_seconds > 120 ? "ConnectTimeout=120"
                                    : "ConnectTimeout=10";
  char expected_ssh[192];
  snprintf(expected_ssh, sizeof(expected_ssh),
           "-o BatchMode=yes -o %s -o ConnectionAttempts=1 quick@box true",
           expected_timeout);
  char expected_scp[320];
  snprintf(expected_scp, sizeof(expected_scp),
           "-o BatchMode=yes -o %s -o ConnectionAttempts=1 %s "
           "quick@box:/tmp/openquick-install.Fake123/quickd",
           expected_timeout, quickd_path);

  size_t backup_index = trace.count;
  size_t user_index = trace.count;
  for (size_t i = 0; i < trace.count; i++) {
    if (backup_index == trace.count &&
        trace.phases[i] == QUICK_INSTALL_PHASE_BACKUP) {
      backup_index = i;
    }
    if (user_index == trace.count &&
        trace.phases[i] == QUICK_INSTALL_PHASE_USER_SETUP) {
      user_index = i;
    }
  }
  ok = backup_index < user_index && file_contains(ssh_log, expected_ssh) &&
       file_contains(script_log, "quickd.absent") &&
       file_contains(script_log, "root-quickd.json.absent") &&
       file_contains(script_log, "unit.was-enabled") &&
       file_contains(script_log, "EVENT:backup");
  if (mode == INSTALL_FAKE_SUCCESS) {
    ok = ok && file_contains(scp_log, expected_scp) &&
         file_contains(ssh_log,
                       "sudo install -d -m 2770 -o quick -g quick-deploy "
                       "/srv/quick") &&
         file_contains(ssh_log,
                       "sudo systemctl enable openquick.service") &&
         file_contains(ssh_log,
                       "sudo systemctl restart openquick.service") &&
         err == APP_SUCCESS && result.completed && result.doctor_ran &&
         result.doctor_ok && result.backup_created && result.backup_path &&
         !result.rollback_attempted && !result.partial_cleanup_remains;
  } else if (mode == INSTALL_FAKE_SERVICE_FAILURE) {
    ok = ok && file_contains(scp_log, expected_scp) && err == APP_ERROR_IO &&
         result.failure_phase == QUICK_INSTALL_PHASE_SERVICE_START &&
         result.mutation_started && result.backup_created &&
         result.rollback_attempted && result.rollback_ok &&
         result.partial_cleanup_remains && result.cleanup_detail &&
         strstr(result.cleanup_detail, "quick-deploy group") &&
         strstr(result.cleanup_detail, "backup at") &&
         file_contains(script_log, "EVENT:rollback") &&
         file_contains(script_log, "openquick.service.absent") &&
         file_contains(script_log, "daemon-reload");
  } else {
    ok = ok && err == APP_ERROR_INTERRUPTED && result.cancelled &&
         result.failure_phase == QUICK_INSTALL_PHASE_USER_SETUP &&
         result.mutation_started && result.backup_created &&
         result.rollback_attempted && result.rollback_ok &&
         result.partial_cleanup_remains && result.cleanup_detail &&
         file_contains(script_log, "EVENT:rollback");
  }
  quick_install_result_destroy(&result);

cleanup:
  if (signal_handler_installed) {
    (void)sigaction(SIGALRM, &old_alarm_action, NULL);
  }
  if (old_path_env) {
    setenv("PATH", old_path, 1);
  } else {
    unsetenv("PATH");
  }
  if (old_quickd_env) {
    setenv("QUICK_QUICKD", old_quickd, 1);
  } else {
    unsetenv("QUICK_QUICKD");
  }
  if (old_ssh_log_env) {
    setenv("QUICK_TEST_INSTALL_SSH_LOG", old_ssh_log, 1);
  } else {
    unsetenv("QUICK_TEST_INSTALL_SSH_LOG");
  }
  if (old_scp_log_env) {
    setenv("QUICK_TEST_INSTALL_SCP_LOG", old_scp_log, 1);
  } else {
    unsetenv("QUICK_TEST_INSTALL_SCP_LOG");
  }
  if (old_script_log_env) {
    setenv("QUICK_TEST_INSTALL_SCRIPT_LOG", old_script_log, 1);
  } else {
    unsetenv("QUICK_TEST_INSTALL_SCRIPT_LOG");
  }
  if (old_mode_env) {
    setenv("QUICK_TEST_INSTALL_MODE", old_mode, 1);
  } else {
    unsetenv("QUICK_TEST_INSTALL_MODE");
  }
  if (old_install_dir_env) {
    setenv("QUICK_INSTALL_DIR", old_install_dir, 1);
  } else {
    unsetenv("QUICK_INSTALL_DIR");
  }
  free(old_path);
  free(old_quickd);
  free(old_ssh_log);
  free(old_scp_log);
  free(old_script_log);
  free(old_mode);
  free(old_install_dir);
  unlink(ssh_log);
  unlink(scp_log);
  unlink(script_log);
  unlink(ssh_path);
  unlink(scp_path);
  unlink(rsync_path);
  unlink(quickd_path);
  rmdir(bin_dir);
  return ok;
}
#endif

static bool test_install_ssh_scp_options_and_backup_order(void) {
#ifndef _WIN32
  return check_fake_host_install(INSTALL_FAKE_SUCCESS, 0);
#else
  return true;
#endif
}

static bool test_install_service_failure_rolls_back(void) {
#ifndef _WIN32
  return check_fake_host_install(INSTALL_FAKE_SERVICE_FAILURE, 999);
#else
  return true;
#endif
}

static bool test_install_cancel_after_mutation_rolls_back(void) {
#ifndef _WIN32
  return check_fake_host_install(INSTALL_FAKE_CANCEL_USER_SETUP, 10);
#else
  return true;
#endif
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
  unit_record(
      stats, test_safe_remote_install_validation(),
      "remote installer validation rejects unsafe argv-over-ssh values");
  unit_record(stats, test_domain_is_loopback(),
              "loopback domain helper matches localhost install semantics");
  unit_record(stats, test_restore_and_rollback_input_validation(),
              "restore and rollback validation rejects unsafe remote values");
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
  unit_record(stats,
              test_doctor_noninteractive_ssh_argv_and_timeout_bounds(),
              "doctor bounds non-interactive SSH timeout argv options");
  unit_record(stats, test_doctor_interactive_ssh_argv_is_unchanged(),
              "doctor preserves interactive SSH argv construction");
  unit_record(stats, test_doctor_cancelled_before_remote_ssh(),
              "doctor cancellation prevents remote SSH spawn");
  unit_record(stats, test_doctor_deep_rsync_argv_and_timeout_bounds(),
              "deep doctor applies bounded non-interactive rsync SSH argv");
  unit_record(stats, test_doctor_deep_rsync_cancellation_is_interrupted(),
              "deep doctor reports rsync cancellation as interrupted");
  unit_record(stats, test_doctor_deep_activation_cancel_cleans_remote_site(),
              "deep doctor cleans the remote site after activation cancel");
  unit_record(stats, test_doctor_deep_precancel_skips_rsync_spawn(),
              "pre-cancelled deep doctor does not spawn rsync");
  unit_record(stats, test_doctor_private_identity_redirect_is_warning(),
              "doctor treats private identity redirects as warning");
  unit_record(stats, test_doctor_malformed_identity_login_field_is_failure(),
              "doctor fails malformed identity JSON with login field");
  unit_record(stats, test_tui_product_model_helpers(),
              "TUI product model formats rows and validates profile fields");
  unit_record(stats, test_onboarding_init_reset_and_return_destination(),
              "onboarding model initializes and resets owned state");
  unit_record(stats, test_onboarding_local_adopt_back_cancel_retry(),
              "onboarding model handles local and adopt navigation");
  unit_record(stats, test_onboarding_connect_and_install_transitions(),
              "onboarding model handles connect and install flows");
  unit_record(stats, test_onboarding_welcome_and_local_failure_semantics(),
              "onboarding model closes welcome and records local failures");
  unit_record(stats, test_onboarding_invalid_transition_is_immutable(),
              "invalid onboarding transitions do not mutate state");
  unit_record(stats, test_onboarding_checks_and_validation(),
              "onboarding model records checks and validation");
  unit_record(stats, test_onboarding_install_result_copy_and_states(),
              "onboarding model copies install results without aliases");
  unit_record(stats, test_onboarding_state_labels(),
              "onboarding model labels every state");
  unit_record(stats, test_mint_dev_token_parses_json(),
              "serve dev op parses minted remote API token JSON");
  unit_record(stats, test_serve_install_steps_structure(),
              "serve op exposes guided install steps");
  unit_record(stats, test_iap_product_model(),
              "IAP product model classifies types and default modes");
  unit_record(stats, test_serve_local_url(),
              "serve local URL follows quickd routing defaults");
  unit_record(stats, test_context_classify_no_project(),
              "onboarding context classifies an empty folder");
  unit_record(stats, test_context_classify_valid_project(),
              "onboarding context classifies a valid project");
  unit_record(stats, test_context_classify_malformed_project(),
              "onboarding context classifies a malformed project");
  unit_record(stats, test_context_classify_adoptable_folder(),
              "onboarding context marks an adoptable folder");
  unit_record(stats, test_context_classify_profile_missing(),
              "onboarding context detects a missing project profile");
  unit_record(stats, test_install_phase_labels(),
              "host install phase labels describe progress");
  unit_record(stats, test_install_result_lifecycle(),
              "host install result lifecycle resets owned state");
  unit_record(stats, test_install_missing_host_returns_error(),
              "host install rejects a missing SSH host before preflight");
  unit_record(stats, test_install_public_none_requires_explicit_override(),
              "host install rejects public iap=none without an override");
  unit_record(stats, test_install_preflight_requires_rsync(),
              "host install preflight requires local rsync");
  unit_record(stats, test_install_cancel_before_start(),
              "host install honors cancellation before local preflight");
  unit_record(stats, test_install_ssh_scp_options_and_backup_order(),
              "host install hardens ssh/scp argv and backs up before mutation");
  unit_record(stats, test_install_service_failure_rolls_back(),
              "service-start failure rolls back from the retained backup");
  unit_record(stats, test_install_cancel_after_mutation_rolls_back(),
              "mid-install cancellation rolls back host file mutations");
}
