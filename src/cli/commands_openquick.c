#include "commands_openquick.h"
#include "commands.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "../core/ops.h"
#include "../io/output.h"
#include "../io/terminal.h"

static char *quick_strdup_cli(const char *value) {
  if (!value) {
    return NULL;
  }
  size_t len = strlen(value);
  char *copy = malloc(len + 1U);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len + 1U);
  return copy;
}

const char *quick_cmd_value(int argc, char *const argv[], const char *name) {
  if (!argv || !name) {
    return NULL;
  }
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      return NULL;
    }
    if (strcmp(argv[i], name) == 0) {
      return (i + 1 < argc) ? argv[i + 1] : NULL;
    }
  }
  return NULL;
}

bool quick_cmd_flag(int argc, char *const argv[], const char *name) {
  if (!argv || !name) {
    return false;
  }
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "--") == 0) {
      return false;
    }
    if (strcmp(argv[i], name) == 0) {
      return true;
    }
  }
  return false;
}

static bool quick_name_in_list(const char *name, const char *const *items,
                               size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(name, items[i]) == 0) {
      return true;
    }
  }
  return false;
}

const char *quick_cmd_first_positional(int argc, char *const argv[],
                                       const char *const *value_options,
                                       size_t value_option_count) {
  bool end_options = false;
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg) {
      continue;
    }
    if (!end_options && strcmp(arg, "--") == 0) {
      end_options = true;
      continue;
    }
    if (!end_options && strncmp(arg, "--", 2) == 0) {
      if (quick_name_in_list(arg, value_options, value_option_count)) {
        i++;
      }
      continue;
    }
    return arg;
  }
  return NULL;
}

app_error quick_cmd_load_profiles(quick_profile_config_t *profiles) {
  if (!profiles) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_profile_config_init(profiles);
  const char *override = getenv("QUICK_CONFIG_PATH");
  if (override && override[0] != '\0') {
    app_error err = quick_profile_config_load_file(override, profiles);
    return err == APP_ERROR_NOT_FOUND ? APP_SUCCESS : err;
  }
  return quick_profile_config_load_default(profiles);
}

char *quick_path_join_cli(const char *a, const char *b) {
  if (!a || a[0] == '\0') {
    return quick_strdup_cli(b ? b : "");
  }
  if (!b || b[0] == '\0') {
    return quick_strdup_cli(a);
  }
  const size_t alen = strlen(a);
  const size_t blen = strlen(b);
  const bool slash = alen > 0 && a[alen - 1] != '/';
  char *out = malloc(alen + (slash ? 1U : 0U) + blen + 1U);
  if (!out) {
    return NULL;
  }
  memcpy(out, a, alen);
  size_t pos = alen;
  if (slash) {
    out[pos++] = '/';
  }
  memcpy(out + pos, b, blen);
  out[pos + blen] = '\0';
  return out;
}

bool quick_path_exists_cli(const char *path) {
  if (!path) {
    return false;
  }
#ifdef _WIN32
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  fclose(f);
  return true;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

bool quick_dir_exists_cli(const char *path) {
#ifdef _WIN32
  return quick_path_exists_cli(path);
#else
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

app_error quick_mkdir_p_cli(const char *path, int mode) {
#ifdef _WIN32
  (void)path;
  (void)mode;
  return APP_SUCCESS;
#else
  if (!path) {
    return APP_ERROR_INVALID_ARG;
  }
  char *copy = quick_strdup_cli(path);
  if (!copy) {
    return APP_ERROR_MEMORY;
  }
  for (char *p = copy + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(copy, (mode_t)mode) != 0 && errno != EEXIST) {
        free(copy);
        return APP_ERROR_IO;
      }
      *p = '/';
    }
  }
  if (mkdir(copy, (mode_t)mode) != 0 && errno != EEXIST) {
    free(copy);
    return APP_ERROR_IO;
  }
  free(copy);
  return APP_SUCCESS;
#endif
}

