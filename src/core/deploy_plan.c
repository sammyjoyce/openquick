#include "deploy_plan.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif

#include "json_util.h"
#include "types.h"

static char *quick_strdup(const char *value) {
  if (!value) {
    return NULL;
  }
  const size_t len = strlen(value);
  char *copy = malloc(len + 1U);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len + 1U);
  return copy;
}

static bool quick_set_string(char **slot, const char *value) {
  char *copy = value ? quick_strdup(value) : NULL;
  if (value && !copy) {
    return false;
  }
  free(*slot);
  *slot = copy;
  return true;
}

static bool quick_path_is_absolute(const char *path) {
  if (!path || path[0] == '\0') {
    return false;
  }
#ifdef _WIN32
  return (isalpha((unsigned char)path[0]) && path[1] == ':') ||
         path[0] == '/' || path[0] == '\\';
#else
  return path[0] == '/';
#endif
}

static char *quick_path_join(const char *a, const char *b) {
  if (!a || a[0] == '\0') {
    return quick_strdup(b ? b : "");
  }
  if (!b || b[0] == '\0') {
    return quick_strdup(a);
  }
  if (quick_path_is_absolute(b)) {
    return quick_strdup(b);
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

static char *quick_dirname_dup(const char *path) {
  if (!path || path[0] == '\0') {
    return quick_strdup(".");
  }
  const char *last = strrchr(path, '/');
  if (!last) {
    return quick_strdup(".");
  }
  if (last == path) {
    return quick_strdup("/");
  }
  size_t len = (size_t)(last - path);
  char *out = malloc(len + 1U);
  if (!out) {
    return NULL;
  }
  memcpy(out, path, len);
  out[len] = '\0';
  return out;
}

static const char *quick_basename_ptr(const char *path) {
  if (!path || path[0] == '\0') {
    return "site";
  }
  const char *end = path + strlen(path);
  while (end > path && end[-1] == '/') {
    end--;
  }
  const char *start = end;
  while (start > path && start[-1] != '/') {
    start--;
  }
  return start;
}

static char *quick_cwd_dup(void) {
#ifdef _WIN32
  return quick_strdup(".");
#else
  char tmp[PATH_MAX];
  if (!getcwd(tmp, sizeof(tmp))) {
    return NULL;
  }
  return quick_strdup(tmp);
#endif
}

static bool quick_file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  fclose(f);
  return true;
}

static app_error quick_mkdir_p(const char *path) {
#ifndef _WIN32
  if (!path || path[0] == '\0') {
    return APP_ERROR_INVALID_ARG;
  }
  char *copy = quick_strdup(path);
  if (!copy) {
    return APP_ERROR_MEMORY;
  }
  for (char *p = copy + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
        free(copy);
        return APP_ERROR_IO;
      }
      *p = '/';
    }
  }
  if (mkdir(copy, 0700) != 0 && errno != EEXIST) {
    free(copy);
    return APP_ERROR_IO;
  }
  free(copy);
#else
  (void)path;
#endif
  return APP_SUCCESS;
}

app_error quick_slug_normalize(const char *input,
                               char out[QUICK_SLUG_MAX + 1]) {
  if (!input || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  size_t used = 0;
  bool last_dash = false;
  for (const unsigned char *p = (const unsigned char *)input; *p != '\0'; p++) {
    unsigned char ch = *p;
    if (ch >= 'A' && ch <= 'Z') {
      ch = (unsigned char)tolower(ch);
    }
    bool emit_dash = false;
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
      if (used >= QUICK_SLUG_MAX) {
        break;
      }
      out[used++] = (char)ch;
      last_dash = false;
    } else if (ch == '-' || ch == '_' || ch == ' ' || ch == '.') {
      emit_dash = true;
    } else {
      emit_dash = true;
    }
    if (emit_dash && used > 0 && !last_dash && used < QUICK_SLUG_MAX) {
      out[used++] = '-';
      last_dash = true;
    }
  }
  while (used > 0 && out[used - 1] == '-') {
    used--;
  }
  out[used] = '\0';
  if (used == 0) {
    return APP_ERROR_VALIDATION;
  }
  return APP_SUCCESS;
}

