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

static char *serve_find_quickd(void) {
  const char *override = getenv("QUICK_QUICKD");
  if (override && override[0] != '\0') {
    return quick_path_exists_cli(override) ? strdup(override) : NULL;
  }
  char *path = quick_find_executable_cli("quickd");
  if (path) {
    return path;
  }
  const char *candidates[] = {"zig-out/bin/quickd", "server/quickd",
                              "server/cmd/quickd/quickd"};
  for (size_t i = 0; i < APP_COUNTOF(candidates); i++) {
    if (quick_path_exists_cli(candidates[i])) {
      return strdup(candidates[i]);
    }
  }
  return NULL;
}

static app_error serve_read_systemd_unit(char **out) {
  if (!out) {
    return APP_ERROR_INVALID_ARG;
  }
  *out = NULL;
  const char *override = getenv("QUICK_INSTALL_DIR");
  if (override && override[0] != '\0') {
    char *path = quick_path_join_cli(override, "systemd/openquick.service");
    if (!path) {
      return APP_ERROR_MEMORY;
    }
    app_error err = quick_read_file_cli(path, out);
    free(path);
    if (err == APP_SUCCESS) {
      return APP_SUCCESS;
    }
  }

  const char *candidates[] = {
      "install/systemd/openquick.service",
      "/usr/local/share/openquick/install/systemd/openquick.service",
  };
  for (size_t i = 0; i < APP_COUNTOF(candidates); i++) {
    app_error err = quick_read_file_cli(candidates[i], out);
    if (err == APP_SUCCESS) {
      return APP_SUCCESS;
    }
  }
  return APP_ERROR_NOT_FOUND;
}

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
    quick_print_error(config,
                      "quickd not found; set QUICK_QUICKD or install quickd on PATH");
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

static bool serve_iap_is_tailscale(const char *iap) {
  return iap &&
         (strcmp(iap, "tailscale") == 0 ||
          strcmp(iap, "tailscale-localapi") == 0 ||
          strcmp(iap, "tailscale-serve") == 0 ||
          strcmp(iap, "tailscale-tsnet") == 0);
}

static bool serve_iap_is_cloudflare(const char *iap) {
  return iap &&
         (strcmp(iap, "cloudflare") == 0 ||
          strcmp(iap, "cloudflare-access") == 0);
}

static bool serve_iap_is_allowed(const char *iap) {
  return serve_iap_is_tailscale(iap) || serve_iap_is_cloudflare(iap) ||
         (iap && strcmp(iap, "none") == 0);
}

static const char *serve_default_iap_mode(const char *iap) {
  if (!iap || strcmp(iap, "none") == 0) return "";
  if (serve_iap_is_cloudflare(iap)) return "access";
  if (strcmp(iap, "tailscale-serve") == 0) return "serve";
  if (strcmp(iap, "tailscale-tsnet") == 0) return "tsnet";
  if (serve_iap_is_tailscale(iap)) return "localapi";
  return "";
}

static app_error serve_validate_install_inputs(const app_config_t *config,
                                               const char *profile_name,
                                               const char *host,
                                               const char *remote_root,
                                               const char *domain,
                                               const char *iap) {
  if (!quick_profile_name_is_safe(profile_name)) {
    quick_print_error(config, "profile contains unsafe characters");
    return APP_ERROR_VALIDATION;
  }
  if (host && !quick_ssh_target_is_safe(host)) {
    quick_print_error(config, "SSH host contains unsafe characters");
    return APP_ERROR_VALIDATION;
  }
  if (!quick_remote_path_is_safe(remote_root)) {
    quick_print_error(config, "remote root must be an absolute safe path without shell metacharacters");
    return APP_ERROR_VALIDATION;
  }
  if (domain && !quick_domain_is_safe(domain)) {
    quick_print_error(config, "domain must be a DNS name without shell metacharacters");
    return APP_ERROR_VALIDATION;
  }
  if (!serve_iap_is_allowed(iap)) {
    quick_print_error(config,
                      "iap must be tailscale, tailscale-localapi, tailscale-serve, tailscale-tsnet, cloudflare, cloudflare-access, or none");
    return APP_ERROR_VALIDATION;
  }
  return APP_SUCCESS;
}