char *quick_find_executable_cli(const char *name) {
  if (!name || name[0] == '\0') {
    return NULL;
  }
  if (strchr(name, '/')) {
    return quick_path_exists_cli(name) ? quick_strdup_cli(name) : NULL;
  }
  const char *path_env = getenv("PATH");
  if (!path_env) {
    return NULL;
  }
  char *paths = quick_strdup_cli(path_env);
  if (!paths) {
    return NULL;
  }
  char *save = NULL;
  for (char *dir = strtok_r(paths, ":", &save); dir;
       dir = strtok_r(NULL, ":", &save)) {
    char *candidate = quick_path_join_cli(dir, name);
    if (candidate && quick_path_exists_cli(candidate)) {
      free(paths);
      return candidate;
    }
    free(candidate);
  }
  free(paths);
  return NULL;
}

app_error quick_read_file_cli(const char *path, char **out) {
  if (!path || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  *out = NULL;
  FILE *stream = fopen(path, "rb");
  if (!stream) {
    return APP_ERROR_NOT_FOUND;
  }
  if (fseek(stream, 0, SEEK_END) != 0) {
    fclose(stream);
    return APP_ERROR_IO;
  }
  long size = ftell(stream);
  if (size < 0 || size > CONFIG_MAX_SIZE) {
    fclose(stream);
    return APP_ERROR_OUT_OF_RANGE;
  }
  if (fseek(stream, 0, SEEK_SET) != 0) {
    fclose(stream);
    return APP_ERROR_IO;
  }
  char *buf = malloc((size_t)size + 1U);
  if (!buf) {
    fclose(stream);
    return APP_ERROR_MEMORY;
  }
  size_t n = fread(buf, 1, (size_t)size, stream);
  fclose(stream);
  if (n != (size_t)size) {
    free(buf);
    return APP_ERROR_IO;
  }
  buf[n] = '\0';
  *out = buf;
  return APP_SUCCESS;
}

static const char *quick_json_find_field_token(const char *json,
                                               const char *field) {
  if (!json || !field) {
    return NULL;
  }
  size_t flen = strlen(field);
  const char *p = json;
  while ((p = strchr(p, '"')) != NULL) {
    p++;
    if (strncmp(p, field, flen) == 0 && p[flen] == '"') {
      const char *colon = strchr(p + flen + 1, ':');
      if (colon) {
        return colon + 1;
      }
    }
    p += strcspn(p, "\"");
  }
  return NULL;
}

char *quick_json_get_string_field_cli(const char *json, const char *field) {
  const char *p = quick_json_find_field_token(json, field);
  if (!p) {
    return NULL;
  }
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  if (strncmp(p, "null", 4) == 0) {
    return NULL;
  }
  if (*p != '"') {
    return NULL;
  }
  p++;
  size_t cap = 64;
  size_t used = 0;
  char *out = malloc(cap);
  if (!out) {
    return NULL;
  }
  while (*p != '\0' && *p != '"') {
    char ch = *p++;
    if (ch == '\\' && *p != '\0') {
      ch = *p++;
      switch (ch) {
      case 'n': ch = '\n'; break;
      case 'r': ch = '\r'; break;
      case 't': ch = '\t'; break;
      case '"': ch = '"'; break;
      case '\\': ch = '\\'; break;
      default: break;
      }
    }
    if (used + 1U >= cap) {
      char *grown = realloc(out, cap * 2U);
      if (!grown) {
        free(out);
        return NULL;
      }
      out = grown;
      cap *= 2U;
    }
    out[used++] = ch;
  }
  out[used] = '\0';
  return out;
}

long quick_json_get_long_field_cli(const char *json, const char *field,
                                   long fallback) {
  const char *p = quick_json_find_field_token(json, field);
  if (!p) {
    return fallback;
  }
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  char *end = NULL;
  long value = strtol(p, &end, 10);
  return end && end != p ? value : fallback;
}

bool quick_cmd_prompt_site_confirmation(const app_config_t *config,
                                        const char *site,
                                        const char *message) {
  if (!site || site[0] == '\0' || !app_terminal_stream_is_tty(APP_TERMINAL_STDIN)) {
    return false;
  }
  if (message && message[0] != '\0') {
    fprintf(stderr, "%s\n", message);
  }
  fprintf(stderr, "Type '%s' to confirm: ", site);
  fflush(stderr);
  char input[256];
  if (!fgets(input, sizeof(input), stdin)) {
    return false;
  }
  input[strcspn(input, "\r\n")] = '\0';
  const bool ok = strcmp(input, site) == 0;
  if (!ok && config && !app_config_is_json_output(config)) {
    fprintf(stderr, "Confirmation did not match; aborted.\n");
  }
  return ok;
}

void quick_print_error(const app_config_t *config, const char *message) {
  app_output(message ? message : "OpenQuick error", config, true);
}

/* Site administration command handlers. */
static const char *site_admin_nth_positional(int argc, char *const argv[],
                                             int wanted,
                                             const char *const *value_opts,
                                             size_t value_opt_count) {
  bool end_options = false;
  int seen = 0;
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg) {
      continue;
    }
    if (!end_options && strcmp(arg, "--") == 0) {
      end_options = true;
      continue;
    }
    if (!end_options && strncmp(arg, "--", 2) == 0) {
      bool takes_value = false;
      for (size_t j = 0; j < value_opt_count; j++) {
        if (strcmp(arg, value_opts[j]) == 0) {
          takes_value = true;
          break;
        }
      }
      if (takes_value) {
        i++;
      }
      continue;
    }
    if (seen == wanted) {
      return arg;
    }
    seen++;
  }
  return NULL;
}