bool quick_slug_is_valid(const char *slug) {
  if (!slug) {
    return false;
  }
  const size_t len = strlen(slug);
  if (len == 0 || len > QUICK_SLUG_MAX) {
    return false;
  }
  if (slug[0] == '-' || slug[len - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    const unsigned char ch = (unsigned char)slug[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) {
      return false;
    }
  }
  return true;
}

static bool quick_safe_chars_only(const char *value, const char *extra) {
  if (!value || value[0] == '\0') {
    return false;
  }
  for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
    const unsigned char ch = *p;
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9')) {
      continue;
    }
    bool allowed = false;
    for (const char *e = extra; e && *e != '\0'; e++) {
      if (ch == (unsigned char)*e) {
        allowed = true;
        break;
      }
    }
    if (!allowed) {
      return false;
    }
  }
  return true;
}

bool quick_profile_name_is_safe(const char *name) {
  return quick_safe_chars_only(name, "._-");
}

bool quick_ssh_target_is_safe(const char *target) {
  return quick_safe_chars_only(target, "._@:%+-");
}

static bool quick_path_has_dotdot_segment(const char *path) {
  const char *p = path;
  while (p && *p != '\0') {
    while (*p == '/') {
      p++;
    }
    const char *end = strchr(p, '/');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len == 2 && p[0] == '.' && p[1] == '.') {
      return true;
    }
    p = end;
  }
  return false;
}

bool quick_remote_path_is_safe(const char *path) {
  if (!path || path[0] != '/') {
    return false;
  }
  if (!quick_safe_chars_only(path, "/._:+-")) {
    return false;
  }
  return !quick_path_has_dotdot_segment(path);
}

bool quick_domain_is_safe(const char *domain) {
  if (!domain || domain[0] == '\0' || strlen(domain) > 253U) {
    return false;
  }
  if (strcmp(domain, "localhost") == 0) {
    return true;
  }
  const char *label = domain;
  while (*label != '\0') {
    const char *dot = strchr(label, '.');
    size_t len = dot ? (size_t)(dot - label) : strlen(label);
    if (len == 0 || len > 63U || label[0] == '-' || label[len - 1] == '-') {
      return false;
    }
    bool all_numeric = true;
    for (size_t i = 0; i < len; i++) {
      const unsigned char ch = (unsigned char)label[i];
      if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-')) {
        return false;
      }
      if (!(ch >= '0' && ch <= '9')) {
        all_numeric = false;
      }
    }
    (void)all_numeric;
    if (!dot) {
      break;
    }
    label = dot + 1;
  }
  return true;
}

void quick_deploy_plan_init(quick_deploy_plan_t *plan) {
  if (plan) {
    *plan = (quick_deploy_plan_t){0};
    quick_site_config_init(&plan->site_config);
  }
}

void quick_deploy_plan_destroy(quick_deploy_plan_t *plan) {
  if (!plan) {
    return;
  }
  free(plan->site_root);
  free(plan->quick_json_path);
  free(plan->source_dir);
  free(plan->output_dir);
  free(plan->ssh);
  free(plan->remote_root);
  free(plan->base_domain);
  free(plan->base_url);
  free(plan->url);
  quick_site_config_destroy(&plan->site_config);
  *plan = (quick_deploy_plan_t){0};
}

static const char *quick_first_nonempty(const char *a, const char *b) {
  return a && a[0] != '\0' ? a : b;
}

static char *quick_make_url(const char *subdomain, const char *base_domain,
                            const char *base_url) {
  if (base_domain && base_domain[0] != '\0') {
    const size_t len =
        strlen("https://") + strlen(subdomain) + 1U + strlen(base_domain) + 1U;
    char *url = malloc(len);
    if (!url) {
      return NULL;
    }
    snprintf(url, len, "https://%s.%s", subdomain, base_domain);
    return url;
  }
  const char *base =
      (base_url && base_url[0] != '\0') ? base_url : "http://localhost:9366/~";
  const size_t blen = strlen(base);
  const bool slash = blen > 0 && base[blen - 1] != '/';
  const size_t len = blen + (slash ? 1U : 0U) + strlen(subdomain) + 1U;
  char *url = malloc(len);
  if (!url) {
    return NULL;
  }
  snprintf(url, len, "%s%s%s", base, slash ? "/" : "", subdomain);
  return url;
}