static char *serve_replace_all(const char *input, const char *needle,
                               const char *replacement) {
  if (!input || !needle || !replacement || needle[0] == '\0') {
    return NULL;
  }
  const size_t input_len = strlen(input);
  const size_t needle_len = strlen(needle);
  const size_t repl_len = strlen(replacement);
  size_t count = 0;
  for (const char *p = strstr(input, needle); p; p = strstr(p + needle_len, needle)) {
    count++;
  }
  size_t out_len = input_len + count * (repl_len > needle_len ? repl_len - needle_len : 0U);
  if (needle_len > repl_len) {
    out_len = input_len - count * (needle_len - repl_len);
  }
  char *out = malloc(out_len + 1U);
  if (!out) {
    return NULL;
  }
  char *dst = out;
  const char *src = input;
  const char *match = NULL;
  while ((match = strstr(src, needle)) != NULL) {
    size_t n = (size_t)(match - src);
    memcpy(dst, src, n);
    dst += n;
    memcpy(dst, replacement, repl_len);
    dst += repl_len;
    src = match + needle_len;
  }
  strcpy(dst, src);
  return out;
}

static char *serve_json_string(const char *value) {
  const char *v = value ? value : "";
  size_t len = 2U;
  for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
    len += (*p < 0x20 || *p == '"' || *p == '\\') ? 6U : 1U;
  }
  char *out = malloc(len + 1U);
  if (!out) {
    return NULL;
  }
  char *dst = out;
  *dst++ = '"';
  for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
    if (*p == '"' || *p == '\\') {
      *dst++ = '\\';
      *dst++ = (char)*p;
    } else if (*p < 0x20) {
      snprintf(dst, 7U, "\\u%04x", *p);
      dst += 6;
    } else {
      *dst++ = (char)*p;
    }
  }
  *dst++ = '"';
  *dst = '\0';
  return out;
}

static char *serve_cloudflare_jwks_url(const char *team_domain) {
  if (!team_domain || team_domain[0] == '\0') {
    return NULL;
  }
  const size_t base_len = strlen(team_domain);
  size_t trimmed = base_len;
  while (trimmed > 0 && team_domain[trimmed - 1U] == '/') {
    trimmed--;
  }
  const char *suffix = "/cdn-cgi/access/certs";
  char *out = malloc(trimmed + strlen(suffix) + 1U);
  if (!out) {
    return NULL;
  }
  memcpy(out, team_domain, trimmed);
  strcpy(out + trimmed, suffix);
  return out;
}

static char *serve_iap_extra_json(const quick_iap_config_t *iap_config) {
  if (!iap_config || !serve_iap_is_cloudflare(iap_config->type)) {
    return strdup("");
  }
  char *team = serve_json_string(iap_config->team_domain);
  char *audience = serve_json_string(iap_config->audience);
  char *jwks_url = serve_cloudflare_jwks_url(iap_config->team_domain);
  char *jwks = serve_json_string(jwks_url ? jwks_url : "");
  free(jwks_url);
  if (!team || !audience || !jwks) {
    free(team);
    free(audience);
    free(jwks);
    return NULL;
  }
  const size_t len = strlen(team) + strlen(audience) + strlen(jwks) + 128U;
  char *out = malloc(len);
  if (!out) {
    free(team);
    free(audience);
    free(jwks);
    return NULL;
  }
  snprintf(out, len,
           ",\n"
           "    \"team_domain\": %s,\n"
           "    \"audience\": %s,\n"
           "    \"jwks_url\": %s",
           team, audience, jwks);
  free(team);
  free(audience);
  free(jwks);
  return out;
}