static bool site_admin_wants_json(const app_config_t *config, int argc,
                                  char *const argv[]) {
  return app_config_is_json_output(config) || quick_cmd_flag(argc, argv, "--json");
}

static void site_admin_print_raw_json_or_fallback(const char *json,
                                                  const char *fallback_site,
                                                  const char *field,
                                                  bool value) {
  if (json && json[0] != '\0') {
    fputs(json, stdout);
    if (json[strlen(json) - 1U] != '\n') {
      fputc('\n', stdout);
    }
    return;
  }
  bool comma = false;
  app_json_begin_object(stdout);
  app_json_write_string_field(stdout, "format_version", "1.0", &comma);
  app_json_write_string_field(stdout, "site", fallback_site, &comma);
  app_json_write_bool_field(stdout, field, value, &comma);
  app_json_end_object(stdout);
  app_json_end_line(stdout);
}

static void site_admin_print_site_human(const app_config_t *config,
                                        const char *title,
                                        const quick_remote_site_info_t *site) {
  app_output_format(config, false, "%s %s", title,
                    site && site->name ? site->name : "(unknown)");
  if (site && site->subdomain) {
    app_output_format(config, false, "  subdomain   %s", site->subdomain);
  }
  if (site && site->url) {
    app_output_format(config, false, "  url         %s", site->url);
  }
  if (site && site->release) {
    app_output_format(config, false, "  release     %s", site->release);
  }
  if (site && site->updated_at) {
    app_output_format(config, false, "  updated     %s", site->updated_at);
  }
  if (site && site->deployer) {
    app_output_format(config, false, "  deployer    %s", site->deployer);
  }
  if (site && site->have_public) {
    app_output_format(config, false, "  public      %s",
                      site->is_public ? "on" : "off");
  }
}

static bool quick_nonempty(const char *value) {
  return value && value[0] != '\0';
}

static const char *quick_config_profile_source(
    const quick_plan_overrides_t *overrides,
    const quick_profile_config_t *profiles, const quick_deploy_plan_t *plan) {
  if (quick_nonempty(overrides ? overrides->profile : NULL)) {
    return "flag:--profile";
  }
  if (quick_nonempty(getenv("QUICK_PROFILE"))) {
    return "env:QUICK_PROFILE";
  }
  if (plan && quick_nonempty(plan->site_config.profile)) {
    return "quick.json:profile";
  }
  if (profiles && quick_nonempty(profiles->default_profile)) {
    return "profile-config:default_profile";
  }
  return "default:local";
}