static app_error quick_find_site_root(const char *path, char **root_out,
                                      char **quick_json_out,
                                      quick_site_config_t *site_config) {
  char *base = NULL;
  if (path && path[0] != '\0') {
    base = quick_path_is_absolute(path) ? quick_strdup(path)
                                        : quick_path_join(".", path);
  } else {
    base = quick_cwd_dup();
  }
  if (!base) {
    return APP_ERROR_MEMORY;
  }

  char *candidate = quick_path_join(base, "quick.json");
  if (!candidate) {
    free(base);
    return APP_ERROR_MEMORY;
  }
  if (quick_file_exists(candidate)) {
    app_error err = quick_site_config_load_file(candidate, site_config);
    if (err != APP_SUCCESS) {
      free(base);
      free(candidate);
      return err;
    }
    *root_out = base;
    *quick_json_out = candidate;
    return APP_SUCCESS;
  }

  free(candidate);
  char *parent = quick_dirname_dup(base);
  if (!parent) {
    free(base);
    return APP_ERROR_MEMORY;
  }
  candidate = quick_path_join(parent, "quick.json");
  if (!candidate) {
    free(parent);
    free(base);
    return APP_ERROR_MEMORY;
  }
  if (quick_file_exists(candidate)) {
    app_error err = quick_site_config_load_file(candidate, site_config);
    if (err != APP_SUCCESS) {
      free(parent);
      free(base);
      free(candidate);
      return err;
    }
    free(base);
    *root_out = parent;
    *quick_json_out = candidate;
    return APP_SUCCESS;
  }

  free(parent);
  free(candidate);
  *root_out = base;
  *quick_json_out = NULL;
  return APP_SUCCESS;
}