static char *serve_host_config_json(const char *remote_root, const char *domain,
                                    const quick_iap_config_t *iap_config) {
  const char *iap = iap_config && iap_config->type ? iap_config->type : "tailscale";
  const char *public_domain = domain ? domain : "";
  const char *mode = iap_config && iap_config->mode && iap_config->mode[0]
                         ? iap_config->mode
                         : serve_default_iap_mode(iap);
  const char *require_identity = strcmp(iap, "none") == 0 ? "false" : "true";
  const char *allow_anonymous = strcmp(iap, "none") == 0 ? "true" : "false";
  char *data_dir = quick_path_join_cli(remote_root, "data");
  if (!data_dir) {
    return NULL;
  }
  char *public_domain_json = serve_json_string(public_domain);
  char *remote_root_json = serve_json_string(remote_root);
  char *data_dir_json = serve_json_string(data_dir);
  char *iap_json = serve_json_string(iap);
  char *mode_json = serve_json_string(mode);
  char *iap_extra = serve_iap_extra_json(iap_config);
  free(data_dir);
  if (!public_domain_json || !remote_root_json || !data_dir_json || !iap_json ||
      !mode_json || !iap_extra) {
    free(public_domain_json);
    free(remote_root_json);
    free(data_dir_json);
    free(iap_json);
    free(mode_json);
    free(iap_extra);
    return NULL;
  }
  const size_t len = strlen(remote_root_json) + strlen(data_dir_json) +
                     strlen(public_domain_json) + strlen(iap_json) +
                     strlen(mode_json) + strlen(iap_extra) + 2048U;
  char *json = malloc(len);
  if (!json) {
    free(public_domain_json);
    free(remote_root_json);
    free(data_dir_json);
    free(iap_json);
    free(mode_json);
    free(iap_extra);
    return NULL;
  }
  snprintf(json, len,
           "{\n"
           "  \"$schema\": \"https://openquick.dev/schemas/host.v1.json\",\n"
           "  \"listen\": \"127.0.0.1:9366\",\n"
           "  \"public_base_domain\": %s,\n"
           "  \"remote_root\": %s,\n"
           "  \"data_dir\": %s,\n"
           "  \"retained_releases\": 10,\n"
           "  \"max_upload_bytes\": 104857600,\n"
           "  \"iap\": {\n"
           "    \"type\": %s,\n"
           "    \"mode\": %s,\n"
           "    \"trusted_proxies\": [\"127.0.0.1/32\"],\n"
           "    \"source_ip_header\": \"X-Forwarded-For\"%s\n"
           "  },\n"
           "  \"deploy\": {\n"
           "    \"policy\": \"any_ssh_deployer\",\n"
           "    \"reserved_names\": [\"api\", \"admin\", \"www\", \"_quick\"]\n"
           "  },\n"
           "  \"viewer\": {\n"
           "    \"require_identity\": %s,\n"
           "    \"allow_anonymous\": %s\n"
           "  }\n"
           "}\n",
           public_domain_json, remote_root_json, data_dir_json, iap_json,
           mode_json, iap_extra, require_identity, allow_anonymous);
  free(public_domain_json);
  free(remote_root_json);
  free(data_dir_json);
  free(iap_json);
  free(mode_json);
  free(iap_extra);
  return json;
}

static app_error serve_write_profile(const char *profile_name, const char *host,
                                     const char *remote_root,
                                     const char *domain, const char *iap) {
  quick_profile_config_t profiles;
  quick_profile_config_init(&profiles);
  (void)quick_profile_config_load_default(&profiles);
  quick_profile_t *profile = quick_profile_config_upsert(&profiles, profile_name);
  if (!profile) {
    quick_profile_config_destroy(&profiles);
    return APP_ERROR_MEMORY;
  }
  if (host) {
    free(profile->ssh);
    profile->ssh = strdup(host);
  }
  free(profile->remote_root);
  profile->remote_root = strdup(remote_root);
  if (domain) {
    free(profile->base_domain);
    profile->base_domain = strdup(domain);
  }
  free(profile->iap.type);
  profile->iap.type = strdup(iap);
  if (!profiles.default_profile) {
    profiles.default_profile = strdup(profile_name);
  }
  char *path = quick_profile_config_default_path();
  app_error err = APP_SUCCESS;
  if (path) {
    err = quick_profile_config_write_file(path, &profiles);
    free(path);
  }
  quick_profile_config_destroy(&profiles);
  return err;
}

static app_error serve_ssh_capture(const char *host, char *const remote_argv[],
                                   const char *stdin_text,
                                   quick_process_result_t *res) {
  size_t remote_count = 0;
  while (remote_argv[remote_count]) {
    remote_count++;
  }
  char **argv = calloc(remote_count + 3U, sizeof(char *));
  if (!argv) {
    return APP_ERROR_MEMORY;
  }
  argv[0] = "ssh";
  argv[1] = (char *)host;
  for (size_t i = 0; i < remote_count; i++) {
    argv[i + 2U] = remote_argv[i];
  }
  argv[remote_count + 2U] = NULL;
  app_error err = quick_process_capture_input(argv, NULL, stdin_text, res);
  free(argv);
  return err;
}