static const char *quick_config_site_source(
    const quick_plan_overrides_t *overrides, const quick_deploy_plan_t *plan) {
  if (quick_nonempty(overrides ? overrides->site : NULL)) {
    return "flag:--site";
  }
  if (quick_nonempty(getenv("QUICK_SITE"))) {
    return "env:QUICK_SITE";
  }
  if (plan && quick_nonempty(plan->site_config.name)) {
    return "quick.json:name";
  }
  return "directory-name";
}

static const char *quick_config_subdomain_source(
    const quick_plan_overrides_t *overrides, const quick_deploy_plan_t *plan) {
  if (quick_nonempty(overrides ? overrides->subdomain : NULL)) {
    return "flag:--subdomain";
  }
  if (plan && quick_nonempty(plan->site_config.subdomain)) {
    return "quick.json:subdomain";
  }
  return "site";
}

static char *quick_config_path_for_display(const char **source_out) {
  const char *override = getenv("QUICK_CONFIG_PATH");
  if (quick_nonempty(override)) {
    if (source_out) {
      *source_out = "env:QUICK_CONFIG_PATH";
    }
    return quick_strdup_cli(override);
  }
  if (source_out) {
    *source_out = "default";
  }
  return quick_profile_config_default_path();
}

static void quick_config_show_json(const quick_deploy_plan_t *plan,
                                   const quick_profile_t *profile,
                                   const char *config_path,
                                   const char *config_path_source,
                                   const char *profile_source,
                                   const char *site_source,
                                   const char *subdomain_source) {
  bool comma = false;
  app_json_begin_object(stdout);
  app_json_write_string_field(stdout, "format_version", "1.0", &comma);
  app_json_write_string_field(stdout, "config_path", config_path, &comma);
  app_json_write_string_field(stdout, "site", plan->site, &comma);
  app_json_write_string_field(stdout, "subdomain", plan->subdomain, &comma);
  app_json_write_string_field(stdout, "profile", plan->profile, &comma);
  app_json_write_string_field(stdout, "ssh", plan->ssh, &comma);
  app_json_write_string_field(stdout, "remote_root", plan->remote_root, &comma);
  app_json_write_string_field(stdout, "base_domain", plan->base_domain, &comma);
  app_json_write_string_field(stdout, "base_url", plan->base_url, &comma);
  app_json_write_string_field(stdout, "url", plan->url, &comma);
  app_json_write_string_field(stdout, "site_root", plan->site_root, &comma);
  app_json_write_string_field(stdout, "quick_json_path", plan->quick_json_path,
                              &comma);
  app_json_write_string_field(stdout, "source_dir", plan->source_dir, &comma);
  app_json_write_string_field(stdout, "output_dir", plan->output_dir, &comma);
  app_json_write_string_field(stdout, "iap_mode",
                              profile && profile->iap.mode ? profile->iap.mode
                                                            : NULL,
                              &comma);
  app_json_write_raw_field(stdout, "sources", "{", &comma);
  bool source_comma = false;
  app_json_write_string_field(stdout, "config_path", config_path_source,
                              &source_comma);
  app_json_write_string_field(stdout, "profile", profile_source,
                              &source_comma);
  app_json_write_string_field(stdout, "site", site_source, &source_comma);
  app_json_write_string_field(stdout, "subdomain", subdomain_source,
                              &source_comma);
  app_json_write_string_field(stdout, "ssh",
                              quick_nonempty(getenv("QUICK_REMOTE"))
                                  ? "env:QUICK_REMOTE"
                                  : (profile && profile->ssh ? "profile:ssh"
                                                            : "unset"),
                              &source_comma);
  app_json_write_string_field(stdout, "remote_root",
                              profile && profile->remote_root
                                  ? "profile:remote_root"
                                  : "default:/srv/quick",
                              &source_comma);
  app_json_write_string_field(stdout, "base_domain",
                              quick_nonempty(getenv("QUICK_BASE_DOMAIN"))
                                  ? "env:QUICK_BASE_DOMAIN"
                                  : (profile && profile->base_domain
                                         ? "profile:base_domain"
                                         : "unset"),
                              &source_comma);
  app_json_write_string_field(stdout, "base_url",
                              profile && profile->base_url ? "profile:base_url"
                                                           : "derived",
                              &source_comma);
  app_json_end_object(stdout);
  app_json_end_object(stdout);
  app_json_end_line(stdout);
}