app_error quick_deploy_plan_resolve(const quick_plan_overrides_t *overrides,
                                    const quick_profile_config_t *profiles,
                                    quick_deploy_plan_t *plan) {
  if (!plan) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_deploy_plan_destroy(plan);
  quick_deploy_plan_init(plan);

  quick_site_config_t site;
  quick_site_config_init(&site);
  char *site_root = NULL;
  char *quick_json = NULL;
  app_error err = quick_find_site_root(overrides ? overrides->path : NULL,
                                       &site_root, &quick_json, &site);
  if (err != APP_SUCCESS) {
    quick_site_config_destroy(&site);
    return err;
  }

  const char *env_profile = getenv("QUICK_PROFILE");
  const char *env_site = getenv("QUICK_SITE");
  const char *env_remote = getenv("QUICK_REMOTE");
  const char *env_base_domain = getenv("QUICK_BASE_DOMAIN");

  const char *profile_name = quick_first_nonempty(
      overrides ? overrides->profile : NULL,
      quick_first_nonempty(
          env_profile,
          quick_first_nonempty(site.profile,
                               profiles ? profiles->default_profile : NULL)));
  if (!profile_name || profile_name[0] == '\0') {
    profile_name = "local";
  }
  snprintf(plan->profile, sizeof(plan->profile), "%s", profile_name);

  const quick_profile_t *profile =
      profiles ? quick_profile_config_find(profiles, profile_name) : NULL;

  const char *site_source =
      quick_first_nonempty(overrides ? overrides->site : NULL,
                           quick_first_nonempty(env_site, site.name));
  char normalized[QUICK_SLUG_MAX + 1];
  if (!site_source || site_source[0] == '\0') {
    const char *base = quick_basename_ptr(site_root);
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", base);
    size_t len = strlen(tmp);
    while (len > 0 && tmp[len - 1] == '/') {
      tmp[--len] = '\0';
    }
    err = quick_slug_normalize(tmp, normalized);
  } else {
    err = quick_slug_normalize(site_source, normalized);
  }
  if (err != APP_SUCCESS || !quick_slug_is_valid(normalized)) {
    free(site_root);
    free(quick_json);
    quick_site_config_destroy(&site);
    return APP_ERROR_VALIDATION;
  }
  snprintf(plan->site, sizeof(plan->site), "%s", normalized);

  const char *subdomain_source =
      quick_first_nonempty(overrides ? overrides->subdomain : NULL,
                           quick_first_nonempty(site.subdomain, plan->site));
  err = quick_slug_normalize(subdomain_source, normalized);
  if (err != APP_SUCCESS || !quick_slug_is_valid(normalized)) {
    free(site_root);
    free(quick_json);
    quick_site_config_destroy(&site);
    return APP_ERROR_VALIDATION;
  }
  snprintf(plan->subdomain, sizeof(plan->subdomain), "%s", normalized);

  const char *ssh = quick_first_nonempty(
      overrides ? overrides->remote : NULL,
      quick_first_nonempty(env_remote, profile ? profile->ssh : NULL));
  const char *remote_root =
      profile && profile->remote_root ? profile->remote_root : "/srv/quick";
  const char *base_domain = quick_first_nonempty(
      overrides ? overrides->base_domain : NULL,
      quick_first_nonempty(env_base_domain,
                           profile ? profile->base_domain : NULL));
  const char *base_url = profile ? profile->base_url : NULL;

  const char *source_rel =
      site.source && site.source[0] != '\0' ? site.source : ".";
  const char *output_rel =
      site.output && site.output[0] != '\0' ? site.output : ".";
  char *source_dir = quick_path_join(site_root, source_rel);
  char *output_dir = quick_path_join(source_dir, output_rel);
  char *url = quick_make_url(plan->subdomain, base_domain, base_url);
  if (!source_dir || !output_dir || !url ||
      (ssh && !quick_set_string(&plan->ssh, ssh)) ||
      !quick_set_string(&plan->remote_root, remote_root) ||
      (base_domain && !quick_set_string(&plan->base_domain, base_domain)) ||
      (base_url && !quick_set_string(&plan->base_url, base_url))) {
    free(site_root);
    free(quick_json);
    free(source_dir);
    free(output_dir);
    free(url);
    quick_site_config_destroy(&site);
    return APP_ERROR_MEMORY;
  }

  plan->site_root = site_root;
  plan->quick_json_path = quick_json;
  plan->source_dir = source_dir;
  plan->output_dir = output_dir;
  plan->url = url;
  plan->site_config = site;
  return APP_SUCCESS;
}

void quick_ignore_init(quick_ignore_t *ignore) {
  if (ignore) {
    *ignore = (quick_ignore_t){0};
  }
}

void quick_ignore_destroy(quick_ignore_t *ignore) {
  if (!ignore) {
    return;
  }
  for (size_t i = 0; i < ignore->count; i++) {
    free(ignore->patterns[i]);
  }
  *ignore = (quick_ignore_t){0};
}

static char *quick_trim_dup(const char *line) {
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  const char *end = line + strlen(line);
  while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' ||
                        end[-1] == '\t')) {
    end--;
  }
  size_t len = (size_t)(end - line);
  char *out = malloc(len + 1U);
  if (!out) {
    return NULL;
  }
  memcpy(out, line, len);
  out[len] = '\0';
  return out;
}

static app_error quick_ignore_add(quick_ignore_t *ignore, const char *pattern) {
  if (ignore->count >= QUICK_IGNORE_MAX_PATTERNS) {
    return APP_ERROR_OUT_OF_RANGE;
  }
  ignore->patterns[ignore->count] = quick_strdup(pattern);
  if (!ignore->patterns[ignore->count]) {
    return APP_ERROR_MEMORY;
  }
  ignore->count++;
  return APP_SUCCESS;
}

app_error quick_ignore_load_file(const char *path, quick_ignore_t *ignore) {
  if (!path || !ignore) {
    return APP_ERROR_INVALID_ARG;
  }
  FILE *stream = fopen(path, "rb");
  if (!stream) {
    return APP_ERROR_NOT_FOUND;
  }
  char line[1024];
  while (fgets(line, sizeof(line), stream)) {
    char *trimmed = quick_trim_dup(line);
    if (!trimmed) {
      fclose(stream);
      return APP_ERROR_MEMORY;
    }
    if (trimmed[0] != '\0' && trimmed[0] != '#') {
      app_error err = quick_ignore_add(ignore, trimmed);
      free(trimmed);
      if (err != APP_SUCCESS) {
        fclose(stream);
        return err;
      }
    } else {
      free(trimmed);
    }
  }
  fclose(stream);
  return APP_SUCCESS;
}

