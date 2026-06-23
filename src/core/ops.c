#include "ops.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "app_info.h"
#include "json_util.h"
#include "site_config.h"
#include "types.h"

static char *quick_ops_strdup(const char *value) {
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

static bool quick_ops_set_string(char **slot, const char *value) {
  char *copy = value ? quick_ops_strdup(value) : NULL;
  if (value && !copy) {
    return false;
  }
  free(*slot);
  *slot = copy;
  return true;
}

static char *quick_ops_path_join(const char *a, const char *b) {
  if (!a || a[0] == '\0') {
    return quick_ops_strdup(b ? b : "");
  }
  if (!b || b[0] == '\0') {
    return quick_ops_strdup(a);
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

static bool quick_ops_path_exists(const char *path) {
  if (!path) {
    return false;
  }
#ifdef _WIN32
  FILE *f = fopen(path, "rb");
  if (!f) {
    return false;
  }
  fclose(f);
  return true;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

static bool quick_ops_dir_exists(const char *path) {
#ifdef _WIN32
  return quick_ops_path_exists(path);
#else
  struct stat st;
  return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static app_error quick_ops_mkdir_p(const char *path, int mode) {
#ifdef _WIN32
  (void)path;
  (void)mode;
  return APP_SUCCESS;
#else
  if (!path) {
    return APP_ERROR_INVALID_ARG;
  }
  char *copy = quick_ops_strdup(path);
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

static char *quick_ops_find_executable(const char *name) {
  if (!name || name[0] == '\0') {
    return NULL;
  }
  if (strchr(name, '/')) {
    return quick_ops_path_exists(name) ? quick_ops_strdup(name) : NULL;
  }
  const char *path_env = getenv("PATH");
  if (!path_env) {
    return NULL;
  }
  char *paths = quick_ops_strdup(path_env);
  if (!paths) {
    return NULL;
  }
  char *save = NULL;
  for (char *dir = strtok_r(paths, ":", &save); dir;
       dir = strtok_r(NULL, ":", &save)) {
    char *candidate = quick_ops_path_join(dir, name);
    if (candidate && quick_ops_path_exists(candidate)) {
      free(paths);
      return candidate;
    }
    free(candidate);
  }
  free(paths);
  return NULL;
}

static const char *quick_ops_json_find_field_token(const char *json,
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

static char *quick_ops_json_get_string_field(const char *json,
                                             const char *field) {
  const char *p = quick_ops_json_find_field_token(json, field);
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

static bool quick_ops_json_get_bool_field(const char *json, const char *field,
                                          bool *out) {
  const char *p = quick_ops_json_find_field_token(json, field);
  if (!p || !out) {
    return false;
  }
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
    p++;
  }
  if (strncmp(p, "true", 4) == 0) {
    *out = true;
    return true;
  }
  if (strncmp(p, "false", 5) == 0) {
    *out = false;
    return true;
  }
  return false;
}

static long quick_ops_json_get_long_field(const char *json, const char *field,
                                          long fallback) {
  const char *p = quick_ops_json_find_field_token(json, field);
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

static app_error quick_ops_write_text_file(const char *path,
                                           const char *content,
                                           bool fail_if_exists) {
  if (fail_if_exists && quick_ops_path_exists(path)) {
    return APP_ERROR_CONFIG_INVALID;
  }
  FILE *stream = fopen(path, "wb");
  if (!stream) {
    return APP_ERROR_IO;
  }
  fputs(content, stream);
  if (fclose(stream) != 0) {
    return APP_ERROR_IO;
  }
  return APP_SUCCESS;
}

static app_error quick_ops_append_string(char ***items, size_t *count,
                                         const char *value) {
  char **grown = realloc(*items, (*count + 1U) * sizeof(char *));
  if (!grown) {
    return APP_ERROR_MEMORY;
  }
  *items = grown;
  (*items)[*count] = quick_ops_strdup(value ? value : "");
  if (!(*items)[*count]) {
    return APP_ERROR_MEMORY;
  }
  (*count)++;
  return APP_SUCCESS;
}

void quick_init_result_init(quick_init_result_t *result) {
  if (result) {
    *result = (quick_init_result_t){0};
  }
}

void quick_init_result_destroy(quick_init_result_t *result) {
  if (!result) {
    return;
  }
  free(result->site);
  free(result->path);
  free(result->profile);
  for (size_t i = 0; i < result->file_count; i++) {
    free(result->files_created[i]);
  }
  free(result->files_created);
  *result = (quick_init_result_t){0};
}

static const char *quick_init_blank_html(const char *name) {
  (void)name;
  return "<!doctype html>\n"
         "<html lang=\"en\">\n"
         "<head>\n"
         "  <meta charset=\"utf-8\">\n"
         "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
         "  <title>OpenQuick site</title>\n"
         "  <style>body{font-family:system-ui,sans-serif;max-width:48rem;margin:4rem auto;padding:0 1rem}</style>\n"
         "</head>\n"
         "<body>\n"
         "  <h1>OpenQuick site</h1>\n"
         "  <p>Edit this folder, then run <code>quick deploy</code>.</p>\n"
         "  <script type=\"module\">\n"
         "    import { quick } from '/_quick/sdk.js';\n"
         "    quick.identity?.current?.().then((me) => console.log('OpenQuick identity', me)).catch(() => {});\n"
         "  </script>\n"
         "</body>\n"
         "</html>\n";
}

static const char *quick_init_realtime_html(void) {
  return "<!doctype html>\n"
         "<html lang=\"en\">\n"
         "<head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>OpenQuick realtime</title></head>\n"
         "<body>\n"
         "  <h1>Realtime OpenQuick site</h1>\n"
         "  <p>This static page is ready to call <code>/_quick/sdk.js</code> when the host enables realtime APIs.</p>\n"
         "  <script type=\"module\">\n"
         "    import { quick } from '/_quick/sdk.js';\n"
         "    const me = await quick.identity.current();\n"
         "    document.body.insertAdjacentHTML('beforeend', `<p>Signed in as ${me.email || me.login || me.subject}</p>`);\n"
         "  </script>\n"
         "</body>\n"
         "</html>\n";
}

static const char *quick_init_agents_md(void) {
  return "# OpenQuick site agent guide\n\n"
         "## Contract\n\n"
         "- Read `quick.json` first. Write deployable static files to its `output` directory.\n"
         "- Keep `source`, `output`, `build`, `name`, `profile`, and `subdomain` consistent with the files you create.\n"
         "- Never create or require a custom server, daemon, cron job, function, or backend.\n"
         "- Never put secrets, API keys, provider tokens, or database URLs in client code, `quick.json`, or deployable files.\n"
         "- Treat `.quick/` as local state. Do not edit it as source.\n\n"
         "## SDK\n\n"
         "Import the same-origin SDK:\n\n"
         "```js\n"
         "import { quick } from '/_quick/sdk.js';\n"
         "```\n\n"
         "Use `quick.identity`, `quick.db`, `quick.realtime`, and `quick.uploads` directly. Gate optional AI and warehouse controls:\n\n"
         "```js\n"
         "const caps = await quick.capabilities(); if (caps.ai) { /* quick.ai.chat(...) */ } if (caps.warehouse) { /* quick.warehouse.query(...) */ }\n"
         "```\n\n"
         "## Deploy loop\n\n"
         "Edit files, then run:\n\n"
         "```bash\n"
         "quick serve --dev      # local preview before a host exists\n"
         "quick deploy --dry-run  # when uncertain\n"
         "quick deploy\n"
         "quick open --plain\n"
         "```\n\n"
         "If this is your first OpenQuick site and no deployment profile exists yet, install or connect a host first:\n\n"
         "```bash\n"
         "quick serve install --profile <profile> --host <user@host> --remote-root /srv/quick --domain quick.example.com --iap tailscale\n"
         "```\n\n"
         "## Names and URLs\n\n"
         "- Site names and subdomains are DNS labels: lowercase `a-z0-9-`, no leading or trailing hyphen, max 63 chars.\n"
         "- The URL is deterministic from the profile: usually `https://<subdomain>.<base_domain>` or a profile `base_url` fallback.\n\n"
         "## Troubleshooting\n\n"
         "```bash\n"
         "quick doctor --json\n"
         "```\n";
}

static const char *quick_init_api_md(void) {
  return "# OpenQuick API reference\n\n"
         "OpenQuick serves host APIs from the same origin as the static site. Import the SDK from `/_quick/sdk.js`.\n\n"
         "```js\n"
         "import { quick } from '/_quick/sdk.js';\n"
         "```\n\n"
         "## Base SDK surface\n\n"
         "Use these APIs as the base OpenQuick surface:\n\n"
         "- `quick.identity.current()` and `quick.identity.onChange(cb)` call `GET /_quick/identity`.\n"
         "- `quick.db.collection(name)` calls `/_quick/db/:collection` for create, get, update, list, remove, and subscriptions.\n"
         "- `quick.realtime.channel(name)` uses the shared WebSocket at `/_quick/realtime`.\n"
         "- `quick.uploads.put(file)`, `get(id)`, and `remove(id)` call `/_quick/uploads`.\n"
         "- `quick.capabilities()` calls `GET /_quick/capabilities`.\n\n"
         "## Optional host-gated APIs\n\n"
         "AI and warehouse are disabled unless the host advertises them. Gate UI before calling them:\n\n"
         "```js\n"
         "const caps = await quick.capabilities(); if (caps.ai) { /* quick.ai.chat(...) */ } if (caps.warehouse) { /* quick.warehouse.query(...) */ }\n"
         "```\n\n"
         "- `quick.ai.chat(messages, options)` calls `POST /_quick/ai/chat`.\n"
         "- `quick.ai.image(prompt, options)` calls `POST /_quick/ai/images`.\n"
         "- `quick.warehouse.query(name, params)` calls `POST /_quick/warehouse/:name`.\n\n"
         "Provider keys, database credentials, and warehouse SQL stay server-side. Do not put secrets in client code.\n";
}

static app_error quick_init_write_quick_json(const char *path,
                                             const char *name,
                                             const char *profile) {
  quick_site_config_t site;
  quick_site_config_init(&site);
  site.name = quick_ops_strdup(name);
  site.source = quick_ops_strdup(".");
  site.output = quick_ops_strdup(".");
  site.build = NULL;
  site.profile = profile && profile[0] != '\0' ? quick_ops_strdup(profile) : NULL;
  site.subdomain = quick_ops_strdup(name);
  site.sdk.enabled = true;
  site.sdk.has_enabled = true;
  site.sdk.import = quick_ops_strdup("/_quick/sdk.js");
  if (!site.name || !site.source || !site.output || !site.subdomain ||
      !site.sdk.import || (profile && profile[0] != '\0' && !site.profile)) {
    quick_site_config_destroy(&site);
    return APP_ERROR_MEMORY;
  }
  app_error err = quick_site_config_write_file(path, &site);
  quick_site_config_destroy(&site);
  return err;
}

app_error quick_op_init(const quick_init_request_t *request,
                        quick_init_result_t *out) {
  if (!request || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_init_result_destroy(out);
  quick_init_result_init(out);
  const char *dir = request->target_dir && request->target_dir[0] != '\0'
                        ? request->target_dir
                        : ".";
  char slug[QUICK_SLUG_MAX + 1];
  app_error err = quick_slug_normalize(request->name ? request->name : dir, slug);
  if (err != APP_SUCCESS) {
    return err;
  }

  err = quick_ops_mkdir_p(dir, 0755);
  if (err != APP_SUCCESS) {
    return err;
  }

  char *docs_dir = quick_ops_path_join(dir, "docs");
  char *index_path = quick_ops_path_join(dir, "index.html");
  char *quick_json = quick_ops_path_join(dir, "quick.json");
  char *agents = quick_ops_path_join(dir, "AGENTS.md");
  char *api = docs_dir ? quick_ops_path_join(docs_dir, "openquick-api.md") : NULL;
  char *ignore = quick_ops_path_join(dir, ".quickignore");
  if (!docs_dir || !index_path || !quick_json || !agents || !api || !ignore) {
    free(docs_dir);
    free(index_path);
    free(quick_json);
    free(agents);
    free(api);
    free(ignore);
    return APP_ERROR_MEMORY;
  }

  const char *targets[] = {index_path, quick_json, agents, api, ignore};
  if (!request->adopt_existing) {
    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
      if (quick_ops_path_exists(targets[i])) {
        free(docs_dir);
        free(index_path);
        free(quick_json);
        free(agents);
        free(api);
        free(ignore);
        return APP_ERROR_CONFIG_INVALID;
      }
    }
  }

  err = quick_ops_mkdir_p(docs_dir, 0755);
  if (err == APP_SUCCESS && !(request->adopt_existing && quick_ops_path_exists(index_path))) {
    err = quick_ops_write_text_file(
        index_path,
        request->template_kind == QUICK_INIT_TEMPLATE_REALTIME
            ? quick_init_realtime_html()
            : quick_init_blank_html(slug),
        true);
    if (err == APP_SUCCESS) {
      (void)quick_ops_append_string(&out->files_created, &out->file_count,
                                    index_path);
    }
  }
  if (err == APP_SUCCESS && !(request->adopt_existing && quick_ops_path_exists(quick_json))) {
    err = quick_init_write_quick_json(quick_json, slug, request->profile);
    if (err == APP_SUCCESS) {
      (void)quick_ops_append_string(&out->files_created, &out->file_count,
                                    quick_json);
    }
  }
  if (err == APP_SUCCESS && !(request->adopt_existing && quick_ops_path_exists(agents))) {
    err = quick_ops_write_text_file(agents, quick_init_agents_md(), true);
    if (err == APP_SUCCESS) {
      (void)quick_ops_append_string(&out->files_created, &out->file_count,
                                    agents);
    }
  }
  if (err == APP_SUCCESS && !(request->adopt_existing && quick_ops_path_exists(api))) {
    err = quick_ops_write_text_file(api, quick_init_api_md(), true);
    if (err == APP_SUCCESS) {
      (void)quick_ops_append_string(&out->files_created, &out->file_count, api);
    }
  }
  if (err == APP_SUCCESS && !(request->adopt_existing && quick_ops_path_exists(ignore))) {
    err = quick_ops_write_text_file(
        ignore, ".git/\n.quick/\nnode_modules/\n.DS_Store\n.env\n.env.*\n", true);
    if (err == APP_SUCCESS) {
      (void)quick_ops_append_string(&out->files_created, &out->file_count,
                                    ignore);
    }
  }
  if (err == APP_SUCCESS) {
    out->site = quick_ops_strdup(slug);
    out->path = quick_ops_strdup(dir);
    out->profile = quick_ops_strdup(request->profile ? request->profile : "");
    if (!out->site || !out->path || !out->profile) {
      err = APP_ERROR_MEMORY;
    }
  }

  free(docs_dir);
  free(index_path);
  free(quick_json);
  free(agents);
  free(api);
  free(ignore);
  if (err != APP_SUCCESS) {
    return err;
  }
  return APP_SUCCESS;
}

void quick_deploy_result_init(quick_deploy_result_t *result) {
  if (result) {
    *result = (quick_deploy_result_t){0};
  }
}

void quick_deploy_result_destroy(quick_deploy_result_t *result) {
  if (!result) {
    return;
  }
  free(result->release);
  free(result->url);
  free(result->failure_message);
  free(result->bootstrap_install_command);
  free(result->last_deployer);
  free(result->last_release);
  free(result->last_deployed_at);
  free(result->cleanup_path);
  free(result->cleanup_message);
  *result = (quick_deploy_result_t){0};
}

char *quick_op_default_deployer_identity(void) {
  const char *user = getenv("USER");
  if (!user || user[0] == '\0') {
    user = getenv("LOGNAME");
  }
  if (!user || user[0] == '\0') {
    user = "unknown";
  }
#ifndef _WIN32
  char host[256];
  if (gethostname(host, sizeof(host)) == 0) {
    host[sizeof(host) - 1U] = '\0';
    if (host[0] != '\0') {
      int needed = snprintf(NULL, 0, "%s@%s", user, host);
      char *identity = needed >= 0 ? malloc((size_t)needed + 1U) : NULL;
      if (identity) {
        snprintf(identity, (size_t)needed + 1U, "%s@%s", user, host);
        return identity;
      }
    }
  }
#endif
  return quick_ops_strdup(user);
}

static app_error quick_deploy_set_failure(quick_deploy_result_t *out,
                                          quick_deploy_phase_t phase,
                                          const char *message) {
  if (!out) {
    return APP_ERROR_INVALID_ARG;
  }
  out->failure_phase = phase;
  if (message && !quick_ops_set_string(&out->failure_message, message)) {
    return APP_ERROR_MEMORY;
  }
  return APP_SUCCESS;
}

static void quick_deploy_cleanup_staging(const quick_deploy_plan_t *plan,
                                         const char *deploy_id,
                                         const char *staging,
                                         quick_deploy_result_t *out) {
  if (!plan || !deploy_id || !staging || !out || !plan->ssh ||
      plan->ssh[0] == '\0') {
    return;
  }
  out->cleanup_attempted = true;
  (void)quick_ops_set_string(&out->cleanup_path, staging);
  char *const argv[] = {"ssh", (char *)plan->ssh, "quickd", "deploy",
                        "cleanup", "--site", (char *)plan->site,
                        "--deploy-id", (char *)deploy_id, "--json", NULL};
  quick_process_result_t res = {0};
  app_error err = quick_process_capture(argv, NULL, &res);
  if (err == APP_SUCCESS && res.exit_code == 0) {
    out->cleanup_ok = true;
    (void)quick_ops_set_string(&out->cleanup_message, "remote staging cleaned");
  } else {
    out->cleanup_ok = false;
    const char *detail = res.err && res.err[0] != '\0'
                             ? res.err
                             : (res.out && res.out[0] != '\0'
                                    ? res.out
                                    : "remote cleanup command failed");
    size_t len = strlen(detail) + strlen(staging) + strlen(plan->ssh) +
                 strlen(plan->site) + strlen(deploy_id) + 160U;
    char *message = malloc(len);
    if (message) {
      snprintf(message, len,
               "%s; staging remains at %s; cleanup command: ssh %s quickd deploy cleanup --site %s --deploy-id %s",
               detail, staging, plan->ssh, plan->site, deploy_id);
      (void)quick_ops_set_string(&out->cleanup_message, message);
      free(message);
    }
  }
  quick_process_result_destroy(&res);
}

static bool quick_deploy_cancelled(const quick_deploy_options_t *options) {
  return options && options->cancel_flag && *options->cancel_flag;
}

static app_error quick_deploy_check_cancelled(
    const quick_deploy_options_t *options, quick_deploy_result_t *out,
    quick_deploy_phase_t phase) {
  if (!quick_deploy_cancelled(options)) {
    return APP_SUCCESS;
  }
  app_error err = quick_deploy_set_failure(out, phase, "operation cancelled");
  return err == APP_SUCCESS ? APP_ERROR_INTERRUPTED : err;
}

static void quick_deploy_emit_phase(quick_deploy_progress_cb cb,
                                    void *userdata,
                                    quick_deploy_phase_t phase,
                                    const char *message) {
  if (cb) {
    cb(phase, QUICK_STREAM_STDOUT, message ? message : "", userdata);
  }
}

static const char *quick_deploy_profile_iap_type(
    const quick_profile_config_t *profiles, const quick_deploy_plan_t *plan) {
  const quick_profile_t *profile =
      profiles ? quick_profile_config_find(profiles, plan->profile) : NULL;
  if (profile && profile->iap.type && profile->iap.type[0] != '\0') {
    return profile->iap.type;
  }
  return "tailscale";
}

static app_error quick_deploy_install_command(
    const quick_profile_config_t *profiles, const quick_deploy_plan_t *plan,
    char **out) {
  if (!plan || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  *out = NULL;
  const char *iap = quick_deploy_profile_iap_type(profiles, plan);
  const char *ssh = plan->ssh ? plan->ssh : "<ssh>";
  const char *root = plan->remote_root ? plan->remote_root : "/srv/quick";
  int needed = 0;
  if (plan->base_domain && plan->base_domain[0] != '\0') {
    needed = snprintf(NULL, 0,
                      "quick serve install --profile %s --host %s --remote-root %s "
                      "--domain %s --iap %s",
                      plan->profile, ssh, root, plan->base_domain, iap);
  } else {
    needed = snprintf(NULL, 0,
                      "quick serve install --profile %s --host %s --remote-root %s "
                      "--iap %s",
                      plan->profile, ssh, root, iap);
  }
  if (needed < 0) {
    return APP_ERROR_INTERNAL;
  }
  char *buf = malloc((size_t)needed + 1U);
  if (!buf) {
    return APP_ERROR_MEMORY;
  }
  if (plan->base_domain && plan->base_domain[0] != '\0') {
    snprintf(buf, (size_t)needed + 1U,
             "quick serve install --profile %s --host %s --remote-root %s "
             "--domain %s --iap %s",
             plan->profile, ssh, root, plan->base_domain, iap);
  } else {
    snprintf(buf, (size_t)needed + 1U,
             "quick serve install --profile %s --host %s --remote-root %s "
             "--iap %s",
             plan->profile, ssh, root, iap);
  }
  *out = buf;
  return APP_SUCCESS;
}

static bool quick_deploy_has_successful_record(
    const quick_deploy_plan_t *plan) {
  quick_deployment_record_t record;
  quick_deployment_record_init(&record);
  bool ok = quick_local_state_read_deployment(plan->site_root, plan->profile,
                                              &record) == APP_SUCCESS &&
            record.release && record.release[0] != '\0' && record.url &&
            record.url[0] != '\0';
  quick_deployment_record_destroy(&record);
  return ok;
}

static bool quick_deploy_text_has_command_not_found(const char *text) {
  return text && (strstr(text, "command not found") ||
                  strstr(text, "not found") || strstr(text, "No such file"));
}

static bool quick_deploy_doctor_has_status(const char *json,
                                           const char *status) {
  if (!json || !status) {
    return false;
  }
  char needle[64];
  snprintf(needle, sizeof(needle), "\"status\":\"%s\"", status);
  if (strstr(json, needle)) {
    return true;
  }
  snprintf(needle, sizeof(needle), "\"status\": \"%s\"", status);
  return strstr(json, needle) != NULL;
}

static bool quick_deploy_doctor_has_publication_issue(const char *json) {
  if (!json) {
    return false;
  }
  const bool has_issue_status = quick_deploy_doctor_has_status(json, "warn") ||
                                quick_deploy_doctor_has_status(json, "fail");
  if (!has_issue_status) {
    return false;
  }
  return strstr(json, "edge/iap") || strstr(json, "domain") ||
         strstr(json, "iap") || strstr(json, "publication");
}

static app_error quick_deploy_first_bootstrap_check(
    const quick_profile_config_t *profiles, const quick_deploy_plan_t *plan,
    const quick_deploy_options_t *options, quick_deploy_result_t *out) {
  if (quick_deploy_has_successful_record(plan)) {
    return APP_SUCCESS;
  }
  char *const doctor_argv[] = {"ssh", (char *)plan->ssh, "quickd", "doctor",
                               "--json", NULL};
  quick_process_result_t doctor = {0};
  app_error err = quick_process_stream_cancelable(
      doctor_argv, NULL, NULL, NULL, NULL,
      options ? options->cancel_flag : NULL, &doctor);
  if (err != APP_SUCCESS) {
    quick_process_result_destroy(&doctor);
    if (err == APP_ERROR_INTERRUPTED) {
      (void)quick_deploy_set_failure(out,
                                     QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
                                     "operation cancelled");
    } else {
      (void)quick_deploy_set_failure(
          out, QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
          "failed to run remote quickd doctor over ssh");
    }
    return err;
  }
  if (doctor.exit_code != 0) {
    const bool missing = doctor.exit_code == 127 ||
                         quick_deploy_text_has_command_not_found(doctor.err) ||
                         quick_deploy_text_has_command_not_found(doctor.out);
    if (missing) {
      char *remediation = NULL;
      err = quick_deploy_install_command(profiles, plan, &remediation);
      if (err != APP_SUCCESS) {
        quick_process_result_destroy(&doctor);
        return err;
      }
      out->bootstrap_missing = true;
      out->bootstrap_install_command = quick_ops_strdup(remediation);
      if (!out->bootstrap_install_command) {
        free(remediation);
        quick_process_result_destroy(&doctor);
        return APP_ERROR_MEMORY;
      }
      if (options && options->bootstrap) {
        int needed = snprintf(NULL, 0, "quickd is missing on %s; running %s",
                              plan->ssh, remediation);
        char *msg = NULL;
        if (needed >= 0) {
          msg = malloc((size_t)needed + 1U);
        }
        if (!msg) {
          free(remediation);
          quick_process_result_destroy(&doctor);
          return APP_ERROR_MEMORY;
        }
        snprintf(msg, (size_t)needed + 1U,
                 "quickd is missing on %s; running %s", plan->ssh,
                 remediation);
        err = quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
                                       msg);
        free(msg);
      } else {
        err = quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
                                       remediation);
      }
      free(remediation);
      quick_process_result_destroy(&doctor);
      return err == APP_SUCCESS ? APP_ERROR_IO : err;
    }
    err = quick_deploy_set_failure(
        out, QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
        doctor.err && doctor.err[0] ? doctor.err : "remote quickd doctor failed");
    quick_process_result_destroy(&doctor);
    return err == APP_SUCCESS ? APP_ERROR_IO : err;
  }
  if (quick_deploy_doctor_has_status(doctor.out, "fail")) {
    char *remediation = NULL;
    err = quick_deploy_install_command(profiles, plan, &remediation);
    if (err != APP_SUCCESS) {
      quick_process_result_destroy(&doctor);
      return err;
    }
    int needed = snprintf(NULL, 0,
                          "/srv/quick is missing or not writable; fix permissions "
                          "or run `%s`",
                          remediation);
    char *msg = needed >= 0 ? malloc((size_t)needed + 1U) : NULL;
    if (!msg) {
      free(remediation);
      quick_process_result_destroy(&doctor);
      return APP_ERROR_MEMORY;
    }
    snprintf(msg, (size_t)needed + 1U,
             "/srv/quick is missing or not writable; fix permissions or run `%s`",
             remediation);
    err = quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
                                   msg);
    free(msg);
    free(remediation);
    quick_process_result_destroy(&doctor);
    return err == APP_SUCCESS ? APP_ERROR_VALIDATION : err;
  }
  if (quick_deploy_doctor_has_publication_issue(doctor.out) &&
      (!options || !options->allow_unpublished)) {
    out->publication_issue = true;
    quick_process_result_destroy(&doctor);
    err = quick_deploy_set_failure(
        out, QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
        "IAP/domain is not configured; pass --allow-unpublished to deploy before publication is complete.");
    return err == APP_SUCCESS ? APP_ERROR_VALIDATION : err;
  }
  quick_process_result_destroy(&doctor);
  return APP_SUCCESS;
}

typedef struct {
  quick_deploy_phase_t phase;
  quick_deploy_progress_cb cb;
  void *ud;
} quick_deploy_stream_bridge_ctx_t;

static void quick_deploy_stream_bridge(quick_stream_kind_t stream,
                                       const char *line, void *userdata) {
  quick_deploy_stream_bridge_ctx_t *ctx = userdata;
  if (ctx && ctx->cb) {
    ctx->cb(ctx->phase, stream, line, ctx->ud);
  }
}

static void quick_ops_argv_destroy(char **argv) {
  if (!argv) {
    return;
  }
  for (size_t i = 0; argv[i]; i++) {
    free(argv[i]);
  }
  free(argv);
}

static app_error quick_ops_token_append(char **token, size_t *used,
                                        size_t *capacity, char ch) {
  if (*used + 1U >= *capacity) {
    size_t next = *capacity ? *capacity * 2U : 32U;
    char *grown = realloc(*token, next);
    if (!grown) {
      return APP_ERROR_MEMORY;
    }
    *token = grown;
    *capacity = next;
  }
  (*token)[(*used)++] = ch;
  (*token)[*used] = '\0';
  return APP_SUCCESS;
}

static app_error quick_ops_split_command(const char *command, char ***argv_out) {
  if (!command || !argv_out) {
    return APP_ERROR_INVALID_ARG;
  }
  *argv_out = NULL;
  char **argv = NULL;
  size_t argc = 0;
  const char *p = command;
  while (*p != '\0') {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    char *token = NULL;
    size_t used = 0;
    size_t capacity = 0;
    bool single_quote = false;
    bool double_quote = false;
    while (*p != '\0' && (single_quote || double_quote ||
                          (*p != ' ' && *p != '\t' && *p != '\n' &&
                           *p != '\r'))) {
      char ch = *p++;
      if (ch == '\'' && !double_quote) {
        single_quote = !single_quote;
        continue;
      }
      if (ch == '"' && !single_quote) {
        double_quote = !double_quote;
        continue;
      }
      if (ch == '\\' && *p != '\0') {
        ch = *p++;
      }
      app_error err = quick_ops_token_append(&token, &used, &capacity, ch);
      if (err != APP_SUCCESS) {
        free(token);
        quick_ops_argv_destroy(argv);
        return err;
      }
    }
    if (single_quote || double_quote) {
      free(token);
      quick_ops_argv_destroy(argv);
      return APP_ERROR_VALIDATION;
    }
    char **grown = realloc(argv, (argc + 2U) * sizeof(char *));
    if (!grown) {
      free(token);
      quick_ops_argv_destroy(argv);
      return APP_ERROR_MEMORY;
    }
    argv = grown;
    argv[argc++] = token ? token : quick_ops_strdup("");
    if (!argv[argc - 1U]) {
      quick_ops_argv_destroy(argv);
      return APP_ERROR_MEMORY;
    }
    argv[argc] = NULL;
  }
  if (!argv) {
    return APP_ERROR_VALIDATION;
  }
  *argv_out = argv;
  return APP_SUCCESS;
}

static app_error quick_deploy_run_build_if_needed(
    const quick_deploy_plan_t *plan, const quick_deploy_options_t *options,
    quick_deploy_progress_cb cb, void *userdata, quick_deploy_result_t *out) {
  if ((options && options->no_build) || !plan->site_config.build ||
      plan->site_config.build[0] == '\0') {
    return APP_SUCCESS;
  }
  if (cb) {
    int needed = snprintf(NULL, 0, "Running build: %s\n",
                          plan->site_config.build);
    char *msg = needed >= 0 ? malloc((size_t)needed + 1U) : NULL;
    if (!msg) {
      return APP_ERROR_MEMORY;
    }
    snprintf(msg, (size_t)needed + 1U, "Running build: %s\n",
             plan->site_config.build);
    cb(QUICK_DEPLOY_PHASE_BUILD, QUICK_STREAM_STDOUT, msg, userdata);
    free(msg);
  }
  char **argv = NULL;
  app_error err = quick_ops_split_command(plan->site_config.build, &argv);
  if (err != APP_SUCCESS) {
    (void)quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_BUILD,
                                   "build command failed");
    return err;
  }
  quick_process_result_t res = {0};
  quick_deploy_stream_bridge_ctx_t bridge = {
      .phase = QUICK_DEPLOY_PHASE_BUILD, .cb = cb, .ud = userdata};
  err = quick_process_stream_cancelable(
      argv, plan->source_dir, NULL, cb ? quick_deploy_stream_bridge : NULL,
      &bridge, options ? options->cancel_flag : NULL, &res);
  quick_ops_argv_destroy(argv);
  if (err != APP_SUCCESS) {
    if (err == APP_ERROR_INTERRUPTED) {
      (void)quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_BUILD,
                                     "operation cancelled");
    }
    quick_process_result_destroy(&res);
    return err;
  }
  if (res.exit_code != 0) {
    quick_process_result_destroy(&res);
    err = quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_BUILD,
                                   "build command failed");
    return err == APP_SUCCESS ? APP_ERROR_IO : err;
  }
  quick_process_result_destroy(&res);
  return APP_SUCCESS;
}

static bool quick_ops_has_suffix(const char *value, const char *suffix) {
  if (!value || !suffix) {
    return false;
  }
  const size_t vlen = strlen(value);
  const size_t slen = strlen(suffix);
  return vlen >= slen && strcmp(value + vlen - slen, suffix) == 0;
}

static bool quick_deploy_same_deployer(const char *last,
                                       const char *current,
                                       const quick_deploy_plan_t *plan) {
  if (!last || last[0] == '\0') {
    return true;
  }
  if (current && current[0] != '\0' && strcmp(last, current) == 0) {
    return true;
  }
  const char *user = getenv("USER");
  if (!user || user[0] == '\0') {
    user = getenv("LOGNAME");
  }
  if (user && user[0] != '\0' && strcmp(last, user) == 0) {
    return true;
  }
  if (plan && plan->ssh) {
    const char *at = strchr(plan->ssh, '@');
    if (at && at != plan->ssh) {
      size_t len = (size_t)(at - plan->ssh);
      if (strlen(last) == len && strncmp(last, plan->ssh, len) == 0) {
        return true;
      }
    }
  }
  return false;
}

static char *quick_deploy_remote_upload_path(const char *staging) {
  if (!staging || staging[0] == '\0') {
    return quick_ops_strdup("upload.zip");
  }
  const char *slash = strrchr(staging, '/');
  if (!slash || slash == staging) {
    const char *prefix = slash == staging ? "/" : "";
    size_t len = strlen(prefix) + strlen("upload.zip") + 1U;
    char *out = malloc(len);
    if (out) {
      snprintf(out, len, "%supload.zip", prefix);
    }
    return out;
  }
  size_t parent_len = (size_t)(slash - staging);
  size_t len = parent_len + strlen("/upload.zip") + 1U;
  char *out = malloc(len);
  if (!out) {
    return NULL;
  }
  memcpy(out, staging, parent_len);
  memcpy(out + parent_len, "/upload.zip", strlen("/upload.zip") + 1U);
  return out;
}

static const char *quick_deploy_env_or_option(const char *option,
                                              const char *env_name) {
  if (option && option[0] != '\0') {
    return option;
  }
  const char *value = getenv(env_name);
  return value && value[0] != '\0' ? value : NULL;
}

void quick_op_deploy_parse_rsync_counts(const char *output, long *changed,
                                        long *reused, long *deleted) {
  if (changed) {
    *changed = 0;
  }
  if (reused) {
    *reused = 0;
  }
  if (deleted) {
    *deleted = 0;
  }
  if (!output) {
    return;
  }
  const char *line = output;
  while (*line != '\0') {
    const char *next = strchr(line, '\n');
    size_t len = next ? (size_t)(next - line) : strlen(line);
    if (len >= 9 && strncmp(line, "*deleting", 9) == 0) {
      if (deleted) {
        (*deleted)++;
      }
    } else if (len > 0 &&
               (line[0] == '>' || line[0] == 'c' || line[0] == 'h')) {
      if (changed) {
        (*changed)++;
      }
    }
    if (!next) {
      break;
    }
    line = next + 1;
  }
}

app_error quick_op_deploy_execute(const app_config_t *config,
                                  const quick_profile_config_t *profiles,
                                  const quick_deploy_plan_t *plan,
                                  const quick_deploy_options_t *options,
                                  quick_deploy_progress_cb cb, void *userdata,
                                  quick_deploy_result_t *out) {
  (void)config;
  if (!plan || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_deploy_result_destroy(out);
  quick_deploy_result_init(out);
  quick_deploy_options_t default_options = {0};
  if (!options) {
    options = &default_options;
  }
  const bool zip_deploy = options->zip_path && options->zip_path[0] != '\0' &&
                          quick_ops_has_suffix(options->zip_path, ".zip");
  out->zip_deploy = zip_deploy;

  app_error err = quick_deploy_check_cancelled(
      options, out, QUICK_DEPLOY_PHASE_NONE);
  if (err != APP_SUCCESS) {
    return err;
  }
  quick_deploy_emit_phase(cb, userdata, QUICK_DEPLOY_PHASE_BUILD,
                          "checking build step\n");
  err = quick_deploy_run_build_if_needed(plan, options, cb, userdata, out);
  if (err != APP_SUCCESS) {
    return err;
  }
  if (!zip_deploy && !quick_ops_dir_exists(plan->output_dir)) {
    err = quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_BUILD,
                                   "build output directory does not exist");
    return err == APP_SUCCESS ? APP_ERROR_NOT_FOUND : err;
  }

  err = quick_deploy_check_cancelled(options, out, QUICK_DEPLOY_PHASE_BUILD);
  if (err != APP_SUCCESS) {
    return err;
  }
  quick_deploy_emit_phase(cb, userdata, QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
                          "checking host bootstrap\n");

  if (!plan->ssh || plan->ssh[0] == '\0') {
    err = quick_deploy_set_failure(
        out, QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK,
        "No SSH host resolved; configure a profile or QUICK_REMOTE");
    return err == APP_SUCCESS ? APP_ERROR_CONFIG_INVALID : err;
  }

  err = quick_deploy_first_bootstrap_check(profiles, plan, options, out);
  if (err != APP_SUCCESS) {
    return err;
  }

  err = quick_deploy_check_cancelled(options, out,
                                     QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK);
  if (err != APP_SUCCESS) {
    return err;
  }
  quick_deploy_emit_phase(cb, userdata, QUICK_DEPLOY_PHASE_PREPARE,
                          "preparing remote staging area\n");

  char *default_deployer = NULL;
  const char *deployer = options->deployer;
  if (!deployer || deployer[0] == '\0') {
    default_deployer = quick_op_default_deployer_identity();
    deployer = default_deployer;
  }
  const char *ssh_key_id = quick_deploy_env_or_option(options->ssh_key_id,
                                                      "QUICK_SSH_KEY_ID");
  const char *ssh_principals = quick_deploy_env_or_option(
      options->ssh_principals, "QUICK_SSH_PRINCIPALS");

  char *prepare_argv[32];
  size_t prepare_argc = 0;
  prepare_argv[prepare_argc++] = "ssh";
  prepare_argv[prepare_argc++] = (char *)plan->ssh;
  prepare_argv[prepare_argc++] = "quickd";
  prepare_argv[prepare_argc++] = "deploy";
  prepare_argv[prepare_argc++] = "prepare";
  prepare_argv[prepare_argc++] = "--site";
  prepare_argv[prepare_argc++] = (char *)plan->site;
  prepare_argv[prepare_argc++] = "--subdomain";
  prepare_argv[prepare_argc++] = (char *)plan->subdomain;
  if (deployer && deployer[0] != '\0') {
    prepare_argv[prepare_argc++] = "--deployer";
    prepare_argv[prepare_argc++] = (char *)deployer;
  }
  if (ssh_key_id) {
    prepare_argv[prepare_argc++] = "--ssh-key-id";
    prepare_argv[prepare_argc++] = (char *)ssh_key_id;
  }
  if (ssh_principals) {
    prepare_argv[prepare_argc++] = "--ssh-principals";
    prepare_argv[prepare_argc++] = (char *)ssh_principals;
  }
  prepare_argv[prepare_argc++] = "--json";
  prepare_argv[prepare_argc] = NULL;
  quick_process_result_t prepare = {0};
  err = quick_process_stream_cancelable(prepare_argv, NULL, NULL, NULL, NULL,
                                        options->cancel_flag, &prepare);
  if (err != APP_SUCCESS) {
    if (err == APP_ERROR_INTERRUPTED) {
      (void)quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_PREPARE,
                                     "operation cancelled");
    }
    free(default_deployer);
    return err;
  }
  if (prepare.exit_code != 0) {
    err = quick_deploy_set_failure(
        out, QUICK_DEPLOY_PHASE_PREPARE,
        prepare.err && prepare.err[0] ? prepare.err : "quickd prepare failed");
    quick_process_result_destroy(&prepare);
    free(default_deployer);
    return err == APP_SUCCESS ? APP_ERROR_IO : err;
  }
  char *deploy_id = quick_ops_json_get_string_field(prepare.out, "deploy_id");
  char *staging = quick_ops_json_get_string_field(prepare.out, "staging_path");
  char *link_dest = quick_ops_json_get_string_field(prepare.out, "link_dest");
  char *last_deployer = quick_ops_json_get_string_field(prepare.out,
                                                        "last_deployer");
  char *last_release = quick_ops_json_get_string_field(prepare.out,
                                                       "last_release");
  char *last_deployed_at = quick_ops_json_get_string_field(
      prepare.out, "last_deployed_at");
  if (last_deployer) {
    (void)quick_ops_set_string(&out->last_deployer, last_deployer);
  }
  if (last_release) {
    (void)quick_ops_set_string(&out->last_release, last_release);
  }
  if (last_deployed_at) {
    (void)quick_ops_set_string(&out->last_deployed_at, last_deployed_at);
  }
  quick_process_result_destroy(&prepare);
  if (!deploy_id || !staging) {
    free(deploy_id);
    free(staging);
    free(link_dest);
    free(last_deployer);
    free(last_release);
    free(last_deployed_at);
    free(default_deployer);
    err = quick_deploy_set_failure(
        out, QUICK_DEPLOY_PHASE_PREPARE,
        "quickd prepare response missed deploy_id or staging_path");
    return err == APP_SUCCESS ? APP_ERROR_INVALID_DATA : err;
  }
  if (last_deployer &&
      !quick_deploy_same_deployer(last_deployer, deployer, plan) &&
      !options->assume_yes && !options->overwrite_confirmed) {
    out->overwrite_confirmation_required = true;
    char msg[512];
    if (last_release && last_release[0]) {
      snprintf(msg, sizeof(msg),
               "Site '%s' was last deployed by %s (release %s); type the site name to confirm overwrite or pass --yes.",
               plan->site, last_deployer, last_release);
    } else {
      snprintf(msg, sizeof(msg),
               "Site '%s' was last deployed by %s; type the site name to confirm overwrite or pass --yes.",
               plan->site, last_deployer);
    }
    err = quick_deploy_set_failure(out, QUICK_DEPLOY_PHASE_PREPARE, msg);
    quick_deploy_cleanup_staging(plan, deploy_id, staging, out);
    free(deploy_id);
    free(staging);
    free(link_dest);
    free(last_deployer);
    free(last_release);
    free(last_deployed_at);
    free(default_deployer);
    return err == APP_SUCCESS ? APP_ERROR_VALIDATION : err;
  }
  free(last_deployer);
  free(last_release);
  free(last_deployed_at);

  err = quick_deploy_check_cancelled(options, out, QUICK_DEPLOY_PHASE_PREPARE);
  if (err != APP_SUCCESS) {
    quick_deploy_cleanup_staging(plan, deploy_id, staging, out);
    free(deploy_id);
    free(staging);
    free(link_dest);
    free(default_deployer);
    return err;
  }
  quick_deploy_emit_phase(cb, userdata, QUICK_DEPLOY_PHASE_TRANSFER,
                          zip_deploy ? "uploading zip\n" : "transferring files\n");

  if (zip_deploy) {
    char *remote_zip = quick_deploy_remote_upload_path(staging);
    char *scp_dest = remote_zip
                         ? malloc(strlen(plan->ssh) + 1U + strlen(remote_zip) + 1U)
                         : NULL;
    if (!remote_zip || !scp_dest) {
      free(remote_zip);
      free(scp_dest);
      free(deploy_id);
      free(staging);
      free(link_dest);
      free(default_deployer);
      return APP_ERROR_MEMORY;
    }
    sprintf(scp_dest, "%s:%s", plan->ssh, remote_zip);
    char *const scp_argv[] = {"scp", (char *)options->zip_path, scp_dest,
                              NULL};
    quick_process_result_t scp = {0};
    quick_deploy_stream_bridge_ctx_t transfer_bridge = {
        .phase = QUICK_DEPLOY_PHASE_TRANSFER,
        .cb = cb,
        .ud = userdata,
    };
    err = quick_process_stream_cancelable(
        scp_argv, NULL, NULL, cb ? quick_deploy_stream_bridge : NULL,
        &transfer_bridge, options->cancel_flag, &scp);
    if (err != APP_SUCCESS || scp.exit_code != 0) {
      app_error set_err = quick_deploy_set_failure(
          out, QUICK_DEPLOY_PHASE_TRANSFER,
          scp.err && scp.err[0] ? scp.err : "scp failed");
      quick_process_result_destroy(&scp);
      quick_deploy_cleanup_staging(plan, deploy_id, staging, out);
      free(remote_zip);
      free(scp_dest);
      free(deploy_id);
      free(staging);
      free(link_dest);
      free(default_deployer);
      if (set_err != APP_SUCCESS) {
        return set_err;
      }
      return err == APP_SUCCESS ? APP_ERROR_IO : err;
    }
    quick_process_result_destroy(&scp);

    char *const extract_argv[] = {"ssh", (char *)plan->ssh, "quickd",
                                  "deploy", "extract-zip", "--site",
                                  (char *)plan->site, "--deploy-id",
                                  deploy_id, "--zip", remote_zip, "--json",
                                  NULL};
    quick_process_result_t extract = {0};
    err = quick_process_stream_cancelable(extract_argv, NULL, NULL, NULL, NULL,
                                          options->cancel_flag, &extract);
    if (err != APP_SUCCESS || extract.exit_code != 0) {
      app_error set_err = quick_deploy_set_failure(
          out, QUICK_DEPLOY_PHASE_TRANSFER,
          extract.err && extract.err[0] ? extract.err
                                        : "quickd extract-zip failed");
      quick_process_result_destroy(&extract);
      quick_deploy_cleanup_staging(plan, deploy_id, staging, out);
      free(remote_zip);
      free(scp_dest);
      free(deploy_id);
      free(staging);
      free(link_dest);
      free(default_deployer);
      if (set_err != APP_SUCCESS) {
        return set_err;
      }
      return err == APP_SUCCESS ? APP_ERROR_IO : err;
    }
    quick_process_result_destroy(&extract);
    out->changed = 1;
    free(remote_zip);
    free(scp_dest);
  } else {
    quick_ignore_t ignore;
    quick_ignore_init(&ignore);
    err = quick_ignore_load_for_site(plan->site_root, &ignore);
    if (err != APP_SUCCESS) {
      quick_deploy_cleanup_staging(plan, deploy_id, staging, out);
      free(deploy_id);
      free(staging);
      free(link_dest);
      free(default_deployer);
      return err;
    }
    size_t exclude_argc = 0;
    char **exclude_args = quick_ignore_to_rsync_args(&ignore, &exclude_argc);

    const size_t max_args = 32U + exclude_argc;
    char **rsync_argv = calloc(max_args, sizeof(char *));
    if (!rsync_argv) {
      quick_ignore_args_destroy(exclude_args, exclude_argc);
      quick_ignore_destroy(&ignore);
      free(deploy_id);
      free(staging);
      free(link_dest);
      free(default_deployer);
      return APP_ERROR_MEMORY;
    }
    size_t ai = 0;
    rsync_argv[ai++] = "rsync";
    rsync_argv[ai++] = "-az";
    rsync_argv[ai++] = "--itemize-changes";
    rsync_argv[ai++] = "--stats";
    if (!options->no_delete) {
      rsync_argv[ai++] = "--delete";
    }
    if (options->checksum) {
      rsync_argv[ai++] = "--checksum";
    }
    rsync_argv[ai++] = "--partial-dir=.rsync-partial";
    rsync_argv[ai++] = "--safe-links";
    rsync_argv[ai++] = "--chmod=Dg+s,ug+rwX,o-rwx";
    char *link_arg = NULL;
    if (link_dest && link_dest[0] != '\0') {
      size_t len = strlen("--link-dest=") + strlen(link_dest) + 1U;
      link_arg = malloc(len);
      if (!link_arg) {
        free(rsync_argv);
        quick_ignore_args_destroy(exclude_args, exclude_argc);
        quick_ignore_destroy(&ignore);
        free(deploy_id);
        free(staging);
        free(link_dest);
        free(default_deployer);
        return APP_ERROR_MEMORY;
      }
      snprintf(link_arg, len, "--link-dest=%s", link_dest);
      rsync_argv[ai++] = link_arg;
    }
    for (size_t i = 0; i < exclude_argc; i++) {
      rsync_argv[ai++] = exclude_args[i];
    }
    char *source = quick_ops_path_join(plan->output_dir, "");
    char *dest = malloc(strlen(plan->ssh) + 1U + strlen(staging) + 2U);
    if (!source || !dest) {
      free(source);
      free(dest);
      free(link_arg);
      free(rsync_argv);
      quick_ignore_args_destroy(exclude_args, exclude_argc);
      quick_ignore_destroy(&ignore);
      free(deploy_id);
      free(staging);
      free(link_dest);
      free(default_deployer);
      return APP_ERROR_MEMORY;
    }
    size_t slen = strlen(source);
    if (slen == 0 || source[slen - 1] != '/') {
      char *with_slash = malloc(slen + 2U);
      if (!with_slash) {
        free(source);
        free(dest);
        free(link_arg);
        free(rsync_argv);
        quick_ignore_args_destroy(exclude_args, exclude_argc);
        quick_ignore_destroy(&ignore);
        free(deploy_id);
        free(staging);
        free(link_dest);
        free(default_deployer);
        return APP_ERROR_MEMORY;
      }
      sprintf(with_slash, "%s/", source);
      free(source);
      source = with_slash;
    }
    sprintf(dest, "%s:%s/", plan->ssh, staging);
    rsync_argv[ai++] = source;
    rsync_argv[ai++] = dest;
    rsync_argv[ai] = NULL;

    quick_process_result_t rsync = {0};
    quick_deploy_stream_bridge_ctx_t transfer_bridge = {
        .phase = QUICK_DEPLOY_PHASE_TRANSFER,
        .cb = cb,
        .ud = userdata,
    };
    err = quick_process_stream_cancelable(
        rsync_argv, NULL, NULL, cb ? quick_deploy_stream_bridge : NULL,
        &transfer_bridge, options->cancel_flag, &rsync);
    quick_op_deploy_parse_rsync_counts(rsync.out, &out->changed, &out->reused,
                                       &out->deleted);
    if (err != APP_SUCCESS || rsync.exit_code != 0) {
      const bool transfer_cancelled =
          err == APP_ERROR_INTERRUPTED || rsync.exit_code == 130 ||
          rsync.exit_code == 143;
      app_error set_err = quick_deploy_set_failure(
          out, QUICK_DEPLOY_PHASE_TRANSFER,
          transfer_cancelled
              ? "operation cancelled"
              : (rsync.err && rsync.err[0] ? rsync.err : "rsync failed"));
      quick_process_result_destroy(&rsync);
      quick_deploy_cleanup_staging(plan, deploy_id, staging, out);
      free(source);
      free(dest);
      free(link_arg);
      free(rsync_argv);
      quick_ignore_args_destroy(exclude_args, exclude_argc);
      quick_ignore_destroy(&ignore);
      free(deploy_id);
      free(staging);
      free(link_dest);
      free(default_deployer);
      if (set_err != APP_SUCCESS) {
        return set_err;
      }
      return transfer_cancelled ? APP_ERROR_INTERRUPTED
                                : (err == APP_SUCCESS ? APP_ERROR_IO : err);
    }
    quick_process_result_destroy(&rsync);
    free(source);
    free(dest);
    free(link_arg);
    free(rsync_argv);
    quick_ignore_args_destroy(exclude_args, exclude_argc);
    quick_ignore_destroy(&ignore);
  }

  err = quick_deploy_check_cancelled(options, out, QUICK_DEPLOY_PHASE_TRANSFER);
  if (err != APP_SUCCESS) {
    quick_deploy_cleanup_staging(plan, deploy_id, staging, out);
    free(deploy_id);
    free(staging);
    free(link_dest);
    free(default_deployer);
    return err;
  }
  quick_deploy_emit_phase(cb, userdata, QUICK_DEPLOY_PHASE_ACTIVATE,
                          "activating release\n");

  char *activate_argv[32];
  size_t activate_argc = 0;
  activate_argv[activate_argc++] = "ssh";
  activate_argv[activate_argc++] = (char *)plan->ssh;
  activate_argv[activate_argc++] = "quickd";
  activate_argv[activate_argc++] = "deploy";
  activate_argv[activate_argc++] = "activate";
  activate_argv[activate_argc++] = "--site";
  activate_argv[activate_argc++] = (char *)plan->site;
  activate_argv[activate_argc++] = "--subdomain";
  activate_argv[activate_argc++] = (char *)plan->subdomain;
  activate_argv[activate_argc++] = "--deploy-id";
  activate_argv[activate_argc++] = deploy_id;
  if (deployer && deployer[0] != '\0') {
    activate_argv[activate_argc++] = "--deployer";
    activate_argv[activate_argc++] = (char *)deployer;
  }
  if (ssh_key_id) {
    activate_argv[activate_argc++] = "--ssh-key-id";
    activate_argv[activate_argc++] = (char *)ssh_key_id;
  }
  if (ssh_principals) {
    activate_argv[activate_argc++] = "--ssh-principals";
    activate_argv[activate_argc++] = (char *)ssh_principals;
  }
  activate_argv[activate_argc++] = "--json";
  activate_argv[activate_argc] = NULL;
  quick_process_result_t activate = {0};
  err = quick_process_stream_cancelable(activate_argv, NULL, NULL, NULL, NULL,
                                        options->cancel_flag, &activate);
  if (err != APP_SUCCESS || activate.exit_code != 0) {
    app_error set_err = quick_deploy_set_failure(
        out, QUICK_DEPLOY_PHASE_ACTIVATE,
        activate.err && activate.err[0] ? activate.err
                                        : "quickd activate failed");
    quick_process_result_destroy(&activate);
    quick_deploy_cleanup_staging(plan, deploy_id, staging, out);
    free(deploy_id);
    free(staging);
    free(link_dest);
    free(default_deployer);
    if (set_err != APP_SUCCESS) {
      return set_err;
    }
    return err == APP_SUCCESS ? APP_ERROR_IO : err;
  }
  char *release = quick_ops_json_get_string_field(activate.out, "release");
  char *url = quick_ops_json_get_string_field(activate.out, "url");
  quick_process_result_destroy(&activate);
  if (!release) {
    release = quick_ops_strdup(deploy_id);
  }
  if (!url) {
    url = quick_ops_strdup(plan->url);
  }
  if (!release || !url) {
    free(release);
    free(url);
    free(deploy_id);
    free(staging);
    free(link_dest);
    free(default_deployer);
    return APP_ERROR_MEMORY;
  }
  err = quick_deploy_check_cancelled(options, out, QUICK_DEPLOY_PHASE_ACTIVATE);
  if (err != APP_SUCCESS) {
    free(release);
    free(url);
    free(deploy_id);
    free(staging);
    free(link_dest);
    free(default_deployer);
    return err;
  }
  quick_deploy_emit_phase(cb, userdata, QUICK_DEPLOY_PHASE_RECORD,
                          "recording deployment\n");
  (void)quick_local_state_write_deployment(plan->site_root, plan->profile,
                                           plan->site, url, release);
  out->release = release;
  out->url = url;

  free(deploy_id);
  free(staging);
  free(link_dest);
  free(default_deployer);
  return APP_SUCCESS;
}

static void quick_list_item_destroy(quick_list_item_t *item) {
  if (!item) {
    return;
  }
  free(item->name);
  free(item->url);
  free(item->release);
  free(item->updated_at);
  free(item->deployer);
  free(item->subdomain);
  *item = (quick_list_item_t){0};
}

void quick_list_result_init(quick_list_result_t *result) {
  if (result) {
    *result = (quick_list_result_t){0};
  }
}

void quick_list_result_destroy(quick_list_result_t *result) {
  if (!result) {
    return;
  }
  for (size_t i = 0; i < result->count; i++) {
    quick_list_item_destroy(&result->items[i]);
  }
  free(result->items);
  free(result->remote_json);
  free(result->remote_error);
  free(result->remote_phase);
  free(result->remote_remediation);
  free(result->site);
  free(result->profile);
  free(result->url);
  *result = (quick_list_result_t){0};
}

static app_error quick_list_append_item(quick_list_result_t *result,
                                        const char *name, const char *url,
                                        const char *release,
                                        const char *updated_at,
                                        const char *deployer,
                                        const char *subdomain,
                                        bool have_public, bool is_public,
                                        bool stale,
                                        quick_list_source_t source) {
  quick_list_item_t *grown =
      realloc(result->items, (result->count + 1U) * sizeof(quick_list_item_t));
  if (!grown) {
    return APP_ERROR_MEMORY;
  }
  result->items = grown;
  quick_list_item_t *item = &result->items[result->count];
  *item = (quick_list_item_t){0};
  item->name = quick_ops_strdup(name ? name : "");
  item->url = quick_ops_strdup(url ? url : "");
  item->release = release ? quick_ops_strdup(release) : NULL;
  item->updated_at = updated_at ? quick_ops_strdup(updated_at) : NULL;
  item->deployer = quick_ops_strdup(deployer ? deployer : "");
  item->subdomain = subdomain ? quick_ops_strdup(subdomain) : NULL;
  item->stale = stale;
  item->have_public = have_public;
  item->is_public = is_public;
  item->source = source;
  if (!item->name || !item->url || !item->deployer ||
      (release && !item->release) || (updated_at && !item->updated_at) ||
      (subdomain && !item->subdomain)) {
    quick_list_item_destroy(item);
    return APP_ERROR_MEMORY;
  }
  result->count++;
  return APP_SUCCESS;
}

static void quick_list_parse_remote_items(const char *json,
                                          quick_list_result_t *result) {
  if (!json || !result) {
    return;
  }
  const char *p = json;
  while ((p = strstr(p, "\"name\"")) != NULL) {
    const char *obj_end = strchr(p, '}');
    if (!obj_end) {
      break;
    }
    size_t len = (size_t)(obj_end - p) + 1U;
    char *snippet = malloc(len + 1U);
    if (!snippet) {
      return;
    }
    memcpy(snippet, p, len);
    snippet[len] = '\0';
    char *name = quick_ops_json_get_string_field(snippet, "name");
    char *url = quick_ops_json_get_string_field(snippet, "url");
    char *release = quick_ops_json_get_string_field(snippet, "release");
    char *updated_at = quick_ops_json_get_string_field(snippet, "updated_at");
    char *deployer = quick_ops_json_get_string_field(snippet, "deployer");
    char *subdomain = quick_ops_json_get_string_field(snippet, "subdomain");
    bool is_public = false;
    bool have_public = quick_ops_json_get_bool_field(snippet, "public",
                                                     &is_public);
    if (name && url) {
      (void)quick_list_append_item(result, name, url, release, updated_at,
                                   deployer ? deployer : "remote", subdomain,
                                   have_public, is_public, false,
                                   QUICK_LIST_SOURCE_REMOTE);
    }
    free(name);
    free(url);
    free(release);
    free(updated_at);
    free(deployer);
    free(subdomain);
    free(snippet);
    p = obj_end + 1;
  }
}

app_error quick_op_list(const quick_list_request_t *request,
                        quick_list_result_t *out) {
  if (!request || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_list_result_destroy(out);
  quick_list_result_init(out);

  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_deploy_plan_resolve(&request->overrides,
                                            request->profiles, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  out->site = quick_ops_strdup(plan.site);
  out->profile = quick_ops_strdup(plan.profile);
  out->url = quick_ops_strdup(plan.url);
  if (!out->site || !out->profile || !out->url) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }

  quick_deployment_record_t local;
  quick_deployment_record_init(&local);
  out->have_local = quick_local_state_read_deployment(plan.site_root,
                                                      plan.profile, &local) ==
                    APP_SUCCESS;
  out->remote_requested = request->remote || (plan.ssh && plan.ssh[0] != '\0');

  if (out->remote_requested && plan.ssh && plan.ssh[0] != '\0') {
    char *const ssh_argv[] = {"ssh", plan.ssh, "quickd", "list", "--json",
                              NULL};
    quick_process_result_t res = {0};
    app_error remote_err = quick_process_capture(ssh_argv, NULL, &res);
    if (remote_err == APP_SUCCESS && res.exit_code == 0 && res.out &&
        res.out[0] != '\0') {
      out->remote_json = quick_ops_strdup(res.out);
      if (!out->remote_json) {
        quick_process_result_destroy(&res);
        quick_deployment_record_destroy(&local);
        quick_deploy_plan_destroy(&plan);
        return APP_ERROR_MEMORY;
      }
      out->remote_ok = true;
      quick_list_parse_remote_items(out->remote_json, out);
    } else {
      const char *detail = res.err && res.err[0] != '\0'
                               ? res.err
                               : (res.out && res.out[0] != '\0' ? res.out
                                                                 : "remote list failed");
      out->remote_error = quick_ops_strdup(detail);
      out->remote_phase = quick_ops_strdup("ssh");
      out->remote_remediation = quick_ops_strdup(
          "check the profile SSH host, run `quick doctor --remote`, or retry later");
      if (!out->remote_error || !out->remote_phase || !out->remote_remediation) {
        quick_process_result_destroy(&res);
        quick_deployment_record_destroy(&local);
        quick_deploy_plan_destroy(&plan);
        return APP_ERROR_MEMORY;
      }
    }
    quick_process_result_destroy(&res);
  } else if (out->remote_requested) {
    out->remote_error = quick_ops_strdup("remote host is not configured");
    out->remote_phase = quick_ops_strdup("resolve");
    out->remote_remediation = quick_ops_strdup(
        "set QUICK_REMOTE or configure profiles.<name>.ssh, then run `quick config show`");
    if (!out->remote_error || !out->remote_phase || !out->remote_remediation) {
      quick_deployment_record_destroy(&local);
      quick_deploy_plan_destroy(&plan);
      return APP_ERROR_MEMORY;
    }
  }

  if (out->have_local) {
    err = quick_list_append_item(out, local.site ? local.site : plan.site,
                                 local.url ? local.url : plan.url,
                                 local.release, local.deployed_at, "local",
                                 plan.subdomain, false, false,
                                 !out->remote_ok && out->remote_requested,
                                 QUICK_LIST_SOURCE_LOCAL);
  }

  quick_deployment_record_destroy(&local);
  quick_deploy_plan_destroy(&plan);
  return err;
}

void quick_remote_site_info_init(quick_remote_site_info_t *info) {
  if (info) {
    *info = (quick_remote_site_info_t){0};
  }
}

void quick_remote_site_info_destroy(quick_remote_site_info_t *info) {
  if (!info) {
    return;
  }
  free(info->name);
  free(info->subdomain);
  free(info->url);
  free(info->release);
  free(info->updated_at);
  free(info->deployer);
  free(info->raw_json);
  *info = (quick_remote_site_info_t){0};
}

static app_error quick_remote_site_info_set(quick_remote_site_info_t *info,
                                            const char *json,
                                            const char *fallback_site) {
  if (!info) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_remote_site_info_destroy(info);
  quick_remote_site_info_init(info);
  info->raw_json = quick_ops_strdup(json ? json : "");
  char *name = quick_ops_json_get_string_field(json, "name");
  if (!name) {
    name = quick_ops_json_get_string_field(json, "site");
  }
  info->name = name ? name : quick_ops_strdup(fallback_site ? fallback_site : "");
  info->subdomain = quick_ops_json_get_string_field(json, "subdomain");
  info->url = quick_ops_json_get_string_field(json, "url");
  info->release = quick_ops_json_get_string_field(json, "release");
  info->updated_at = quick_ops_json_get_string_field(json, "updated_at");
  if (!info->updated_at) {
    info->updated_at = quick_ops_json_get_string_field(json, "last_deployed_at");
  }
  info->deployer = quick_ops_json_get_string_field(json, "deployer");
  if (!info->deployer) {
    info->deployer = quick_ops_json_get_string_field(json, "last_deployer");
  }
  bool value = false;
  if (quick_ops_json_get_bool_field(json, "public", &value)) {
    info->have_public = true;
    info->is_public = value;
  }
  if (!info->raw_json || !info->name) {
    return APP_ERROR_MEMORY;
  }
  return APP_SUCCESS;
}

static app_error quick_ops_resolve_remote_plan(
    const quick_profile_config_t *profiles, const char *profile,
    const char *site, quick_deploy_plan_t *plan) {
  quick_plan_overrides_t overrides = {.profile = profile, .site = site};
  app_error err = quick_deploy_plan_resolve(&overrides, profiles, plan);
  if (err != APP_SUCCESS) {
    return err;
  }
  if (!plan->ssh || plan->ssh[0] == '\0') {
    return APP_ERROR_CONFIG_INVALID;
  }
  return APP_SUCCESS;
}

static app_error quick_ops_fetch_site(const quick_deploy_plan_t *plan,
                                      quick_remote_site_info_t *info) {
  char *const argv[] = {"ssh", (char *)plan->ssh, "quickd", "sites", "get",
                        (char *)plan->site, "--json", NULL};
  quick_process_result_t res = {0};
  app_error err = quick_process_capture(argv, NULL, &res);
  if (err == APP_SUCCESS && res.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    err = quick_remote_site_info_set(info, res.out, plan->site);
  }
  quick_process_result_destroy(&res);
  return err;
}

void quick_delete_result_init(quick_delete_result_t *result) {
  if (result) {
    *result = (quick_delete_result_t){0};
    quick_remote_site_info_init(&result->site);
  }
}

void quick_delete_result_destroy(quick_delete_result_t *result) {
  if (!result) {
    return;
  }
  quick_remote_site_info_destroy(&result->site);
  free(result->profile);
  free(result->ssh);
  free(result->delete_json);
  free(result->archive);
  *result = (quick_delete_result_t){0};
}

app_error quick_op_delete(const quick_delete_request_t *request,
                          quick_delete_result_t *out) {
  if (!request || !out || !request->site || request->site[0] == '\0') {
    return APP_ERROR_INVALID_ARG;
  }
  quick_delete_result_destroy(out);
  quick_delete_result_init(out);
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_ops_resolve_remote_plan(request->profiles,
                                                request->profile,
                                                request->site, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  out->profile = quick_ops_strdup(plan.profile);
  out->ssh = quick_ops_strdup(plan.ssh);
  if (!out->profile || !out->ssh) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }
  err = quick_ops_fetch_site(&plan, &out->site);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  if (!request->assume_yes && !request->confirmed) {
    out->confirmation_required = true;
    quick_deploy_plan_destroy(&plan);
    return APP_SUCCESS;
  }
  char *const argv[] = {"ssh", plan.ssh, "quickd", "sites", "delete",
                        plan.site, "--json", NULL};
  quick_process_result_t res = {0};
  err = quick_process_capture(argv, NULL, &res);
  if (err == APP_SUCCESS && res.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    out->delete_json = quick_ops_strdup(res.out ? res.out : "");
    out->archive = quick_ops_json_get_string_field(res.out, "archive");
    out->deleted = true;
    if (!out->delete_json) {
      err = APP_ERROR_MEMORY;
    }
  }
  quick_process_result_destroy(&res);
  quick_deploy_plan_destroy(&plan);
  return err;
}

void quick_restore_result_init(quick_restore_result_t *result) {
  if (result) {
    *result = (quick_restore_result_t){0};
  }
}

void quick_restore_result_destroy(quick_restore_result_t *result) {
  if (!result) {
    return;
  }
  free(result->profile);
  free(result->ssh);
  free(result->remote_json);
  free(result->site);
  free(result->archive);
  free(result->release);
  free(result->url);
  *result = (quick_restore_result_t){0};
}

app_error quick_op_restore(const quick_restore_request_t *request,
                           quick_restore_result_t *out) {
  if (!request || !out || !request->site || request->site[0] == '\0' ||
      !request->archive || request->archive[0] == '\0') {
    return APP_ERROR_INVALID_ARG;
  }
  quick_restore_result_destroy(out);
  quick_restore_result_init(out);
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_ops_resolve_remote_plan(request->profiles,
                                                request->profile,
                                                request->site, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  out->profile = quick_ops_strdup(plan.profile);
  out->ssh = quick_ops_strdup(plan.ssh);
  out->site = quick_ops_strdup(plan.site);
  out->archive = quick_ops_strdup(request->archive);
  if (!out->profile || !out->ssh || !out->site || !out->archive) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }
  if (!request->assume_yes && !request->confirmed) {
    out->confirmation_required = true;
    quick_deploy_plan_destroy(&plan);
    return APP_SUCCESS;
  }
  char *const argv[] = {"ssh", plan.ssh, "quickd", "sites", "restore",
                        plan.site, "--from", (char *)request->archive,
                        "--json", NULL};
  quick_process_result_t res = {0};
  err = quick_process_capture(argv, NULL, &res);
  if (err == APP_SUCCESS && res.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    out->remote_json = quick_ops_strdup(res.out ? res.out : "");
    out->release = quick_ops_json_get_string_field(res.out, "release");
    out->url = quick_ops_json_get_string_field(res.out, "url");
    out->restored = true;
    if (!out->remote_json) {
      err = APP_ERROR_MEMORY;
    }
  }
  quick_process_result_destroy(&res);
  quick_deploy_plan_destroy(&plan);
  return err;
}

void quick_rollback_result_init(quick_rollback_result_t *result) {
  if (result) {
    *result = (quick_rollback_result_t){0};
    quick_remote_site_info_init(&result->site);
  }
}

void quick_rollback_result_destroy(quick_rollback_result_t *result) {
  if (!result) {
    return;
  }
  quick_remote_site_info_destroy(&result->site);
  free(result->profile);
  free(result->ssh);
  free(result->remote_json);
  free(result->release);
  free(result->previous_release);
  *result = (quick_rollback_result_t){0};
}

app_error quick_op_rollback(const quick_rollback_request_t *request,
                            quick_rollback_result_t *out) {
  if (!request || !out || !request->site || request->site[0] == '\0') {
    return APP_ERROR_INVALID_ARG;
  }
  quick_rollback_result_destroy(out);
  quick_rollback_result_init(out);
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_ops_resolve_remote_plan(request->profiles,
                                                request->profile,
                                                request->site, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  out->profile = quick_ops_strdup(plan.profile);
  out->ssh = quick_ops_strdup(plan.ssh);
  if (!out->profile || !out->ssh) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }
  err = quick_ops_fetch_site(&plan, &out->site);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  if (!request->assume_yes && !request->confirmed) {
    out->confirmation_required = true;
    quick_deploy_plan_destroy(&plan);
    return APP_SUCCESS;
  }
  char *deployer_name = quick_op_default_deployer_identity();
  if (!deployer_name) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }
  quick_process_result_t res = {0};
  if (request->release && request->release[0] != '\0') {
    char *const argv[] = {"ssh", plan.ssh, "quickd", "releases", "rollback",
                          "--site", plan.site, "--to", (char *)request->release,
                          "--deployer", deployer_name, "--json", NULL};
    err = quick_process_capture(argv, NULL, &res);
  } else {
    char *const argv[] = {"ssh", plan.ssh, "quickd", "releases", "rollback",
                          "--site", plan.site, "--deployer", deployer_name,
                          "--json", NULL};
    err = quick_process_capture(argv, NULL, &res);
  }
  free(deployer_name);
  if (err == APP_SUCCESS && res.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    out->remote_json = quick_ops_strdup(res.out ? res.out : "");
    out->release = quick_ops_json_get_string_field(res.out, "release");
    out->previous_release = quick_ops_json_get_string_field(res.out, "previous_release");
    out->rolled_back = true;
    if (!out->remote_json) {
      err = APP_ERROR_MEMORY;
    }
  }
  quick_process_result_destroy(&res);
  quick_deploy_plan_destroy(&plan);
  return err;
}

void quick_public_result_init(quick_public_result_t *result) {
  if (result) {
    *result = (quick_public_result_t){0};
    quick_remote_site_info_init(&result->site);
  }
}

void quick_public_result_destroy(quick_public_result_t *result) {
  if (!result) {
    return;
  }
  quick_remote_site_info_destroy(&result->site);
  free(result->profile);
  free(result->ssh);
  free(result->remote_json);
  *result = (quick_public_result_t){0};
}

app_error quick_op_public(const quick_public_request_t *request,
                          quick_public_result_t *out) {
  if (!request || !out || !request->site || request->site[0] == '\0') {
    return APP_ERROR_INVALID_ARG;
  }
  quick_public_result_destroy(out);
  quick_public_result_init(out);
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_ops_resolve_remote_plan(request->profiles,
                                                request->profile,
                                                request->site, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  out->profile = quick_ops_strdup(plan.profile);
  out->ssh = quick_ops_strdup(plan.ssh);
  if (!out->profile || !out->ssh) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }
  err = quick_ops_fetch_site(&plan, &out->site);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  out->is_public = out->site.is_public;
  out->have_public = out->site.have_public;
  if (request->action == QUICK_PUBLIC_STATUS) {
    quick_deploy_plan_destroy(&plan);
    return APP_SUCCESS;
  }
  if (request->action == QUICK_PUBLIC_ON && !request->assume_yes &&
      !request->confirmed) {
    out->confirmation_required = true;
    quick_deploy_plan_destroy(&plan);
    return APP_SUCCESS;
  }
  char *const argv[] = {"ssh", plan.ssh, "quickd", "sites", "public",
                        plan.site,
                        request->action == QUICK_PUBLIC_ON ? "--on" : "--off",
                        "--json", NULL};
  quick_process_result_t res = {0};
  err = quick_process_capture(argv, NULL, &res);
  if (err == APP_SUCCESS && res.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    out->remote_json = quick_ops_strdup(res.out ? res.out : "");
    bool value = request->action == QUICK_PUBLIC_ON;
    (void)quick_ops_json_get_bool_field(res.out, "public", &value);
    out->is_public = value;
    out->have_public = true;
    out->changed = true;
    if (!out->remote_json) {
      err = APP_ERROR_MEMORY;
    }
  }
  quick_process_result_destroy(&res);
  quick_deploy_plan_destroy(&plan);
  return err;
}

void quick_domain_result_init(quick_domain_result_t *result) {
  if (result) {
    *result = (quick_domain_result_t){0};
  }
}

void quick_domain_result_destroy(quick_domain_result_t *result) {
  if (!result) {
    return;
  }
  free(result->profile);
  free(result->ssh);
  free(result->site);
  free(result->domain);
  free(result->remote_json);
  *result = (quick_domain_result_t){0};
}

app_error quick_op_domain(const quick_domain_request_t *request,
                          quick_domain_result_t *out) {
  if (!request || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  if ((request->action == QUICK_DOMAIN_ADD ||
       request->action == QUICK_DOMAIN_REMOVE) &&
      (!request->domain || !quick_domain_is_safe(request->domain))) {
    return APP_ERROR_VALIDATION;
  }
  if (request->action == QUICK_DOMAIN_ADD &&
      (!request->site || request->site[0] == '\0')) {
    return APP_ERROR_MISSING_ARG;
  }
  quick_domain_result_destroy(out);
  quick_domain_result_init(out);
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_ops_resolve_remote_plan(
      request->profiles, request->profile,
      request->action == QUICK_DOMAIN_ADD ? request->site : NULL, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  out->profile = quick_ops_strdup(plan.profile);
  out->ssh = quick_ops_strdup(plan.ssh);
  out->site = quick_ops_strdup(plan.site);
  out->domain = request->domain ? quick_ops_strdup(request->domain) : NULL;
  if (!out->profile || !out->ssh || !out->site ||
      (request->domain && !out->domain)) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }

  quick_process_result_t res = {0};
  if (request->action == QUICK_DOMAIN_ADD) {
    char *const argv[] = {"ssh", plan.ssh, "quickd", "domains", "add",
                          (char *)request->domain, "--site", plan.site,
                          "--json", NULL};
    err = quick_process_capture(argv, NULL, &res);
  } else if (request->action == QUICK_DOMAIN_REMOVE) {
    char *const argv[] = {"ssh", plan.ssh, "quickd", "domains", "remove",
                          (char *)request->domain, "--json", NULL};
    err = quick_process_capture(argv, NULL, &res);
  } else {
    char *const argv[] = {"ssh", plan.ssh, "quickd", "domains", "list",
                          "--json", NULL};
    err = quick_process_capture(argv, NULL, &res);
  }
  if (err == APP_SUCCESS && res.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    out->remote_json = quick_ops_strdup(res.out ? res.out : "");
    if (!out->remote_json) {
      err = APP_ERROR_MEMORY;
    }
  }
  quick_process_result_destroy(&res);
  quick_deploy_plan_destroy(&plan);
  return err;
}

void quick_host_stats_result_init(quick_host_stats_result_t *result) {
  if (result) {
    *result = (quick_host_stats_result_t){0};
  }
}

void quick_host_stats_result_destroy(quick_host_stats_result_t *result) {
  if (!result) {
    return;
  }
  free(result->profile);
  free(result->ssh);
  free(result->raw_json);
  *result = (quick_host_stats_result_t){0};
}

app_error quick_op_host_stats(const quick_host_stats_request_t *request,
                              quick_host_stats_result_t *out) {
  if (!request || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_host_stats_result_destroy(out);
  quick_host_stats_result_init(out);
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_ops_resolve_remote_plan(request->profiles,
                                                request->profile, NULL, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  out->profile = quick_ops_strdup(plan.profile);
  out->ssh = quick_ops_strdup(plan.ssh);
  if (!out->profile || !out->ssh) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }
  char *const argv[] = {"ssh", plan.ssh, "quickd", "admin", "stats",
                        "--json", NULL};
  quick_process_result_t res = {0};
  err = quick_process_capture(argv, NULL, &res);
  if (err == APP_SUCCESS && res.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    out->raw_json = quick_ops_strdup(res.out ? res.out : "");
    if (!out->raw_json) {
      err = APP_ERROR_MEMORY;
    } else {
      out->sites = quick_ops_json_get_long_field(res.out, "sites", -1);
      out->releases = quick_ops_json_get_long_field(res.out, "releases", -1);
      out->sites_bytes = quick_ops_json_get_long_field(res.out, "sites_bytes", -1);
      out->uploads_bytes = quick_ops_json_get_long_field(res.out, "uploads_bytes", -1);
      out->db_bytes = quick_ops_json_get_long_field(res.out, "db_bytes", -1);
      out->parsed = true;
    }
  }
  quick_process_result_destroy(&res);
  quick_deploy_plan_destroy(&plan);
  return err;
}

static quick_doctor_status_t quick_doctor_status_from_string(const char *s) {
  if (s && strcmp(s, "ok") == 0) {
    return QUICK_DOCTOR_STATUS_OK;
  }
  if (s && strcmp(s, "fail") == 0) {
    return QUICK_DOCTOR_STATUS_FAIL;
  }
  if (s && strcmp(s, "skip") == 0) {
    return QUICK_DOCTOR_STATUS_SKIP;
  }
  return QUICK_DOCTOR_STATUS_WARN;
}

const char *quick_doctor_status_string(quick_doctor_status_t status) {
  switch (status) {
  case QUICK_DOCTOR_STATUS_OK:
    return "ok";
  case QUICK_DOCTOR_STATUS_WARN:
    return "warn";
  case QUICK_DOCTOR_STATUS_FAIL:
    return "fail";
  case QUICK_DOCTOR_STATUS_SKIP:
    return "skip";
  }
  return "warn";
}

void quick_doctor_result_init(quick_doctor_result_t *result) {
  if (result) {
    *result = (quick_doctor_result_t){.ok = true};
  }
}

void quick_doctor_result_destroy(quick_doctor_result_t *result) {
  if (!result) {
    return;
  }
  for (size_t i = 0; i < result->count; i++) {
    free(result->checks[i].name);
    free(result->checks[i].group);
    free(result->checks[i].detail);
    free(result->checks[i].remediation);
  }
  free(result->checks);
  *result = (quick_doctor_result_t){0};
}

static app_error quick_doctor_add_check(quick_doctor_result_t *result,
                                        const char *name, const char *group,
                                        const char *status,
                                        const char *detail,
                                        const char *remediation) {
  quick_doctor_check_t *grown = realloc(
      result->checks, (result->count + 1U) * sizeof(quick_doctor_check_t));
  if (!grown) {
    return APP_ERROR_MEMORY;
  }
  result->checks = grown;
  quick_doctor_check_t *check = &result->checks[result->count];
  *check = (quick_doctor_check_t){0};
  check->name = quick_ops_strdup(name ? name : "");
  check->group = quick_ops_strdup(group ? group : "");
  check->status = quick_doctor_status_from_string(status);
  check->detail = quick_ops_strdup(detail ? detail : "");
  check->remediation = quick_ops_strdup(remediation ? remediation : "");
  if (!check->name || !check->group || !check->detail || !check->remediation) {
    free(check->name);
    free(check->group);
    free(check->detail);
    free(check->remediation);
    *check = (quick_doctor_check_t){0};
    return APP_ERROR_MEMORY;
  }
  if (check->status == QUICK_DOCTOR_STATUS_FAIL) {
    result->ok = false;
  }
  result->count++;
  return APP_SUCCESS;
}

static char *quick_doctor_url_join(const char *base, const char *suffix) {
  if (!base || !suffix) {
    return NULL;
  }
  const size_t blen = strlen(base);
  const bool slash = blen > 0 && base[blen - 1] == '/';
  const char *s = suffix;
  while (*s == '/') {
    s++;
  }
  char *out = malloc(blen + (slash ? 0U : 1U) + strlen(s) + 1U);
  if (!out) {
    return NULL;
  }
  sprintf(out, "%s%s%s", base, slash ? "" : "/", s);
  return out;
}

static bool quick_doctor_identity_shape_ok(const char *json) {
  if (!json || !strstr(json, "\"authenticated\"")) {
    return false;
  }
  char *provider = quick_ops_json_get_string_field(json, "provider");
  char *subject = quick_ops_json_get_string_field(json, "subject");
  bool ok = provider && provider[0] != '\0' && subject && subject[0] != '\0';
  free(provider);
  free(subject);
  return ok;
}

static bool quick_doctor_identity_looks_like_login_gate(const char *body) {
  if (!body || body[0] == '\0') {
    return false;
  }
  return strstr(body, "__exe.dev/login") || strstr(body, "Temporary Redirect") ||
         strstr(body, "Cloudflare Access") || strstr(body, "Sign in");
}

static app_error quick_doctor_curl_get(const char *url,
                                       quick_process_result_t *res) {
  char *const argv[] = {"curl", "-fsS", "--max-time", "5", (char *)url,
                        NULL};
  return quick_process_capture(argv, NULL, res);
}

static app_error quick_doctor_probe_http(quick_doctor_result_t *result,
                                         const char *curl,
                                         const quick_deploy_plan_t *plan) {
  if (!curl) {
    return quick_doctor_add_check(result, "http_probe", "edge/iap", "warn",
                                  "curl not found; HTTP probes skipped",
                                  "install curl");
  }
  if (!plan || !plan->url || plan->url[0] == '\0') {
    return quick_doctor_add_check(
        result, "http_probe", "edge/iap", "skip", "no public URL resolved",
        "Configure base_domain or base_url in the selected profile.");
  }
  char *health_url = quick_doctor_url_join(plan->url, "/_quick/health");
  char *identity_url = quick_doctor_url_join(plan->url, "/_quick/identity");
  if (!health_url || !identity_url) {
    free(health_url);
    free(identity_url);
    return quick_doctor_add_check(result, "http_probe", "edge/iap", "fail",
                                  "failed to build probe URLs",
                                  "Check configured base URL/domain.");
  }

  quick_process_result_t health = {0};
  app_error err = quick_doctor_curl_get(health_url, &health);
  app_error add_err = quick_doctor_add_check(
      result, "http_health", "edge/iap",
      err == APP_SUCCESS && health.exit_code == 0 ? "ok" : "fail", health_url,
      "Ensure the edge routes /_quick/health to quickd.");
  quick_process_result_destroy(&health);
  if (add_err != APP_SUCCESS) {
    free(health_url);
    free(identity_url);
    return add_err;
  }

  quick_process_result_t identity = {0};
  err = quick_doctor_curl_get(identity_url, &identity);
  const bool identity_http_ok = err == APP_SUCCESS && identity.exit_code == 0;
  const bool identity_ok = identity_http_ok &&
                           quick_doctor_identity_shape_ok(identity.out);
  const bool identity_login_gate = identity_http_ok && !identity_ok &&
                                   quick_doctor_identity_looks_like_login_gate(identity.out);
  char identity_detail[640];
  snprintf(identity_detail, sizeof(identity_detail), "%s%s", identity_url,
           identity_ok ? "" : identity_login_gate
                             ? " (unauthenticated curl reached a login/redirect page)"
                             : " (identity JSON missing authenticated/provider/subject)");
  add_err = quick_doctor_add_check(
      result, "http_identity", "edge/iap",
      identity_ok ? "ok" : identity_login_gate ? "warn" : "fail",
      identity_detail,
      identity_login_gate
          ? "Run doctor from an authenticated edge session/network, or make the site public only if intended."
          : "Ensure /_quick/identity returns authenticated, provider, and subject.");
  quick_process_result_destroy(&identity);
  free(health_url);
  free(identity_url);
  return add_err;
}

static void quick_doctor_random_hex6(char out[7]) {
  unsigned char b[3] = {0};
#ifndef _WIN32
  FILE *f = fopen("/dev/urandom", "rb");
  if (f) {
    (void)fread(b, 1, sizeof(b), f);
    fclose(f);
  } else
#endif
  {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    unsigned long seed = (unsigned long)ts.tv_nsec ^ (unsigned long)ts.tv_sec;
    b[0] = (unsigned char)(seed & 0xffU);
    b[1] = (unsigned char)((seed >> 8) & 0xffU);
    b[2] = (unsigned char)((seed >> 16) & 0xffU);
  }
  static const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < 3; i++) {
    out[i * 2U] = hex[(b[i] >> 4) & 0xfU];
    out[i * 2U + 1U] = hex[b[i] & 0xfU];
  }
  out[6] = '\0';
}

static char *quick_doctor_url_for_site(const quick_deploy_plan_t *plan,
                                       const char *site) {
  if (plan->base_domain && plan->base_domain[0] != '\0') {
    size_t len = strlen("https://") + strlen(site) + 1U +
                 strlen(plan->base_domain) + 1U;
    char *url = malloc(len);
    if (url) {
      snprintf(url, len, "https://%s.%s", site, plan->base_domain);
    }
    return url;
  }
  const char *base = plan->base_url && plan->base_url[0] != '\0'
                         ? plan->base_url
                         : "http://localhost:9366/~";
  size_t blen = strlen(base);
  bool slash = blen > 0 && base[blen - 1] == '/';
  size_t len = blen + (slash ? 0U : 1U) + strlen(site) + 2U;
  char *url = malloc(len);
  if (url) {
    snprintf(url, len, "%s%s%s/", base, slash ? "" : "/", site);
  }
  return url;
}

static void quick_doctor_cleanup_temp_dir(const char *dir) {
#ifndef _WIN32
  if (!dir) {
    return;
  }
  char index_path[512];
  snprintf(index_path, sizeof(index_path), "%s/index.html", dir);
  (void)unlink(index_path);
  (void)rmdir(dir);
#else
  (void)dir;
#endif
}

static app_error quick_doctor_write_file(const char *path, const char *body) {
  FILE *f = fopen(path, "wb");
  if (!f) {
    return APP_ERROR_IO;
  }
  const size_t len = strlen(body);
  bool ok = fwrite(body, 1, len, f) == len;
  if (fclose(f) != 0 || !ok) {
    return APP_ERROR_IO;
  }
  return APP_SUCCESS;
}

static app_error quick_doctor_write_temp_site(char *dir_template,
                                              size_t dir_len) {
#ifndef _WIN32
  (void)dir_len;
  if (!mkdtemp(dir_template)) {
    return APP_ERROR_IO;
  }
  char index_path[512];
  snprintf(index_path, sizeof(index_path), "%s/index.html", dir_template);
  const char *body = "<!doctype html><title>OpenQuick doctor</title>ok\n";
  app_error err = quick_doctor_write_file(index_path, body);
  if (err != APP_SUCCESS) {
    quick_doctor_cleanup_temp_dir(dir_template);
    return err;
  }
  return APP_SUCCESS;
#else
  (void)dir_template;
  (void)dir_len;
  return APP_ERROR_FEATURE_BASE;
#endif
}

static void quick_doctor_remove_local_site(const char *remote_root,
                                           const char *site,
                                           const char *deploy_id) {
#ifndef _WIN32
  char *site_dir = quick_ops_path_join(remote_root, "sites");
  char *one_site = site_dir ? quick_ops_path_join(site_dir, site) : NULL;
  char *releases = one_site ? quick_ops_path_join(one_site, "releases") : NULL;
  char *release = releases ? quick_ops_path_join(releases, deploy_id) : NULL;
  char *index = release ? quick_ops_path_join(release, "index.html") : NULL;
  char *current = one_site ? quick_ops_path_join(one_site, "current") : NULL;
  char *site_json = one_site ? quick_ops_path_join(one_site, "site.json") : NULL;
  if (index) {
    (void)unlink(index);
  }
  if (release) {
    (void)rmdir(release);
  }
  if (releases) {
    (void)rmdir(releases);
  }
  if (current) {
    (void)unlink(current);
  }
  if (site_json) {
    (void)unlink(site_json);
  }
  if (one_site) {
    (void)rmdir(one_site);
  }
  free(site_dir);
  free(one_site);
  free(releases);
  free(release);
  free(index);
  free(current);
  free(site_json);
#else
  (void)remote_root;
  (void)site;
  (void)deploy_id;
#endif
}

static bool quick_doctor_probe_public_and_identity(const char *url) {
  if (!url) {
    return false;
  }
  quick_process_result_t public_res = {0};
  app_error err = quick_doctor_curl_get(url, &public_res);
  bool public_ok = err == APP_SUCCESS && public_res.exit_code == 0;
  quick_process_result_destroy(&public_res);
  char *identity_url = quick_doctor_url_join(url, "/_quick/identity");
  quick_process_result_t identity = {0};
  err = identity_url ? quick_doctor_curl_get(identity_url, &identity)
                     : APP_ERROR_MEMORY;
  bool identity_ok = err == APP_SUCCESS && identity.exit_code == 0 &&
                     quick_doctor_identity_shape_ok(identity.out);
  quick_process_result_destroy(&identity);
  free(identity_url);
  return public_ok && identity_ok;
}

static bool quick_doctor_try_local_deep(const quick_deploy_plan_t *plan,
                                        const char *site, char *deep_detail,
                                        size_t deep_detail_len,
                                        bool *ok_out) {
#ifndef _WIN32
  if (!plan || strcmp(plan->profile, "local") != 0 || !plan->remote_root ||
      !quick_ops_dir_exists(plan->remote_root)) {
    return false;
  }
  char deploy_id[64];
  char hex[7];
  quick_doctor_random_hex6(hex);
  snprintf(deploy_id, sizeof(deploy_id), "20260612T000000Z-%s", hex);
  char *sites_dir = quick_ops_path_join(plan->remote_root, "sites");
  char *site_dir = sites_dir ? quick_ops_path_join(sites_dir, site) : NULL;
  char *releases = site_dir ? quick_ops_path_join(site_dir, "releases") : NULL;
  char *release = releases ? quick_ops_path_join(releases, deploy_id) : NULL;
  char *index = release ? quick_ops_path_join(release, "index.html") : NULL;
  char *site_json = site_dir ? quick_ops_path_join(site_dir, "site.json") : NULL;
  char *current = site_dir ? quick_ops_path_join(site_dir, "current") : NULL;
  bool attempted = false;
  bool probe_ok = false;
  if (index && site_json && current &&
      quick_ops_mkdir_p(release, 0770) == APP_SUCCESS &&
      quick_doctor_write_file(index,
                              "<!doctype html><title>OpenQuick doctor</title>ok\n") ==
          APP_SUCCESS &&
      quick_doctor_write_file(site_json,
                              "{\"name\":\"_doctor\",\"subdomain\":\"_doctor\"}\n") ==
          APP_SUCCESS) {
    char target[128];
    snprintf(target, sizeof(target), "releases/%s", deploy_id);
    (void)unlink(current);
    if (symlink(target, current) == 0) {
      attempted = true;
      char *url = quick_doctor_url_for_site(plan, site);
      probe_ok = quick_doctor_probe_public_and_identity(url);
      snprintf(deep_detail, deep_detail_len, "%s local %s", site,
               probe_ok ? "deployed and probed" : "probe failed");
      free(url);
    }
  }
  quick_doctor_remove_local_site(plan->remote_root, site, deploy_id);
  free(sites_dir);
  free(site_dir);
  free(releases);
  free(release);
  free(index);
  free(site_json);
  free(current);
  if (ok_out) {
    *ok_out = probe_ok;
  }
  return attempted;
#else
  (void)plan;
  (void)site;
  (void)deep_detail;
  (void)deep_detail_len;
  (void)ok_out;
  return false;
#endif
}

static app_error quick_doctor_run_deep(quick_doctor_result_t *result,
                                       const char *curl,
                                       const quick_deploy_plan_t *plan) {
  char deep_detail[512];
  deep_detail[0] = '\0';
  char local_hex[7];
  quick_doctor_random_hex6(local_hex);
  char local_site[32];
  snprintf(local_site, sizeof(local_site), "_doctor-%s", local_hex);
  if (!plan || !plan->ssh || plan->ssh[0] == '\0') {
    bool local_ok = false;
    if (curl && quick_doctor_try_local_deep(plan, local_site, deep_detail,
                                            sizeof(deep_detail), &local_ok)) {
      return quick_doctor_add_check(
          result, "deep_temp_deploy", "edge/iap", local_ok ? "ok" : "fail",
          deep_detail,
          "Start local quickd and ensure /_quick/identity is reachable.");
    }
    return quick_doctor_add_check(
        result, "deep_temp_deploy", "edge/iap", "skip",
        "no SSH host resolved for deep check",
        "Configure profiles.<name>.ssh or run `quick serve install --profile <profile> --host <ssh> ...`.");
  }
  if (!curl) {
    return quick_doctor_add_check(result, "deep_temp_deploy", "edge/iap",
                                  "warn",
                                  "curl not found; deep HTTP probe skipped",
                                  "install curl");
  }
  char hex[7];
  quick_doctor_random_hex6(hex);
  char site[32];
  snprintf(site, sizeof(site), "_doctor-%s", hex);
  char tmpdir[128];
  snprintf(tmpdir, sizeof(tmpdir), "/tmp/openquick-doctor-%s-XXXXXX", hex);
  app_error err = quick_doctor_write_temp_site(tmpdir, sizeof(tmpdir));
  if (err != APP_SUCCESS) {
    return quick_doctor_add_check(
        result, "deep_temp_deploy", "edge/iap", "fail",
        "failed to create temporary diagnostic site",
        "Check local temporary directory permissions.");
  }

  char *const prepare_argv[] = {"ssh", (char *)plan->ssh, "quickd", "deploy",
                                "prepare", "--site", site, "--json", NULL};
  quick_process_result_t prepare = {0};
  err = quick_process_capture(prepare_argv, NULL, &prepare);
  char *deploy_id = NULL;
  char *staging = NULL;
  if (err == APP_SUCCESS && prepare.exit_code == 0) {
    deploy_id = quick_ops_json_get_string_field(prepare.out, "deploy_id");
    staging = quick_ops_json_get_string_field(prepare.out, "staging_path");
  }
  quick_process_result_destroy(&prepare);
  if (!deploy_id || !staging) {
    snprintf(deep_detail, sizeof(deep_detail), "%s prepare failed", site);
    app_error add_err = quick_doctor_add_check(
        result, "deep_temp_deploy", "edge/iap", "fail", deep_detail,
        "Run `quickd deploy prepare` on the host and inspect quickd logs.");
    free(deploy_id);
    free(staging);
    quick_doctor_cleanup_temp_dir(tmpdir);
    return add_err;
  }

  char *source = quick_doctor_url_join(tmpdir, "/");
  char *dest = malloc(strlen(plan->ssh) + strlen(staging) + 3U);
  if (!source || !dest) {
    err = APP_ERROR_MEMORY;
  } else {
    sprintf(dest, "%s:%s/", plan->ssh, staging);
    char *const rsync_argv[] = {"rsync", "-az", "--delete", source, dest,
                                NULL};
    quick_process_result_t rsync = {0};
    err = quick_process_capture(rsync_argv, NULL, &rsync);
    if (err == APP_SUCCESS && rsync.exit_code != 0) {
      err = APP_ERROR_IO;
    }
    quick_process_result_destroy(&rsync);
  }
  free(source);
  free(dest);
  if (err != APP_SUCCESS) {
    snprintf(deep_detail, sizeof(deep_detail), "%s transfer failed", site);
    app_error add_err = quick_doctor_add_check(
        result, "deep_temp_deploy", "edge/iap", "fail", deep_detail,
        "Install rsync and verify SSH write access to the staging path.");
    free(deploy_id);
    free(staging);
    quick_doctor_cleanup_temp_dir(tmpdir);
    return add_err;
  }

  char *const activate_argv[] = {"ssh", (char *)plan->ssh, "quickd", "deploy",
                                 "activate", "--site", site, "--deploy-id",
                                 deploy_id, "--json", NULL};
  quick_process_result_t activate = {0};
  err = quick_process_capture(activate_argv, NULL, &activate);
  char *url = NULL;
  if (err == APP_SUCCESS && activate.exit_code == 0) {
    url = quick_ops_json_get_string_field(activate.out, "url");
  }
  quick_process_result_destroy(&activate);
  if (!url) {
    url = quick_doctor_url_for_site(plan, site);
  }

  bool ok = false;
  if (url) {
    quick_process_result_t public_res = {0};
    err = quick_doctor_curl_get(url, &public_res);
    bool public_ok = err == APP_SUCCESS && public_res.exit_code == 0;
    quick_process_result_destroy(&public_res);
    char *identity_url = quick_doctor_url_join(url, "/_quick/identity");
    quick_process_result_t identity = {0};
    err = identity_url ? quick_doctor_curl_get(identity_url, &identity)
                       : APP_ERROR_MEMORY;
    bool identity_ok = err == APP_SUCCESS && identity.exit_code == 0 &&
                       quick_doctor_identity_shape_ok(identity.out);
    quick_process_result_destroy(&identity);
    free(identity_url);
    ok = public_ok && identity_ok;
  }

  char *const delete_argv[] = {"ssh", (char *)plan->ssh, "quickd", "sites",
                               "delete", site, "--json", NULL};
  quick_process_result_t del = {0};
  (void)quick_process_capture(delete_argv, NULL, &del);
  const bool deleted = del.exit_code == 0;
  quick_process_result_destroy(&del);

  snprintf(deep_detail, sizeof(deep_detail), "%s %s%s", site,
           ok ? "deployed and probed" : "probe failed",
           deleted ? "" : "; cleanup failed");
  app_error add_err = quick_doctor_add_check(
      result, "deep_temp_deploy", "edge/iap", ok && deleted ? "ok" : "fail",
      deep_detail,
      "Verify prepare/rsync/activate, public URL routing, identity, and `quickd sites delete`.");

  free(url);
  free(deploy_id);
  free(staging);
  quick_doctor_cleanup_temp_dir(tmpdir);
  return add_err;
}

app_error quick_op_doctor(const quick_doctor_request_t *request,
                          quick_doctor_result_t *out) {
  if (!request || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_doctor_result_destroy(out);
  quick_doctor_result_init(out);
  const app_build_info_t *build = app_build_info();
  app_error err = quick_doctor_add_check(
      out, "quick_version", "local", "ok", build->version,
      "Install the latest quick binary if versions drift.");
  if (err != APP_SUCCESS) {
    return err;
  }

  char *rsync = quick_ops_find_executable("rsync");
  err = quick_doctor_add_check(out, "rsync_present", "local",
                               rsync ? "ok" : "fail",
                               rsync ? rsync : "rsync not found on PATH",
                               "Install rsync and ensure it is on PATH.");
  if (err != APP_SUCCESS) {
    free(rsync);
    return err;
  }

  char *ssh = quick_ops_find_executable("ssh");
  err = quick_doctor_add_check(out, "ssh_present", "local",
                               ssh ? "ok" : "fail",
                               ssh ? ssh : "ssh not found on PATH",
                               "Install OpenSSH client and ensure it is on PATH.");
  if (err != APP_SUCCESS) {
    free(rsync);
    free(ssh);
    return err;
  }

  quick_plan_overrides_t overrides = {
      .site = request->site,
      .profile = request->profile,
  };
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error plan_err = quick_deploy_plan_resolve(&overrides, request->profiles,
                                                 &plan);
  err = quick_doctor_add_check(
      out, "quick_json", "local", plan_err == APP_SUCCESS ? "ok" : "warn",
      plan.quick_json_path ? plan.quick_json_path : "no quick.json in current tree",
      "Run `quick init` in a site directory.");
  if (err == APP_SUCCESS) {
    err = quick_doctor_add_check(
        out, "site_dns_label", "local",
        plan_err == APP_SUCCESS && quick_slug_is_valid(plan.site) ? "ok" :
                                                                    "fail",
        plan_err == APP_SUCCESS ? plan.site : "unresolved",
        "Use lowercase a-z, digits, and hyphens; no edge hyphens.");
  }
  if (err == APP_SUCCESS) {
    err = quick_doctor_add_check(
        out, "build_output", "local",
        plan_err == APP_SUCCESS && quick_ops_dir_exists(plan.output_dir) ? "ok" :
                                                                           "warn",
        plan_err == APP_SUCCESS ? plan.output_dir : "unresolved",
        "Run the configured build or update quick.json output.");
  }
  quick_ignore_t ignore;
  quick_ignore_init(&ignore);
  app_error ignore_err = plan_err == APP_SUCCESS
                             ? quick_ignore_load_for_site(plan.site_root, &ignore)
                             : APP_ERROR_NOT_FOUND;
  if (err == APP_SUCCESS) {
    err = quick_doctor_add_check(
        out, "quickignore", "local", ignore_err == APP_SUCCESS ? "ok" : "warn",
        ignore_err == APP_SUCCESS ? "parsed" : "missing or invalid .quickignore",
        "Create .quickignore with deploy excludes.");
  }
  quick_ignore_destroy(&ignore);

  const bool remote = request->remote || request->profile != NULL;
  if (err == APP_SUCCESS && remote) {
    if (plan_err == APP_SUCCESS && plan.ssh && plan.ssh[0] != '\0') {
      char *const doctor_argv[] = {"ssh", plan.ssh, "quickd", "doctor",
                                   "--host", "--json", NULL};
      quick_process_result_t res = {0};
      app_error proc_err = quick_process_capture(doctor_argv, NULL, &res);
      err = quick_doctor_add_check(
          out, "quickd_doctor", "remote",
          proc_err == APP_SUCCESS && res.exit_code == 0 ? "ok" : "fail",
          proc_err == APP_SUCCESS && res.out && res.out[0]
              ? "quickd doctor responded"
              : "quickd doctor failed",
          "Run `quick serve install` or inspect quickd on the host.");
      quick_process_result_destroy(&res);
      if (err == APP_SUCCESS) {
        char *const stats_argv[] = {"ssh", plan.ssh, "quickd", "admin",
                                    "stats", "--json", NULL};
        quick_process_result_t stats = {0};
        proc_err = quick_process_capture(stats_argv, NULL, &stats);
        char detail[160];
        if (proc_err == APP_SUCCESS && stats.exit_code == 0 && stats.out &&
            stats.out[0]) {
          long sites = quick_ops_json_get_long_field(stats.out, "sites", -1);
          long releases = quick_ops_json_get_long_field(stats.out, "releases", -1);
          snprintf(detail, sizeof(detail), "sites=%ld releases=%ld", sites,
                   releases);
        } else {
          snprintf(detail, sizeof(detail), "quickd admin stats failed");
        }
        err = quick_doctor_add_check(
            out, "host_stats", "remote",
            proc_err == APP_SUCCESS && stats.exit_code == 0 ? "ok" : "fail",
            detail, "Run `quickd admin stats --json` on the host.");
        quick_process_result_destroy(&stats);
      }
    } else {
      err = quick_doctor_add_check(
          out, "ssh_profile", "remote", "warn", "no SSH host resolved",
          "Configure profiles.<name>.ssh or QUICK_REMOTE.");
    }
  }

  char *curl = quick_ops_find_executable("curl");
  if (err == APP_SUCCESS) {
    err = quick_doctor_probe_http(out, curl,
                                  plan_err == APP_SUCCESS ? &plan : NULL);
  }
  if (err == APP_SUCCESS && request->deep) {
    err = quick_doctor_run_deep(out, curl,
                                plan_err == APP_SUCCESS ? &plan : NULL);
  }

  free(rsync);
  free(ssh);
  free(curl);
  quick_deploy_plan_destroy(&plan);
  return err;
}

void quick_url_result_init(quick_url_result_t *result) {
  if (result) {
    *result = (quick_url_result_t){0};
  }
}

void quick_url_result_destroy(quick_url_result_t *result) {
  if (!result) {
    return;
  }
  free(result->url);
  *result = (quick_url_result_t){0};
}

const char *quick_url_source_string(quick_url_source_t source) {
  switch (source) {
  case QUICK_URL_SOURCE_LOCAL:
    return "local";
  case QUICK_URL_SOURCE_REMOTE:
    return "remote";
  case QUICK_URL_SOURCE_DETERMINISTIC:
    return "deterministic";
  }
  return "deterministic";
}

static app_error quick_open_resolve_remote_url(const quick_deploy_plan_t *plan,
                                               char **url_out) {
  *url_out = NULL;
  if (!plan->ssh || plan->ssh[0] == '\0') {
    return APP_ERROR_NOT_FOUND;
  }
  char *const argv[] = {"ssh", (char *)plan->ssh, "quickd", "sites", "get",
                        (char *)plan->site, "--json", NULL};
  quick_process_result_t res = {0};
  app_error err = quick_process_capture(argv, NULL, &res);
  if (err != APP_SUCCESS) {
    return err;
  }
  if (res.exit_code == 0) {
    *url_out = quick_ops_json_get_string_field(res.out, "url");
  }
  quick_process_result_destroy(&res);
  return *url_out ? APP_SUCCESS : APP_ERROR_NOT_FOUND;
}

app_error quick_op_resolve_url(const quick_profile_config_t *profiles,
                               const quick_plan_overrides_t *overrides,
                               quick_url_result_t *out) {
  if (!out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_url_result_destroy(out);
  quick_url_result_init(out);
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_deploy_plan_resolve(overrides, profiles, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }

  quick_deployment_record_t record;
  quick_deployment_record_init(&record);
  if (quick_local_state_read_deployment(plan.site_root, plan.profile,
                                        &record) == APP_SUCCESS &&
      record.url && record.url[0] != '\0') {
    out->url = quick_ops_strdup(record.url);
    out->live = true;
    out->source = QUICK_URL_SOURCE_LOCAL;
  }
  if (!out->url) {
    char *remote_url = NULL;
    (void)quick_open_resolve_remote_url(&plan, &remote_url);
    if (remote_url) {
      out->url = remote_url;
      out->live = true;
      out->source = QUICK_URL_SOURCE_REMOTE;
    }
  }
  if (!out->url) {
    out->url = quick_ops_strdup(plan.url);
    out->live = false;
    out->source = QUICK_URL_SOURCE_DETERMINISTIC;
  }

  quick_deployment_record_destroy(&record);
  quick_deploy_plan_destroy(&plan);
  return out->url ? APP_SUCCESS : APP_ERROR_MEMORY;
}

app_error quick_op_open_url(const char *url) {
  if (!url || url[0] == '\0') {
    return APP_SUCCESS;
  }
#ifdef __APPLE__
  char *const argv[] = {"open", (char *)url, NULL};
#else
  char *const argv[] = {"xdg-open", (char *)url, NULL};
#endif
  int code = 0;
  return quick_process_run_inherit(argv, NULL, &code);
}

app_error quick_op_copy_url(const char *url, char **message_out) {
  if (message_out) {
    *message_out = NULL;
  }
#ifdef _WIN32
  (void)url;
  if (message_out) {
    *message_out = quick_ops_strdup("clipboard copy is not supported on this platform");
  }
  return APP_ERROR_FEATURE_BASE;
#else
  if (!url) {
    return APP_ERROR_INVALID_ARG;
  }
  char *tool = NULL;
  char *const *argv = NULL;
#ifdef __APPLE__
  tool = quick_ops_find_executable("pbcopy");
  char *pbcopy_argv[] = {tool, NULL};
  argv = pbcopy_argv;
#else
  tool = quick_ops_find_executable("wl-copy");
  char *wl_argv[] = {tool, NULL};
  char *xclip_argv[] = {NULL, "-selection", "clipboard", NULL};
  if (tool) {
    argv = wl_argv;
  } else {
    char *xclip_tool = quick_ops_find_executable("xclip");
    if (xclip_tool) {
      tool = xclip_tool;
      xclip_argv[0] = tool;
      argv = xclip_argv;
    }
  }
#endif
  if (!tool || !argv) {
    if (message_out) {
      *message_out = quick_ops_strdup(
          "no clipboard tool found; install pbcopy, wl-copy, or xclip");
      if (!*message_out) {
        free(tool);
        return APP_ERROR_MEMORY;
      }
    }
    free(tool);
    return APP_ERROR_NOT_FOUND;
  }
  quick_process_result_t res = {0};
  app_error err = quick_process_capture_input((char *const *)argv, NULL, url,
                                              &res);
  if ((err != APP_SUCCESS || res.exit_code != 0) && message_out) {
    const char *msg = res.err && res.err[0] ? res.err : "clipboard copy failed";
    *message_out = quick_ops_strdup(msg);
    if (!*message_out) {
      quick_process_result_destroy(&res);
      free(tool);
      return APP_ERROR_MEMORY;
    }
  }
  quick_process_result_destroy(&res);
  free(tool);
  return err;
#endif
}

static char *quick_serve_find_quickd(void) {
  const char *override = getenv("QUICK_QUICKD");
  if (override && override[0] != '\0') {
    return quick_ops_path_exists(override) ? quick_ops_strdup(override) : NULL;
  }
  char *path = quick_ops_find_executable("quickd");
  if (path) {
    return path;
  }
  const char *candidates[] = {"zig-out/bin/quickd", "server/quickd",
                              "server/cmd/quickd/quickd"};
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    if (quick_ops_path_exists(candidates[i])) {
      return quick_ops_strdup(candidates[i]);
    }
  }
  return NULL;
}

void quick_dev_token_result_init(quick_dev_token_result_t *result) {
  if (result) {
    *result = (quick_dev_token_result_t){0};
  }
}

void quick_dev_token_result_destroy(quick_dev_token_result_t *result) {
  if (!result) {
    return;
  }
  free(result->profile);
  free(result->ssh);
  free(result->site);
  free(result->url);
  free(result->token);
  free(result->expires_at);
  free(result->raw_json);
  *result = (quick_dev_token_result_t){0};
}

app_error quick_op_mint_dev_token(const quick_dev_token_request_t *request,
                                  quick_dev_token_result_t *out) {
  if (!request || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_dev_token_result_destroy(out);
  quick_dev_token_result_init(out);

  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_ops_resolve_remote_plan(request->profiles,
                                                request->profile,
                                                request->site, &plan);
  if (err != APP_SUCCESS) {
    quick_deploy_plan_destroy(&plan);
    return err;
  }
  if (!plan.url || strncmp(plan.url, "https://", strlen("https://")) != 0) {
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_CONFIG_INVALID;
  }

  int ttl = request->ttl_seconds > 0 ? request->ttl_seconds : 3600;
  char ttl_buf[32];
  snprintf(ttl_buf, sizeof(ttl_buf), "%d", ttl);
  char *const argv[] = {"ssh",
                        plan.ssh,
                        "quickd",
                        "admin",
                        "mint-dev-token",
                        "--site",
                        plan.site,
                        "--ttl",
                        ttl_buf,
                        "--json",
                        NULL};
  quick_process_result_t res = {0};
  err = quick_process_capture(argv, NULL, &res);
  if (err == APP_SUCCESS && res.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    char *token = quick_ops_json_get_string_field(res.out, "token");
    char *site = quick_ops_json_get_string_field(res.out, "site");
    char *expires_at = quick_ops_json_get_string_field(res.out, "expires_at");
    if (!token || !site || !expires_at || strcmp(site, plan.site) != 0) {
      free(token);
      free(site);
      free(expires_at);
      err = APP_ERROR_INVALID_DATA;
    } else {
      out->profile = quick_ops_strdup(plan.profile);
      out->ssh = quick_ops_strdup(plan.ssh);
      out->site = site;
      out->url = quick_ops_strdup(plan.url);
      out->token = token;
      out->expires_at = expires_at;
      out->raw_json = quick_ops_strdup(res.out ? res.out : "");
      if (!out->profile || !out->ssh || !out->url || !out->raw_json) {
        err = APP_ERROR_MEMORY;
      }
    }
  }

  quick_process_result_destroy(&res);
  quick_deploy_plan_destroy(&plan);
  if (err != APP_SUCCESS) {
    quick_dev_token_result_destroy(out);
  }
  return err;
}

void quick_serve_dev_command_init(quick_serve_dev_command_t *command) {
  if (command) {
    *command = (quick_serve_dev_command_t){0};
  }
}

void quick_serve_dev_command_destroy(quick_serve_dev_command_t *command) {
  if (!command) {
    return;
  }
  free(command->quickd_path);
  if (command->argv) {
    for (size_t i = 0; i < command->argc; i++) {
      free(command->argv[i]);
    }
  }
  free(command->argv);
  *command = (quick_serve_dev_command_t){0};
}

static app_error quick_serve_argv_append(quick_serve_dev_command_t *command,
                                         const char *value) {
  char **grown = realloc(command->argv, (command->argc + 2U) * sizeof(char *));
  if (!grown) {
    return APP_ERROR_MEMORY;
  }
  command->argv = grown;
  command->argv[command->argc] = quick_ops_strdup(value ? value : "");
  if (!command->argv[command->argc]) {
    return APP_ERROR_MEMORY;
  }
  command->argc++;
  command->argv[command->argc] = NULL;
  return APP_SUCCESS;
}

app_error quick_op_serve_dev_command(const quick_serve_dev_request_t *request,
                                     quick_serve_dev_command_t *out) {
  if (!request || !out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_serve_dev_command_destroy(out);
  quick_serve_dev_command_init(out);
  char *quickd = quick_serve_find_quickd();
  if (!quickd) {
    return APP_ERROR_NOT_FOUND;
  }

  quick_plan_overrides_t overrides = {.profile = request->profile};
  quick_deploy_plan_t plan;
  quick_deploy_plan_init(&plan);
  app_error err = quick_deploy_plan_resolve(&overrides, request->profiles,
                                            &plan);
  if (err != APP_SUCCESS) {
    free(quickd);
    quick_deploy_plan_destroy(&plan);
    return err;
  }

  const char *port = request->port ? request->port : "9366";
  char listen[64];
  snprintf(listen, sizeof(listen), "127.0.0.1:%s", port);
  out->quickd_path = quick_ops_strdup(quickd);
  if (!out->quickd_path) {
    free(quickd);
    quick_deploy_plan_destroy(&plan);
    return APP_ERROR_MEMORY;
  }
  const char *values[] = {quickd,       "serve", "--dev",  "--dir",
                          plan.output_dir, "--site", plan.site, "--listen",
                          listen};
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    err = quick_serve_argv_append(out, values[i]);
    if (err != APP_SUCCESS) {
      free(quickd);
      quick_deploy_plan_destroy(&plan);
      return err;
    }
  }
  if (request->remote_api_profile) {
    const char *remote_profile = request->remote_api_profile[0] != '\0'
                                     ? request->remote_api_profile
                                     : plan.profile;
    quick_dev_token_result_t token;
    quick_dev_token_result_init(&token);
    quick_dev_token_request_t token_request = {.profiles = request->profiles,
                                               .profile = remote_profile,
                                               .site = plan.site,
                                               .ttl_seconds = 3600};
    err = quick_op_mint_dev_token(&token_request, &token);
    if (err == APP_SUCCESS) {
      err = quick_serve_argv_append(out, "--remote-api");
    }
    if (err == APP_SUCCESS) {
      err = quick_serve_argv_append(out, token.url);
    }
    if (err == APP_SUCCESS) {
      err = quick_serve_argv_append(out, "--remote-api-token");
    }
    if (err == APP_SUCCESS) {
      err = quick_serve_argv_append(out, token.token);
    }
    quick_dev_token_result_destroy(&token);
  }

  if (err == APP_SUCCESS && request->identity && request->identity[0] != '\0') {
    err = quick_serve_argv_append(out, "--identity");
    if (err == APP_SUCCESS) {
      err = quick_serve_argv_append(out, request->identity);
    }
  }

  free(quickd);
  quick_deploy_plan_destroy(&plan);
  return err;
}

void quick_serve_install_steps_init(quick_serve_install_steps_t *steps) {
  if (steps) {
    *steps = (quick_serve_install_steps_t){0};
  }
}

void quick_serve_install_steps_destroy(quick_serve_install_steps_t *steps) {
  if (!steps) {
    return;
  }
  for (size_t i = 0; i < steps->count; i++) {
    free(steps->steps[i].summary);
  }
  free(steps->steps);
  *steps = (quick_serve_install_steps_t){0};
}

static app_error quick_serve_install_steps_append(
    quick_serve_install_steps_t *steps, const char *summary) {
  quick_serve_install_step_t *grown = realloc(
      steps->steps, (steps->count + 1U) * sizeof(quick_serve_install_step_t));
  if (!grown) {
    return APP_ERROR_MEMORY;
  }
  steps->steps = grown;
  steps->steps[steps->count].summary = quick_ops_strdup(summary);
  if (!steps->steps[steps->count].summary) {
    return APP_ERROR_MEMORY;
  }
  steps->count++;
  return APP_SUCCESS;
}

app_error quick_op_serve_install_steps(
    const quick_serve_install_request_t *request,
    quick_serve_install_steps_t *out) {
  (void)request;
  if (!out) {
    return APP_ERROR_INVALID_ARG;
  }
  quick_serve_install_steps_destroy(out);
  quick_serve_install_steps_init(out);
  static const char *const summaries[] = {
      "create quick user and quick-deploy group",
      "create /srv/quick-style dirs with documented permissions",
      "copy quickd to /usr/local/bin/quickd",
      "write /etc/openquick/quickd.json",
      "install install/systemd/openquick.service and enable it",
      "run quickd doctor --host --json",
  };
  for (size_t i = 0; i < sizeof(summaries) / sizeof(summaries[0]); i++) {
    app_error err = quick_serve_install_steps_append(out, summaries[i]);
    if (err != APP_SUCCESS) {
      return err;
    }
  }
  return APP_SUCCESS;
}