app_error app_cmd_config_show(const app_config_t *config, int argc,
                              char *const argv[]) {
  const char *value_opts[] = {"--profile", "--site", "--subdomain"};
  const char *action = quick_cmd_first_positional(argc, argv, value_opts,
                                                  APP_COUNTOF(value_opts));
  if (!action || strcmp(action, "show") != 0) {
    quick_print_error(config, "config requires action 'show'");
    return APP_ERROR_MISSING_ARG;
  }

  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to read OpenQuick profile config");
    return err;
  }

  quick_plan_overrides_t overrides = {
      .profile = quick_cmd_value(argc, argv, "--profile"),
      .site = quick_cmd_value(argc, argv, "--site"),
      .subdomain = quick_cmd_value(argc, argv, "--subdomain")};
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  err = quick_deploy_plan_resolve(&overrides, &profiles, &plan);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to resolve OpenQuick config");
    quick_profile_config_destroy(&profiles);
    return err;
  }

  const char *config_path_source = "default";
  char *config_path = quick_config_path_for_display(&config_path_source);
  const quick_profile_t *profile =
      quick_profile_config_find(&profiles, plan.profile);
  const char *profile_source =
      quick_config_profile_source(&overrides, &profiles, &plan);
  const char *site_source = quick_config_site_source(&overrides, &plan);
  const char *subdomain_source =
      quick_config_subdomain_source(&overrides, &plan);

  if (site_admin_wants_json(config, argc, argv)) {
    quick_config_show_json(&plan, profile, config_path, config_path_source,
                           profile_source, site_source, subdomain_source);
  } else {
    app_output_format(config, false, "OpenQuick config");
    app_output_format(config, false, "  config     %s (%s)",
                      config_path ? config_path : "(none)",
                      config_path_source);
    app_output_format(config, false, "  profile    %s (%s)", plan.profile,
                      profile_source);
    app_output_format(config, false, "  site       %s (%s)", plan.site,
                      site_source);
    app_output_format(config, false, "  subdomain  %s (%s)", plan.subdomain,
                      subdomain_source);
    app_output_format(config, false, "  host       %s",
                      plan.ssh ? plan.ssh : "(none)");
    app_output_format(config, false, "  root       %s", plan.remote_root);
    app_output_format(config, false, "  domain     %s",
                      plan.base_domain ? plan.base_domain : "(path fallback)");
    app_output_format(config, false, "  base url   %s",
                      plan.base_url ? plan.base_url : "(derived)");
    app_output_format(config, false, "  url        %s", plan.url);
    app_output_format(config, false, "  iap        %s",
                      profile && profile->iap.mode ? profile->iap.mode
                                                   : "(none)");
  }
  free(config_path);
  quick_deploy_plan_destroy(&plan);
  quick_profile_config_destroy(&profiles);
  return APP_SUCCESS;
}