app_error quick_ignore_load_for_site(const char *site_root,
                                     quick_ignore_t *ignore) {
  if (!site_root || !ignore) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_ignore_destroy(ignore);
  quick_ignore_init(ignore);
  char *path = quick_path_join(site_root, ".quickignore");
  if (!path) {
    return APP_ERROR_MEMORY;
  }
  app_error err = quick_ignore_load_file(path, ignore);
  free(path);
  if (err == APP_ERROR_NOT_FOUND) {
    return APP_SUCCESS;
  }
  return err;
}

char **quick_ignore_to_rsync_args(const quick_ignore_t *ignore,
                                  size_t *argc_out) {
  if (argc_out) {
    *argc_out = 0;
  }
  if (!ignore || ignore->count == 0) {
    return NULL;
  }
  char **args = calloc(ignore->count, sizeof(char *));
  if (!args) {
    return NULL;
  }
  for (size_t i = 0; i < ignore->count; i++) {
    const char *prefix = "--exclude=";
    size_t len = strlen(prefix) + strlen(ignore->patterns[i]) + 1U;
    args[i] = malloc(len);
    if (!args[i]) {
      quick_ignore_args_destroy(args, i);
      return NULL;
    }
    snprintf(args[i], len, "%s%s", prefix, ignore->patterns[i]);
  }
  if (argc_out) {
    *argc_out = ignore->count;
  }
  return args;
}

void quick_ignore_args_destroy(char **args, size_t argc) {
  if (!args) {
    return;
  }
  for (size_t i = 0; i < argc; i++) {
    free(args[i]);
  }
  free(args);
}

void quick_deployment_record_init(quick_deployment_record_t *record) {
  if (record) {
    *record = (quick_deployment_record_t){0};
  }
}

void quick_deployment_record_destroy(quick_deployment_record_t *record) {
  if (!record) {
    return;
  }
  free(record->profile);
  free(record->site);
  free(record->url);
  free(record->release);
  free(record->deployed_at);
  *record = (quick_deployment_record_t){0};
}

static char *quick_deployment_path(const char *site_root, const char *profile) {
  char *quick_dir = quick_path_join(site_root, ".quick/deployments");
  if (!quick_dir) {
    return NULL;
  }
  size_t len = strlen(profile) + strlen(".json") + 1U;
  char *file = malloc(len);
  if (!file) {
    free(quick_dir);
    return NULL;
  }
  snprintf(file, len, "%s.json", profile);
  char *path = quick_path_join(quick_dir, file);
  free(file);
  free(quick_dir);
  return path;
}

static void quick_write_json_string(FILE *stream, const char *value) {
  fputc('"', stream);
  for (const unsigned char *p = (const unsigned char *)(value ? value : "");
       *p != '\0'; p++) {
    switch (*p) {
    case '"':
      fputs("\\\"", stream);
      break;
    case '\\':
      fputs("\\\\", stream);
      break;
    case '\n':
      fputs("\\n", stream);
      break;
    case '\r':
      fputs("\\r", stream);
      break;
    case '\t':
      fputs("\\t", stream);
      break;
    default:
      if (*p < 0x20) {
        fprintf(stream, "\\u%04x", *p);
      } else {
        fputc(*p, stream);
      }
      break;
    }
  }
  fputc('"', stream);
}