static app_error serve_ssh_expect(const app_config_t *config, const char *host,
                                  char *const remote_argv[]) {
  quick_process_result_t res = {0};
  app_error err = serve_ssh_capture(host, remote_argv, NULL, &res);
  if (err != APP_SUCCESS || res.exit_code != 0) {
    quick_print_error(config, res.err && res.err[0] ? res.err : "remote install command failed");
    quick_process_result_destroy(&res);
    return err == APP_SUCCESS ? APP_ERROR_IO : err;
  }
  quick_process_result_destroy(&res);
  return APP_SUCCESS;
}

static app_error serve_ssh_tee(const app_config_t *config, const char *host,
                               const char *remote_path, const char *content) {
  char *const argv[] = {"sudo", "tee", (char *)remote_path, NULL};
  quick_process_result_t res = {0};
  app_error err = serve_ssh_capture(host, argv, content, &res);
  if (err != APP_SUCCESS || res.exit_code != 0) {
    quick_print_error(config, res.err && res.err[0] ? res.err : "remote tee failed");
    quick_process_result_destroy(&res);
    return err == APP_SUCCESS ? APP_ERROR_IO : err;
  }
  quick_process_result_destroy(&res);
  return APP_SUCCESS;
}

static char *serve_trimmed_copy(const char *value) {
  if (!value) return NULL;
  const char *start = value;
  while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
  const char *end = start + strlen(start);
  while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
  size_t len = (size_t)(end - start);
  char *copy = malloc(len + 1U);
  if (!copy) return NULL;
  memcpy(copy, start, len);
  copy[len] = '\0';
  return copy;
}

static bool serve_remote_user_is_safe(const char *user) {
  if (!user || user[0] == '\0' || user[0] == '-') return false;
  for (const unsigned char *p = (const unsigned char *)user; *p; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
        (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.') {
      continue;
    }
    return false;
  }
  return true;
}

static app_error serve_remote_mktemp_dir(const app_config_t *config,
                                         const char *host, char **out) {
  *out = NULL;
  char *const argv[] = {"mktemp", "-d", "/tmp/openquick-install.XXXXXX", NULL};
  quick_process_result_t res = {0};
  app_error err = serve_ssh_capture(host, argv, NULL, &res);
  if (err != APP_SUCCESS || res.exit_code != 0) {
    quick_print_error(config, res.err && res.err[0] ? res.err : "remote mktemp failed");
    quick_process_result_destroy(&res);
    return err == APP_SUCCESS ? APP_ERROR_IO : err;
  }
  char *path = serve_trimmed_copy(res.out);
  quick_process_result_destroy(&res);
  if (!path || path[0] == '\0') {
    free(path);
    quick_print_error(config, "remote mktemp returned an empty path");
    return APP_ERROR_IO;
  }
  *out = path;
  return APP_SUCCESS;
}