app_error app_cmd_delete(const app_config_t *config, int argc,
                         char *const argv[]) {
  const char *value_opts[] = {"--profile"};
  const char *site = site_admin_nth_positional(argc, argv, 0, value_opts,
                                               APP_COUNTOF(value_opts));
  if (!site) {
    quick_print_error(config, "delete requires a site");
    return APP_ERROR_MISSING_ARG;
  }
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to read OpenQuick profile config");
    return err;
  }
  quick_delete_result_t result;
  quick_delete_result_init(&result);
  quick_delete_request_t request = {.profiles = &profiles,
                                    .profile = quick_cmd_value(argc, argv, "--profile"),
                                    .site = site,
                                    .assume_yes = quick_cmd_flag(argc, argv, "--yes")};
  err = quick_op_delete(&request, &result);
  if (err == APP_SUCCESS && result.confirmation_required) {
    if (!site_admin_wants_json(config, argc, argv)) {
      site_admin_print_site_human(config, "delete", &result.site);
    }
    char prompt[512];
    snprintf(prompt, sizeof(prompt),
             "Deleting site '%s' removes it from the remote host.", site);
    if (quick_cmd_prompt_site_confirmation(config, site, prompt)) {
      request.confirmed = true;
      quick_delete_result_destroy(&result);
      quick_delete_result_init(&result);
      err = quick_op_delete(&request, &result);
    } else {
      quick_print_error(config,
                        "Delete requires typing the site name to confirm; pass --yes for non-interactive use.");
      err = APP_ERROR_VALIDATION;
    }
  }
  if (err == APP_SUCCESS) {
    if (site_admin_wants_json(config, argc, argv)) {
      site_admin_print_raw_json_or_fallback(result.delete_json, site, "deleted",
                                            result.deleted);
    } else {
      site_admin_print_site_human(config, "deleted", &result.site);
      if (result.archive) {
        app_output_format(config, false, "  archive    %s", result.archive);
        app_output_format(config, false, "  restore    quick restore %s --from %s", site, result.archive);
      }
    }
  } else if (err != APP_ERROR_VALIDATION) {
    quick_print_error(config, "failed to delete remote site");
  }
  quick_delete_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return err;
}

app_error app_cmd_restore(const app_config_t *config, int argc,
                          char *const argv[]) {
  const char *value_opts[] = {"--profile", "--from"};
  const char *site = site_admin_nth_positional(argc, argv, 0, value_opts,
                                               APP_COUNTOF(value_opts));
  const char *archive = quick_cmd_value(argc, argv, "--from");
  if (!site || !archive) {
    quick_print_error(config, "restore requires a site and --from archive path");
    return APP_ERROR_MISSING_ARG;
  }
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to read OpenQuick profile config");
    return err;
  }
  quick_restore_result_t result;
  quick_restore_result_init(&result);
  quick_restore_request_t request = {.profiles = &profiles,
                                     .profile = quick_cmd_value(argc, argv, "--profile"),
                                     .site = site,
                                     .archive = archive,
                                     .assume_yes = quick_cmd_flag(argc, argv, "--yes")};
  err = quick_op_restore(&request, &result);
  if (err == APP_SUCCESS && result.confirmation_required) {
    char prompt[640];
    snprintf(prompt, sizeof(prompt),
             "Restoring site '%s' will replace the deleted site from archive '%s'.",
             site, archive);
    if (quick_cmd_prompt_site_confirmation(config, site, prompt)) {
      request.confirmed = true;
      quick_restore_result_destroy(&result);
      quick_restore_result_init(&result);
      err = quick_op_restore(&request, &result);
    } else {
      quick_print_error(config,
                        "Restore requires typing the site name to confirm; pass --yes for non-interactive use.");
      err = APP_ERROR_VALIDATION;
    }
  }
  if (err == APP_SUCCESS) {
    if (site_admin_wants_json(config, argc, argv)) {
      if (result.remote_json && result.remote_json[0] != '\0') {
        fputs(result.remote_json, stdout);
        if (result.remote_json[strlen(result.remote_json) - 1U] != '\n') {
          fputc('\n', stdout);
        }
      } else {
        bool comma = false;
        app_json_begin_object(stdout);
        app_json_write_string_field(stdout, "format_version", "1.0", &comma);
        app_json_write_string_field(stdout, "site", site, &comma);
        app_json_write_bool_field(stdout, "restored", result.restored, &comma);
        app_json_end_object(stdout);
        app_json_end_line(stdout);
      }
    } else {
      app_output_format(config, false, "restored %s", site);
      if (result.release) {
        app_output_format(config, false, "  release    %s", result.release);
      }
      if (result.url) {
        app_output_format(config, false, "  url        %s", result.url);
      }
    }
  } else if (err != APP_ERROR_VALIDATION) {
    quick_print_error(config, "failed to restore remote site");
  }
  quick_restore_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return err;
}