static void quick_utc_now(char out[32]) {
#ifdef _WIN32
  snprintf(out, 32, "unknown");
#else
  time_t now = time(NULL);
  struct tm tm;
  if (!gmtime_r(&now, &tm)) {
    snprintf(out, 32, "unknown");
    return;
  }
  strftime(out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
#endif
}

app_error quick_local_state_write_deployment(const char *site_root,
                                             const char *profile,
                                             const char *site, const char *url,
                                             const char *release) {
  if (!site_root || !profile || !site || !url || !release) {
    return APP_ERROR_INVALID_ARG;
  }
  char *dir = quick_path_join(site_root, ".quick/deployments");
  if (!dir) {
    return APP_ERROR_MEMORY;
  }
  app_error err = quick_mkdir_p(dir);
  free(dir);
  if (err != APP_SUCCESS) {
    return err;
  }
  char *path = quick_deployment_path(site_root, profile);
  if (!path) {
    return APP_ERROR_MEMORY;
  }
  FILE *stream = fopen(path, "wb");
  free(path);
  if (!stream) {
    return APP_ERROR_IO;
  }
  char now[32];
  quick_utc_now(now);
  fputs("{\n  \"profile\": ", stream);
  quick_write_json_string(stream, profile);
  fputs(",\n  \"site\": ", stream);
  quick_write_json_string(stream, site);
  fputs(",\n  \"url\": ", stream);
  quick_write_json_string(stream, url);
  fputs(",\n  \"release\": ", stream);
  quick_write_json_string(stream, release);
  fputs(",\n  \"deployed_at\": ", stream);
  quick_write_json_string(stream, now);
  fputs("\n}\n", stream);
  if (fclose(stream) != 0) {
    return APP_ERROR_IO;
  }
  return APP_SUCCESS;
}

static app_error quick_parse_deployment(quick_deployment_record_t *record,
                                        const char *content) {
  const char *p = quick_json_skip_ws(content);
  app_error err = quick_json_expect_char(&p, '{');
  if (err != APP_SUCCESS) {
    return err;
  }
  p = quick_json_skip_ws(p);
  if (*p == '}') {
    return quick_json_finish(p + 1);
  }
  while (*p != '\0') {
    char *key = NULL;
    err = quick_json_read_string_alloc(&p, &key);
    if (err != APP_SUCCESS) {
      return err;
    }
    err = quick_json_expect_char(&p, ':');
    if (err != APP_SUCCESS) {
      free(key);
      return err;
    }
    char *value = NULL;
    if (strcmp(key, "profile") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_set_string(&record->profile, value);
        free(value);
      }
    } else if (strcmp(key, "site") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_set_string(&record->site, value);
        free(value);
      }
    } else if (strcmp(key, "url") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_set_string(&record->url, value);
        free(value);
      }
    } else if (strcmp(key, "release") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_set_string(&record->release, value);
        free(value);
      }
    } else if (strcmp(key, "deployed_at") == 0) {
      err = quick_json_read_string_or_null(&p, &value);
      if (err == APP_SUCCESS) {
        quick_set_string(&record->deployed_at, value);
        free(value);
      }
    } else {
      err = quick_json_skip_value(&p, 0);
    }
    free(key);
    if (err != APP_SUCCESS) {
      return err;
    }
    p = quick_json_skip_ws(p);
    if (*p == ',') {
      p++;
      continue;
    }
    if (*p == '}') {
      return quick_json_finish(p + 1);
    }
    return APP_ERROR_CONFIG_PARSE;
  }
  return APP_ERROR_CONFIG_PARSE;
}

app_error quick_local_state_read_deployment(const char *site_root,
                                            const char *profile,
                                            quick_deployment_record_t *record) {
  if (!site_root || !profile || !record) {
    return APP_ERROR_INVALID_ARG;
  }
  char *path = quick_deployment_path(site_root, profile);
  if (!path) {
    return APP_ERROR_MEMORY;
  }
  FILE *stream = fopen(path, "rb");
  free(path);
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
  char *content = malloc((size_t)size + 1U);
  if (!content) {
    fclose(stream);
    return APP_ERROR_MEMORY;
  }
  const size_t read_len = fread(content, 1, (size_t)size, stream);
  fclose(stream);
  if (read_len != (size_t)size) {
    free(content);
    return APP_ERROR_IO;
  }
  content[read_len] = '\0';
  quick_deployment_record_t parsed;
  quick_deployment_record_init(&parsed);
  app_error err = quick_parse_deployment(&parsed, content);
  free(content);
  if (err != APP_SUCCESS) {
    quick_deployment_record_destroy(&parsed);
    return err;
  }
  quick_deployment_record_destroy(record);
  *record = parsed;
  return APP_SUCCESS;
}