static app_error serve_install_execute(const app_config_t *config,
                                       const char *profile_name,
                                       const char *host,
                                       const char *remote_root,
                                       const char *domain,
                                       const quick_iap_config_t *iap_config) {
  if (!host) {
    quick_print_error(config, "--execute requires --host");
    return APP_ERROR_MISSING_ARG;
  }
  char *quickd = serve_find_quickd();
  if (!quickd) {
    quick_print_error(config,
                      "quickd not found; set QUICK_QUICKD or install quickd on PATH before --execute");
    return APP_ERROR_NOT_FOUND;
  }
  char *scp = quick_find_executable_cli("scp");
  if (!scp) {
    free(quickd);
    quick_print_error(config, "scp not found; install OpenSSH scp or put it on PATH");
    return APP_ERROR_NOT_FOUND;
  }
  free(scp);

  const char *iap = iap_config && iap_config->type ? iap_config->type : "tailscale";
  char *host_json = serve_host_config_json(remote_root, domain, iap_config);
  char *unit = NULL;
  app_error err = host_json ? serve_read_systemd_unit(&unit) : APP_ERROR_MEMORY;
  if (err != APP_SUCCESS) {
    free(host_json);
    free(quickd);
    quick_print_error(config, "failed to read openquick.service install asset");
    return err;
  }
  char *unit_for_root = strcmp(remote_root, "/srv/quick") == 0
                            ? strdup(unit)
                            : serve_replace_all(unit, "/srv/quick", remote_root);
  free(unit);
  if (!unit_for_root) {
    free(host_json);
    free(quickd);
    return APP_ERROR_MEMORY;
  }

  char *remote_tmp_dir = NULL;
  char *tmp_remote = NULL;
  char *scp_dest = NULL;
  char *sites_dir = NULL;
  char *data_dir = NULL;
  char *uploads_dir = NULL;
  char *logs_dir = NULL;
  char *config_dir = NULL;
  char *backup_dir = NULL;
  char *remote_user = NULL;
  bool backup_created = false;

  err = serve_remote_mktemp_dir(config, host, &remote_tmp_dir);
  if (err != APP_SUCCESS) goto done;
  tmp_remote = quick_path_join_cli(remote_tmp_dir, "quickd");
  backup_dir = quick_path_join_cli(remote_tmp_dir, "backup");
  if (!tmp_remote || !backup_dir) {
    err = APP_ERROR_MEMORY;
    goto done;
  }
  scp_dest = malloc(strlen(host) + strlen(tmp_remote) + 2U);
  if (!scp_dest) {
    err = APP_ERROR_MEMORY;
    goto done;
  }
  sprintf(scp_dest, "%s:%s", host, tmp_remote);

  char *const groupadd[] = {"sudo", "groupadd", "--system", "--force", "quick-deploy", NULL};
  err = serve_ssh_expect(config, host, groupadd);
  if (err != APP_SUCCESS) goto done;

  char *const id_user[] = {"id", "-un", NULL};
  quick_process_result_t user_res = {0};
  err = serve_ssh_capture(host, id_user, NULL, &user_res);
  if (err != APP_SUCCESS || user_res.exit_code != 0) {
    quick_print_error(config, user_res.err && user_res.err[0] ? user_res.err : "failed to identify SSH user");
    err = err == APP_SUCCESS ? APP_ERROR_IO : err;
    quick_process_result_destroy(&user_res);
    goto done;
  }
  remote_user = serve_trimmed_copy(user_res.out);
  quick_process_result_destroy(&user_res);
  if (!serve_remote_user_is_safe(remote_user)) {
    quick_print_error(config, "remote SSH user contains unsafe characters");
    err = APP_ERROR_VALIDATION;
    goto done;
  }

  char *const id_quick[] = {"id", "-u", "quick", NULL};
  quick_process_result_t id_res = {0};
  err = serve_ssh_capture(host, id_quick, NULL, &id_res);
  const bool quick_user_exists = err == APP_SUCCESS && id_res.exit_code == 0;
  quick_process_result_destroy(&id_res);
  if (err != APP_SUCCESS) goto done;
  if (!quick_user_exists) {
    char *const useradd[] = {"sudo", "useradd", "--system", "--home-dir",
                             (char *)remote_root, "--create-home", "--shell",
                             "/usr/sbin/nologin", "quick", NULL};
    err = serve_ssh_expect(config, host, useradd);
    if (err != APP_SUCCESS) goto done;
  }
  char *const usermod[] = {"sudo", "usermod", "-a", "-G", "quick-deploy", "quick", NULL};
  err = serve_ssh_expect(config, host, usermod);
  if (err != APP_SUCCESS) goto done;
  if (remote_user && strcmp(remote_user, "root") != 0 &&
      strcmp(remote_user, "quick") != 0) {
    char *const deployer_usermod[] = {"sudo", "usermod", "-a", "-G",
                                      "quick-deploy", remote_user, NULL};
    err = serve_ssh_expect(config, host, deployer_usermod);
    if (err != APP_SUCCESS) goto done;
  }

  char *const mkdir_root[] = {"sudo", "install", "-d", "-m", "2750", "-o", "quick", "-g", "quick-deploy", (char *)remote_root, NULL};
  err = serve_ssh_expect(config, host, mkdir_root);
  if (err != APP_SUCCESS) goto done;
  sites_dir = quick_path_join_cli(remote_root, "sites");
  data_dir = quick_path_join_cli(remote_root, "data");
  uploads_dir = quick_path_join_cli(remote_root, "uploads");
  logs_dir = quick_path_join_cli(remote_root, "logs");
  config_dir = quick_path_join_cli(remote_root, "config");
  if (!sites_dir || !data_dir || !uploads_dir || !logs_dir || !config_dir) {
    err = APP_ERROR_MEMORY;
    goto dirs_done;
  }
  char *const mkdir_sites[] = {"sudo", "install", "-d", "-m", "2770", "-o", "quick", "-g", "quick-deploy", sites_dir, NULL};
  err = serve_ssh_expect(config, host, mkdir_sites);
  if (err != APP_SUCCESS) goto dirs_done;
  char *const mkdir_data[] = {"sudo", "install", "-d", "-m", "2770", "-o", "quick", "-g", "quick-deploy", data_dir, NULL};
  err = serve_ssh_expect(config, host, mkdir_data);
  if (err != APP_SUCCESS) goto dirs_done;
  char *const mkdir_uploads[] = {"sudo", "install", "-d", "-m", "2770", "-o", "quick", "-g", "quick-deploy", uploads_dir, NULL};
  err = serve_ssh_expect(config, host, mkdir_uploads);
  if (err != APP_SUCCESS) goto dirs_done;
  char *const mkdir_logs[] = {"sudo", "install", "-d", "-m", "2750", "-o", "quick", "-g", "quick-deploy", logs_dir, NULL};
  err = serve_ssh_expect(config, host, mkdir_logs);
  if (err != APP_SUCCESS) goto dirs_done;
  char *const mkdir_config[] = {"sudo", "install", "-d", "-m", "2750", "-o", "quick", "-g", "quick-deploy", config_dir, NULL};
  err = serve_ssh_expect(config, host, mkdir_config);

dirs_done:
  if (err == APP_SUCCESS) {
    char *const backup[] = {
        "sh", "-c",
        "set -e; b=$1; sudo install -d -m 0700 \"$b\"; "
        "[ ! -e /usr/local/bin/quickd ] || sudo cp -p /usr/local/bin/quickd \"$b/quickd\"; "
        "[ ! -e /etc/openquick/quickd.json ] || sudo cp -p /etc/openquick/quickd.json \"$b/quickd.json\"; "
        "[ ! -e /etc/systemd/system/openquick.service ] || sudo cp -p /etc/systemd/system/openquick.service \"$b/openquick.service\"",
        "sh", backup_dir, NULL};
    err = serve_ssh_expect(config, host, backup);
    if (err == APP_SUCCESS) {
      backup_created = true;
      app_output_format(config, false, "backup     %s", backup_dir);
    }
  }
  if (err == APP_SUCCESS) {
    char *const scp_argv[] = {"scp", quickd, scp_dest, NULL};
    quick_process_result_t scp_res = {0};
    err = quick_process_capture(scp_argv, NULL, &scp_res);
    if (err != APP_SUCCESS || scp_res.exit_code != 0) {
      quick_print_error(config, scp_res.err && scp_res.err[0] ? scp_res.err : "failed to copy quickd over scp");
      err = err == APP_SUCCESS ? APP_ERROR_IO : err;
    }
    quick_process_result_destroy(&scp_res);
  }
  if (err == APP_SUCCESS) {
    char *const install_quickd[] = {"sudo", "install", "-m", "0755", "-o", "root", "-g", "root", tmp_remote, "/usr/local/bin/quickd", NULL};
    err = serve_ssh_expect(config, host, install_quickd);
  }
  if (err == APP_SUCCESS) {
    char *const rm_tmp[] = {"rm", "-f", tmp_remote, NULL};
    (void)serve_ssh_expect(config, host, rm_tmp);
  }
  if (err == APP_SUCCESS) {
    char *const mkdir_etc[] = {"sudo", "install", "-d", "-m", "0755", "-o", "root", "-g", "root", "/etc/openquick", NULL};
    err = serve_ssh_expect(config, host, mkdir_etc);
  }
  if (err == APP_SUCCESS) {
    err = serve_ssh_tee(config, host, "/etc/openquick/quickd.json", host_json);
  }
  if (err == APP_SUCCESS) {
    char *root_config = quick_path_join_cli(config_dir, "quickd.json");
    if (!root_config) {
      err = APP_ERROR_MEMORY;
    } else {
      err = serve_ssh_tee(config, host, root_config, host_json);
      if (err == APP_SUCCESS) {
        char *const config_perms[] = {"sudo", "chown", "root:quick-deploy",
                                      root_config, NULL};
        err = serve_ssh_expect(config, host, config_perms);
      }
      if (err == APP_SUCCESS) {
        char *const config_mode[] = {"sudo", "chmod", "0640", root_config,
                                     NULL};
        err = serve_ssh_expect(config, host, config_mode);
      }
      free(root_config);
    }
  }
  if (err == APP_SUCCESS) {
    err = serve_ssh_tee(config, host, "/etc/systemd/system/openquick.service", unit_for_root);
  }
  if (err == APP_SUCCESS) {
    char *const daemon_reload[] = {"sudo", "systemctl", "daemon-reload", NULL};
    err = serve_ssh_expect(config, host, daemon_reload);
  }
  if (err == APP_SUCCESS) {
    char *const enable_unit[] = {"sudo", "systemctl", "enable", "--now", "openquick.service", NULL};
    err = serve_ssh_expect(config, host, enable_unit);
  }
  if (err == APP_SUCCESS) {
    char *const group_writable[] = {"sudo", "chgrp", "-R", "quick-deploy",
                                    data_dir, sites_dir, uploads_dir, NULL};
    err = serve_ssh_expect(config, host, group_writable);
  }
  if (err == APP_SUCCESS) {
    char *const mode_writable[] = {"sudo", "chmod", "-R", "g+rwX",
                                   data_dir, sites_dir, uploads_dir, NULL};
    err = serve_ssh_expect(config, host, mode_writable);
  }
  if (err == APP_SUCCESS) {
    char *const doctor[] = {"quickd", "doctor", "--host", "--json", NULL};
    quick_process_result_t doc_res = {0};
    err = serve_ssh_capture(host, doctor, NULL, &doc_res);
    if (err != APP_SUCCESS || doc_res.exit_code != 0 ||
        (doc_res.out && strstr(doc_res.out, "\"status\":\"fail\""))) {
      const char *detail = doc_res.err && doc_res.err[0]
                               ? doc_res.err
                               : (doc_res.out && doc_res.out[0]
                                      ? doc_res.out
                                      : "quickd doctor --host --json failed after install");
      quick_print_error(config, detail);
      err = err == APP_SUCCESS ? APP_ERROR_IO : err;
      if (backup_created) {
        app_output_format(config, true, "rollback   restoring backup from %s", backup_dir);
        char *const restore[] = {
            "sh", "-c",
            "set -e; b=$1; "
            "[ ! -f \"$b/quickd\" ] || sudo cp -p \"$b/quickd\" /usr/local/bin/quickd; "
            "[ ! -f \"$b/quickd.json\" ] || sudo cp -p \"$b/quickd.json\" /etc/openquick/quickd.json; "
            "[ ! -f \"$b/openquick.service\" ] || sudo cp -p \"$b/openquick.service\" /etc/systemd/system/openquick.service; "
            "sudo systemctl daemon-reload; sudo systemctl restart openquick.service || true",
            "sh", backup_dir, NULL};
        app_error restore_err = serve_ssh_expect(config, host, restore);
        if (restore_err == APP_SUCCESS) {
          app_output("rollback   previous quickd/config restored; inspect backup before removing it", config, true);
        } else {
          app_output_format(config, true, "rollback   restore failed; manual backup remains at %s", backup_dir);
        }
      }
    }
    quick_process_result_destroy(&doc_res);
  }

done:
  free(sites_dir);
  free(data_dir);
  free(uploads_dir);
  free(logs_dir);
  free(config_dir);
  free(backup_dir);
  free(remote_user);
  free(tmp_remote);
  free(remote_tmp_dir);
  free(scp_dest);
  free(unit_for_root);
  free(host_json);
  free(quickd);
  if (err == APP_SUCCESS) {
    app_output_format(config, false, "Installed quickd on %s", host);
  }
  (void)profile_name;
  return err;
}