app_error app_cmd_rollback(const app_config_t *config, int argc,
                           char *const argv[]) {
  const char *value_opts[] = {"--profile", "--to"};
  const char *site = site_admin_nth_positional(argc, argv, 0, value_opts,
                                               APP_COUNTOF(value_opts));
  if (!site) {
    quick_print_error(config, "rollback requires a site");
    return APP_ERROR_MISSING_ARG;
  }
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to read OpenQuick profile config");
    return err;
  }
  quick_rollback_result_t result;
  quick_rollback_result_init(&result);
  quick_rollback_request_t request = {.profiles = &profiles,
                                      .profile = quick_cmd_value(argc, argv, "--profile"),
                                      .site = site,
                                      .release = quick_cmd_value(argc, argv, "--to"),
                                      .assume_yes = quick_cmd_flag(argc, argv, "--yes")};
  err = quick_op_rollback(&request, &result);
  if (err == APP_SUCCESS && result.confirmation_required) {
    if (!site_admin_wants_json(config, argc, argv)) {
      site_admin_print_site_human(config, "rollback", &result.site);
    }
    char prompt[640];
    if (request.release && request.release[0] != '\0') {
      snprintf(prompt, sizeof(prompt),
               "Rolling back site '%s' will restore release '%s'.",
               site, request.release);
    } else {
      snprintf(prompt, sizeof(prompt),
               "Rolling back site '%s' will restore the previous release.",
               site);
    }
    if (quick_cmd_prompt_site_confirmation(config, site, prompt)) {
      request.confirmed = true;
      quick_rollback_result_destroy(&result);
      quick_rollback_result_init(&result);
      err = quick_op_rollback(&request, &result);
    } else {
      quick_print_error(config,
                        "Rollback requires typing the site name to confirm; pass --yes for non-interactive use.");
      err = APP_ERROR_VALIDATION;
    }
  }
  if (err == APP_SUCCESS) {
    if (site_admin_wants_json(config, argc, argv)) {
      site_admin_print_raw_json_or_fallback(result.remote_json, site,
                                            "rolled_back",
                                            result.rolled_back);
    } else {
      site_admin_print_site_human(config, "rolled back", &result.site);
      if (result.release) {
        app_output_format(config, false, "  restored    %s", result.release);
      }
      if (result.previous_release) {
        app_output_format(config, false, "  previous    %s", result.previous_release);
      }
    }
  } else if (err != APP_ERROR_VALIDATION) {
    quick_print_error(config, "failed to roll back remote site");
  }
  quick_rollback_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return err;
}

app_error app_cmd_public(const app_config_t *config, int argc,
                         char *const argv[]) {
  const char *value_opts[] = {"--profile"};
  const char *site = site_admin_nth_positional(argc, argv, 0, value_opts,
                                               APP_COUNTOF(value_opts));
  const char *state = site_admin_nth_positional(argc, argv, 1, value_opts,
                                                APP_COUNTOF(value_opts));
  if (!site) {
    quick_print_error(config, "public requires a site");
    return APP_ERROR_MISSING_ARG;
  }
  quick_public_action_t action = QUICK_PUBLIC_STATUS;
  if (state) {
    if (strcmp(state, "on") == 0) {
      action = QUICK_PUBLIC_ON;
    } else if (strcmp(state, "off") == 0) {
      action = QUICK_PUBLIC_OFF;
    } else {
      quick_print_error(config, "public state must be 'on' or 'off'");
      return APP_ERROR_VALIDATION;
    }
  }
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to read OpenQuick profile config");
    return err;
  }
  quick_public_result_t result;
  quick_public_result_init(&result);
  quick_public_request_t request = {.profiles = &profiles,
                                    .profile = quick_cmd_value(argc, argv, "--profile"),
                                    .site = site,
                                    .action = action,
                                    .assume_yes = quick_cmd_flag(argc, argv, "--yes")};
  err = quick_op_public(&request, &result);
  if (err == APP_SUCCESS && result.confirmation_required) {
    if (!site_admin_wants_json(config, argc, argv)) {
      site_admin_print_site_human(config, "public", &result.site);
    }
    char prompt[512];
    snprintf(prompt, sizeof(prompt),
             "Making site '%s' public allows unauthenticated GET/HEAD for static files.",
             site);
    if (quick_cmd_prompt_site_confirmation(config, site, prompt)) {
      request.confirmed = true;
      quick_public_result_destroy(&result);
      quick_public_result_init(&result);
      err = quick_op_public(&request, &result);
    } else {
      quick_print_error(config,
                        "Public-on requires typing the site name to confirm; pass --yes for non-interactive use.");
      err = APP_ERROR_VALIDATION;
    }
  }
  if (err == APP_SUCCESS) {
    if (site_admin_wants_json(config, argc, argv)) {
      const char *json = result.changed ? result.remote_json : result.site.raw_json;
      site_admin_print_raw_json_or_fallback(json, site, "public",
                                            result.is_public);
    } else {
      site_admin_print_site_human(config,
                                  action == QUICK_PUBLIC_STATUS ? "public" : "updated public",
                                  &result.site);
      app_output_format(config, false, "  public      %s",
                        result.is_public ? "on" : "off");
    }
  } else if (err != APP_ERROR_VALIDATION) {
    quick_print_error(config, "failed to update public status");
  }
  quick_public_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return err;
}

app_error app_cmd_domain(const app_config_t *config, int argc,
                         char *const argv[]) {
  const char *value_opts[] = {"--site", "--profile"};
  const char *action_arg = site_admin_nth_positional(argc, argv, 0, value_opts,
                                                     APP_COUNTOF(value_opts));
  const char *domain = site_admin_nth_positional(argc, argv, 1, value_opts,
                                                 APP_COUNTOF(value_opts));
  if (!action_arg) {
    quick_print_error(config, "domain requires add, remove, or list");
    return APP_ERROR_MISSING_ARG;
  }
  quick_domain_action_t action;
  if (strcmp(action_arg, "add") == 0) {
    action = QUICK_DOMAIN_ADD;
  } else if (strcmp(action_arg, "remove") == 0) {
    action = QUICK_DOMAIN_REMOVE;
  } else if (strcmp(action_arg, "list") == 0) {
    action = QUICK_DOMAIN_LIST;
  } else {
    quick_print_error(config, "domain action must be add, remove, or list");
    return APP_ERROR_VALIDATION;
  }
  if ((action == QUICK_DOMAIN_ADD || action == QUICK_DOMAIN_REMOVE) &&
      !domain) {
    quick_print_error(config, "domain add/remove requires a domain");
    return APP_ERROR_MISSING_ARG;
  }
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to read OpenQuick profile config");
    return err;
  }
  quick_domain_result_t result;
  quick_domain_result_init(&result);
  quick_domain_request_t request = {.profiles = &profiles,
                                    .profile = quick_cmd_value(argc, argv, "--profile"),
                                    .site = quick_cmd_value(argc, argv, "--site"),
                                    .domain = domain,
                                    .action = action};
  err = quick_op_domain(&request, &result);
  if (err == APP_SUCCESS) {
    if (site_admin_wants_json(config, argc, argv)) {
      if (result.remote_json && result.remote_json[0]) {
        fputs(result.remote_json, stdout);
        if (result.remote_json[strlen(result.remote_json) - 1U] != '\n') {
          fputc('\n', stdout);
        }
      } else {
        fputs("{\"format_version\":\"1.0\",\"domains\":[]}", stdout);
        fputc('\n', stdout);
      }
    } else if (action == QUICK_DOMAIN_LIST) {
      app_output(result.remote_json && result.remote_json[0]
                     ? result.remote_json
                     : "No domains returned.",
                 config, false);
    } else {
      app_output_format(config, false, "domain %s %s", action_arg,
                        domain ? domain : "");
      if (request.site) {
        app_output_format(config, false, "  site        %s", result.site);
      }
    }
  } else {
    quick_print_error(config, "failed to run domain command");
  }
  quick_domain_result_destroy(&result);
  quick_profile_config_destroy(&profiles);
  return err;
}