static app_error serve_install(const app_config_t *config, int argc,
                               char *const argv[]) {
  const char *profile_name = quick_cmd_value(argc, argv, "--profile");
  const char *host = quick_cmd_value(argc, argv, "--host");
  const char *remote_root = quick_cmd_value(argc, argv, "--remote-root");
  const char *domain = quick_cmd_value(argc, argv, "--domain");
  const char *iap = quick_cmd_value(argc, argv, "--iap");
  const bool execute = quick_cmd_flag(argc, argv, "--execute");
  const bool unsafe = quick_cmd_flag(argc, argv, "--allow-public-unsafe");
  if (!profile_name) profile_name = "default";

  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    return err;
  }
  const quick_profile_t *profile = quick_profile_config_find(&profiles, profile_name);
  if (!host && profile && profile->ssh && profile->ssh[0]) host = profile->ssh;
  if (!remote_root && profile && profile->remote_root && profile->remote_root[0]) {
    remote_root = profile->remote_root;
  }
  if (!domain && profile && profile->base_domain && profile->base_domain[0]) {
    domain = profile->base_domain;
  }
  if (!iap && profile && profile->iap.type && profile->iap.type[0]) {
    iap = profile->iap.type;
  }
  if (!remote_root) remote_root = "/srv/quick";
  if (!iap) iap = "tailscale";

  quick_iap_config_t install_iap = {.type = (char *)iap};
  if (profile) {
    install_iap.mode = profile->iap.mode;
    install_iap.team_domain = profile->iap.team_domain;
    install_iap.audience = profile->iap.audience;
  }
  if (!serve_iap_is_cloudflare(iap)) {
    install_iap.team_domain = NULL;
    install_iap.audience = NULL;
  }
  if (strcmp(iap, "none") == 0) {
    install_iap.mode = NULL;
  }
  if (serve_iap_is_cloudflare(iap) &&
      (!install_iap.team_domain || install_iap.team_domain[0] == '\0' ||
       !install_iap.audience || install_iap.audience[0] == '\0')) {
    quick_print_error(config,
                      "iap=cloudflare requires iap.team_domain and iap.audience in the selected profile");
    quick_profile_config_destroy(&profiles);
    return APP_ERROR_VALIDATION;
  }

  err = serve_validate_install_inputs(config, profile_name, host,
                                      remote_root, domain, iap);
  if (err != APP_SUCCESS) {
    quick_profile_config_destroy(&profiles);
    return err;
  }
  if (strcmp(iap, "none") == 0 && domain && strcmp(domain, "localhost") != 0 &&
      strcmp(domain, "127.0.0.1") != 0 && !unsafe) {
    quick_print_error(config,
                      "iap=none is only allowed for loopback unless --allow-public-unsafe is passed");
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
    app_output_format(config, false, "  host          %s", host ? host : "(local)");
    app_output_format(config, false, "  remote root   %s", remote_root);
    app_output_format(config, false, "  domain        %s", domain ? domain : "(path fallback)");
    app_output_format(config, false, "  iap           %s", iap);
    app_output("  steps", config, false);
    for (size_t i = 0; i < steps.count; i++) {
      app_output_format(config, false, "    %zu. %s", i + 1U,
                        steps.steps[i].summary);
    }
  }

  if (execute) {
    err = serve_install_execute(config, profile_name, host, remote_root, domain,
                                &install_iap);
    if (err != APP_SUCCESS) {
      quick_profile_config_destroy(&profiles);
      quick_serve_install_steps_destroy(&steps);
      return err;
    }
  }
  err = serve_write_profile(profile_name, host, remote_root, domain, iap);
  quick_profile_config_destroy(&profiles);
  quick_serve_install_steps_destroy(&steps);
  return err;
}

app_error app_cmd_serve(const app_config_t *config, int argc,
                        char *const argv[]) {
  if (quick_cmd_flag(argc, argv, "--dev")) {
    return serve_dev(config, argc, argv);
  }
  const char *value_opts[] = {"--profile", "--host", "--remote-root",
                              "--domain", "--iap", "--remote-api"};
  const char *sub = quick_cmd_first_positional(argc, argv, value_opts,
                                               APP_COUNTOF(value_opts));
  if (sub && strcmp(sub, "install") == 0) {
    return serve_install(config, argc, argv);
  }
  quick_print_error(config, "serve requires --dev or install");
  return APP_ERROR_MISSING_ARG;
}
