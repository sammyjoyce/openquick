#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../core/config.h"
#include "../core/deploy_plan.h"
#include "../core/error.h"
#include "../core/ops.h"
#include "../core/process.h"
#include "../io/output.h"
#include "commands.h"
#include "commands_openquick.h"

app_error app_cmd_deploy(const app_config_t *config, int argc,
                         char *const argv[]);
app_error app_cmd_serve(const app_config_t *config, int argc,
                        char *const argv[]);

typedef struct deploy_dry_summary_t deploy_dry_summary_t;
static void deploy_print_json_summary(const deploy_dry_summary_t *summary,
                                      bool *comma);
static void deploy_print_human_summary(const app_config_t *config,
                                       const deploy_dry_summary_t *summary);

static void deploy_print_json_plan(const quick_deploy_plan_t *plan,
                                   bool no_delete, bool checksum,
                                   const deploy_dry_summary_t *summary) {
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
  if (summary) {
    deploy_print_json_summary(summary, &comma);
  }
  app_json_end_object(stdout);
  app_json_end_line(stdout);
}

static void deploy_print_human_plan(const app_config_t *config,
                                    const quick_deploy_plan_t *plan,
                                    bool no_delete, bool checksum,
                                    const deploy_dry_summary_t *summary) {
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
  if (summary) {
    deploy_print_human_summary(config, summary);
  }
}

static const char *deploy_phase_label(quick_deploy_phase_t phase) {
  switch (phase) {
  case QUICK_DEPLOY_PHASE_BUILD:
    return "build";
  case QUICK_DEPLOY_PHASE_BOOTSTRAP_CHECK:
    return "bootstrap-check";
  case QUICK_DEPLOY_PHASE_PREPARE:
    return "prepare";
  case QUICK_DEPLOY_PHASE_TRANSFER:
    return "transfer";
  case QUICK_DEPLOY_PHASE_ACTIVATE:
    return "activate";
  case QUICK_DEPLOY_PHASE_RECORD:
    return "record";
  case QUICK_DEPLOY_PHASE_NONE:
  default:
    return "unknown";
  }
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

typedef struct {
  char *path;
  char hash[17];
  unsigned long long size;
  bool seen;
} deploy_manifest_entry_t;

typedef struct {
  deploy_manifest_entry_t *items;
  size_t count;
} deploy_manifest_t;

struct deploy_dry_summary_t {
  deploy_manifest_t previous;
  deploy_manifest_t current;
  char **added;
  char **changed;
  char **deleted;
  char **excluded;
  size_t added_count;
  size_t changed_count;
  size_t deleted_count;
  size_t excluded_count;
};

static char *deploy_strdup(const char *s) {
  if (!s) {
    return NULL;
  }
  size_t n = strlen(s) + 1U;
  char *out = malloc(n);
  if (out) {
    memcpy(out, s, n);
  }
  return out;
}

static void deploy_manifest_destroy(deploy_manifest_t *m) {
  if (!m) {
    return;
  }
  for (size_t i = 0; i < m->count; i++) {
    free(m->items[i].path);
  }
  free(m->items);
  *m = (deploy_manifest_t){0};
}

static void deploy_summary_destroy(deploy_dry_summary_t *s) {
  if (!s) {
    return;
  }
  deploy_manifest_destroy(&s->previous);
  deploy_manifest_destroy(&s->current);
  for (size_t i = 0; i < s->added_count; i++) free(s->added[i]);
  for (size_t i = 0; i < s->changed_count; i++) free(s->changed[i]);
  for (size_t i = 0; i < s->deleted_count; i++) free(s->deleted[i]);
  for (size_t i = 0; i < s->excluded_count; i++) free(s->excluded[i]);
  free(s->added);
  free(s->changed);
  free(s->deleted);
  free(s->excluded);
  *s = (deploy_dry_summary_t){0};
}

static bool deploy_string_list_append(char ***items, size_t *count,
                                      const char *value) {
  char **grown = realloc(*items, (*count + 1U) * sizeof(*grown));
  if (!grown) {
    return false;
  }
  *items = grown;
  (*items)[*count] = deploy_strdup(value);
  if (!(*items)[*count]) {
    return false;
  }
  (*count)++;
  return true;
}

static bool deploy_manifest_append(deploy_manifest_t *m, const char *path,
                                   const char hash[17],
                                   unsigned long long size) {
  deploy_manifest_entry_t *grown =
      realloc(m->items, (m->count + 1U) * sizeof(*grown));
  if (!grown) {
    return false;
  }
  m->items = grown;
  m->items[m->count] = (deploy_manifest_entry_t){0};
  m->items[m->count].path = deploy_strdup(path);
  if (!m->items[m->count].path) {
    return false;
  }
  memcpy(m->items[m->count].hash, hash, 17U);
  m->items[m->count].size = size;
  m->count++;
  return true;
}

static deploy_manifest_entry_t *deploy_manifest_find(deploy_manifest_t *m,
                                                     const char *path) {
  for (size_t i = 0; m && i < m->count; i++) {
    if (strcmp(m->items[i].path, path) == 0) {
      return &m->items[i];
    }
  }
  return NULL;
}

static char *deploy_path_with_trailing_slash(const char *path) {
  if (!path) {
    return NULL;
  }
  size_t len = strlen(path);
  const bool has_slash = len > 0 && path[len - 1U] == '/';
  char *out = malloc(len + (has_slash ? 1U : 2U));
  if (!out) {
    return NULL;
  }
  memcpy(out, path, len);
  if (!has_slash) {
    out[len++] = '/';
  }
  out[len] = '\0';
  return out;
}

static char *deploy_make_temp_dir(void) {
  const char *base = getenv("TMPDIR");
  if (!base || base[0] == '\0') {
    base = "/tmp";
  }
  static const char suffix[] = "openquick-deploy-dry-XXXXXX";
  size_t base_len = strlen(base);
  const bool needs_slash = base_len > 0 && base[base_len - 1U] != '/';
  char *tmpl = malloc(base_len + (needs_slash ? 1U : 0U) + sizeof(suffix));
  if (!tmpl) {
    return NULL;
  }
  memcpy(tmpl, base, base_len);
  size_t pos = base_len;
  if (needs_slash) {
    tmpl[pos++] = '/';
  }
  memcpy(tmpl + pos, suffix, sizeof(suffix));
  if (!mkdtemp(tmpl)) {
    free(tmpl);
    return NULL;
  }
  return tmpl;
}

static app_error deploy_hash_file(const char *path, char out[17],
                                  unsigned long long *size_out) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return APP_ERROR_IO;
  }
  uint64_t h = 1469598103934665603ULL;
  unsigned char buf[8192];
  unsigned long long size = 0;
  for (;;) {
    size_t n = fread(buf, 1, sizeof(buf), f);
    for (size_t i = 0; i < n; i++) {
      h ^= (uint64_t)buf[i];
      h *= 1099511628211ULL;
    }
    size += (unsigned long long)n;
    if (n < sizeof(buf)) {
      if (ferror(f)) {
        fclose(f);
        return APP_ERROR_IO;
      }
      break;
    }
  }
  fclose(f);
  snprintf(out, 17, "%016llx", (unsigned long long)h);
  if (size_out) {
    *size_out = size;
  }
  return APP_SUCCESS;
}

static app_error deploy_hash_symlink(const char *path, const struct stat *st,
                                     char out[17],
                                     unsigned long long *size_out) {
  size_t cap = st && st->st_size > 0 ? (size_t)st->st_size + 1U : 256U;
  char *target = NULL;
  ssize_t n = -1;
  for (;;) {
    char *grown = realloc(target, cap);
    if (!grown) {
      free(target);
      return APP_ERROR_MEMORY;
    }
    target = grown;
    n = readlink(path, target, cap);
    if (n < 0) {
      free(target);
      return APP_ERROR_IO;
    }
    if ((size_t)n < cap) {
      break;
    }
    if (cap > (SIZE_MAX / 2U)) {
      free(target);
      return APP_ERROR_OUT_OF_RANGE;
    }
    cap *= 2U;
  }

  uint64_t h = 1469598103934665603ULL;
  static const unsigned char prefix[] = {'s', 'y', 'm', 'l', 'i', 'n', 'k', 0};
  for (size_t i = 0; i < sizeof(prefix); i++) {
    h ^= (uint64_t)prefix[i];
    h *= 1099511628211ULL;
  }
  for (ssize_t i = 0; i < n; i++) {
    h ^= (uint64_t)(unsigned char)target[i];
    h *= 1099511628211ULL;
  }
  snprintf(out, 17, "%016llx", (unsigned long long)h);
  if (size_out) {
    *size_out = (unsigned long long)n;
  }
  free(target);
  return APP_SUCCESS;
}

static app_error deploy_hash_selected_path(const char *root, const char *rel,
                                           char out[17],
                                           unsigned long long *size_out) {
  char *path = quick_path_join_cli(root, rel);
  if (!path) {
    return APP_ERROR_MEMORY;
  }
  struct stat st;
  if (lstat(path, &st) != 0) {
    free(path);
    return APP_ERROR_IO;
  }
  app_error err = APP_ERROR_INVALID_ARG;
  if (S_ISREG(st.st_mode)) {
    err = deploy_hash_file(path, out, size_out);
  } else if (S_ISLNK(st.st_mode)) {
    err = deploy_hash_symlink(path, &st, out, size_out);
  }
  free(path);
  return err;
}

static char *deploy_manifest_path(const quick_deploy_plan_t *plan) {
  char *dir = quick_path_join_cli(plan->site_root, ".quick/deployments");
  if (!dir) {
    return NULL;
  }
  size_t len = strlen(plan->profile) + strlen(".manifest") + 1U;
  char *file = malloc(len);
  if (!file) {
    free(dir);
    return NULL;
  }
  snprintf(file, len, "%s.manifest", plan->profile);
  char *path = quick_path_join_cli(dir, file);
  free(file);
  free(dir);
  return path;
}

static app_error deploy_manifest_load(const char *path, deploy_manifest_t *m) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return errno == ENOENT ? APP_SUCCESS : APP_ERROR_IO;
  }
  char line[4096];
  while (fgets(line, sizeof(line), f)) {
    char hash[17] = {0};
    unsigned long long size = 0;
    char rel[3500] = {0};
    if (sscanf(line, "%16s %llu %3499[^\n]", hash, &size, rel) == 3) {
      if (!deploy_manifest_append(m, rel, hash, size)) {
        fclose(f);
        return APP_ERROR_MEMORY;
      }
    }
  }
  fclose(f);
  return APP_SUCCESS;
}

static app_error deploy_manifest_write(const char *path,
                                       const deploy_manifest_t *m) {
  const char *slash = strrchr(path, '/');
  if (slash) {
    size_t len = (size_t)(slash - path);
    char *dir = malloc(len + 1U);
    if (!dir) {
      return APP_ERROR_MEMORY;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    (void)quick_mkdir_p_cli(dir, 0755);
    free(dir);
  }
  FILE *f = fopen(path, "wb");
  if (!f) {
    return APP_ERROR_IO;
  }
  for (size_t i = 0; m && i < m->count; i++) {
    fprintf(f, "%s %llu %s\n", m->items[i].hash, m->items[i].size,
            m->items[i].path);
  }
  return fclose(f) == 0 ? APP_SUCCESS : APP_ERROR_IO;
}

static app_error deploy_summary_record_selected_path(
    const char *root, const char *rel, deploy_dry_summary_t *summary) {
  if (!rel || rel[0] == '\0' || strcmp(rel, ".") == 0) {
    return APP_SUCCESS;
  }
  char hash[17];
  unsigned long long size = 0;
  app_error err = deploy_hash_selected_path(root, rel, hash, &size);
  if (err != APP_SUCCESS) {
    return err;
  }
  if (!deploy_manifest_append(&summary->current, rel, hash, size)) {
    return APP_ERROR_MEMORY;
  }
  deploy_manifest_entry_t *old = deploy_manifest_find(&summary->previous, rel);
  if (!old) {
    return deploy_string_list_append(&summary->added, &summary->added_count, rel)
               ? APP_SUCCESS
               : APP_ERROR_MEMORY;
  }
  old->seen = true;
  if (old->size != size || strcmp(old->hash, hash) != 0) {
    return deploy_string_list_append(&summary->changed, &summary->changed_count,
                                     rel)
               ? APP_SUCCESS
               : APP_ERROR_MEMORY;
  }
  return APP_SUCCESS;
}

static app_error deploy_parse_rsync_excluded_line(
    char *line, deploy_dry_summary_t *summary) {
  static const char file_prefix[] = "[sender] hiding file ";
  static const char dir_prefix[] = "[sender] hiding directory ";
  const char *rel = NULL;
  if (strncmp(line, file_prefix, sizeof(file_prefix) - 1U) == 0) {
    rel = line + sizeof(file_prefix) - 1U;
  } else if (strncmp(line, dir_prefix, sizeof(dir_prefix) - 1U) == 0) {
    rel = line + sizeof(dir_prefix) - 1U;
  }
  if (!rel) {
    return APP_SUCCESS;
  }
  char *because = strstr((char *)rel, " because of pattern ");
  if (!because || because == rel) {
    return APP_SUCCESS;
  }
  *because = '\0';
  return deploy_string_list_append(&summary->excluded, &summary->excluded_count,
                                   rel)
             ? APP_SUCCESS
             : APP_ERROR_MEMORY;
}

static app_error deploy_parse_rsync_output(const char *output, const char *root,
                                           deploy_dry_summary_t *summary) {
  if (!output) {
    return APP_SUCCESS;
  }
  const char *p = output;
  while (*p) {
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    while (len > 0 && p[len - 1U] == '\r') {
      len--;
    }
    char *line = malloc(len + 1U);
    if (!line) {
      return APP_ERROR_MEMORY;
    }
    memcpy(line, p, len);
    line[len] = '\0';

    app_error err = deploy_parse_rsync_excluded_line(line, summary);
    if (err == APP_SUCCESS && root && len > 12U && line[11] == ' ' &&
        (line[1] == 'f' || line[1] == 'L')) {
      err = deploy_summary_record_selected_path(root, line + 12, summary);
    }
    free(line);
    if (err != APP_SUCCESS) {
      return err;
    }
    if (!end) {
      break;
    }
    p = end + 1;
  }
  return APP_SUCCESS;
}

static app_error deploy_enumerate_rsync_selection(
    const quick_deploy_plan_t *plan, bool no_delete,
    deploy_dry_summary_t *summary) {
  quick_ignore_t ignore;
  quick_ignore_init(&ignore);
  app_error err = quick_ignore_load_for_site(plan->site_root, &ignore);
  if (err != APP_SUCCESS) {
    quick_ignore_destroy(&ignore);
    return err;
  }
  size_t exclude_argc = 0;
  char **exclude_args = quick_ignore_to_rsync_args(&ignore, &exclude_argc);
  if (ignore.count > 0 && !exclude_args) {
    quick_ignore_destroy(&ignore);
    return APP_ERROR_MEMORY;
  }

  struct stat root_st;
  if (lstat(plan->output_dir, &root_st) != 0) {
    err = errno == ENOENT ? APP_SUCCESS : APP_ERROR_IO;
    quick_ignore_args_destroy(exclude_args, exclude_argc);
    quick_ignore_destroy(&ignore);
    return err;
  }

  char *source = deploy_path_with_trailing_slash(plan->output_dir);
  char *temp_dir = deploy_make_temp_dir();
  char *dest = temp_dir ? deploy_path_with_trailing_slash(temp_dir) : NULL;
  const size_t max_args = 24U + exclude_argc;
  char **rsync_argv = calloc(max_args, sizeof(char *));
  if (!source || !temp_dir || !dest || !rsync_argv) {
    free(source);
    free(dest);
    if (temp_dir) {
      (void)rmdir(temp_dir);
    }
    free(temp_dir);
    free(rsync_argv);
    quick_ignore_args_destroy(exclude_args, exclude_argc);
    quick_ignore_destroy(&ignore);
    return APP_ERROR_MEMORY;
  }

  size_t ai = 0;
  rsync_argv[ai++] = "rsync";
  rsync_argv[ai++] = "-a";
  rsync_argv[ai++] = "--dry-run";
  rsync_argv[ai++] = "--itemize-changes";
  if (!no_delete) {
    rsync_argv[ai++] = "--delete";
  }
  rsync_argv[ai++] = "--partial-dir=.rsync-partial";
  rsync_argv[ai++] = "--safe-links";
  rsync_argv[ai++] = "--chmod=Dg+s,ug+rwX,o-rwx";
  rsync_argv[ai++] = "--debug=FILTER";
  rsync_argv[ai++] = "--out-format=%i %n";
  for (size_t i = 0; i < exclude_argc; i++) {
    rsync_argv[ai++] = exclude_args[i];
  }
  rsync_argv[ai++] = source;
  rsync_argv[ai++] = dest;
  rsync_argv[ai] = NULL;

  quick_process_result_t rsync = {0};
  err = quick_process_capture(rsync_argv, NULL, &rsync);
  if (err == APP_SUCCESS && rsync.exit_code != 0) {
    err = APP_ERROR_IO;
  }
  if (err == APP_SUCCESS) {
    /* Keep dry-run selection coupled to the deploy transfer: rsync applies
       --safe-links and quick_ignore_to_rsync_args() excludes here exactly as
       quick_op_deploy_execute() does for the remote transfer. */
    err = deploy_parse_rsync_output(rsync.out, plan->output_dir, summary);
  }
  if (err == APP_SUCCESS) {
    err = deploy_parse_rsync_output(rsync.err, NULL, summary);
  }

  quick_process_result_destroy(&rsync);
  free(rsync_argv);
  free(source);
  free(dest);
  (void)rmdir(temp_dir);
  free(temp_dir);
  quick_ignore_args_destroy(exclude_args, exclude_argc);
  quick_ignore_destroy(&ignore);
  return err;
}

static app_error deploy_dry_summary_build(const quick_deploy_plan_t *plan,
                                          bool no_delete,
                                          deploy_dry_summary_t *summary) {
  memset(summary, 0, sizeof(*summary));
  char *manifest = deploy_manifest_path(plan);
  if (!manifest) {
    return APP_ERROR_MEMORY;
  }
  app_error err = deploy_manifest_load(manifest, &summary->previous);
  if (err == APP_SUCCESS) {
    err = deploy_enumerate_rsync_selection(plan, no_delete, summary);
  }
  if (err == APP_SUCCESS && !no_delete) {
    for (size_t i = 0; i < summary->previous.count; i++) {
      if (!summary->previous.items[i].seen) {
        if (!deploy_string_list_append(&summary->deleted, &summary->deleted_count,
                                       summary->previous.items[i].path)) {
          err = APP_ERROR_MEMORY;
          break;
        }
      }
    }
  }
  free(manifest);
  return err;
}

static void deploy_write_string_array(char **items, size_t count) {
  fputc('[', stdout);
  for (size_t i = 0; i < count; i++) {
    if (i > 0) {
      fputc(',', stdout);
    }
    app_json_write_string(stdout, items[i]);
  }
  fputc(']', stdout);
}

static void deploy_json_key_array(const char *key, char **items, size_t count,
                                  bool *comma) {
  if (*comma) {
    fputc(',', stdout);
  }
  *comma = true;
  app_json_write_string(stdout, key);
  fputc(':', stdout);
  deploy_write_string_array(items, count);
}

static void deploy_print_json_summary(const deploy_dry_summary_t *summary,
                                      bool *comma) {
  char buf[64];
  app_json_write_raw_field(stdout, "summary", "{", comma);
  bool sc = false;
  snprintf(buf, sizeof(buf), "%zu", summary->added_count);
  app_json_write_raw_field(stdout, "added_count", buf, &sc);
  snprintf(buf, sizeof(buf), "%zu", summary->changed_count);
  app_json_write_raw_field(stdout, "changed_count", buf, &sc);
  snprintf(buf, sizeof(buf), "%zu", summary->deleted_count);
  app_json_write_raw_field(stdout, "deleted_count", buf, &sc);
  snprintf(buf, sizeof(buf), "%zu", summary->excluded_count);
  app_json_write_raw_field(stdout, "excluded_count", buf, &sc);
  app_json_end_object(stdout);
  deploy_json_key_array("added", summary->added, summary->added_count, comma);
  deploy_json_key_array("changed", summary->changed, summary->changed_count, comma);
  deploy_json_key_array("deleted", summary->deleted, summary->deleted_count, comma);
  deploy_json_key_array("excluded", summary->excluded, summary->excluded_count, comma);
}

static void deploy_print_human_summary(const app_config_t *config,
                                       const deploy_dry_summary_t *summary) {
  app_output_format(config, false,
                    "  summary     %zu added, %zu changed, %zu deleted, %zu excluded",
                    summary->added_count, summary->changed_count,
                    summary->deleted_count, summary->excluded_count);
  if (summary->deleted_count > 0) {
    app_output("  deletes     destructive deletes planned:", config, false);
    size_t limit = summary->deleted_count < 5 ? summary->deleted_count : 5;
    for (size_t i = 0; i < limit; i++) {
      app_output_format(config, false, "              - %s", summary->deleted[i]);
    }
    if (summary->deleted_count > limit) {
      app_output_format(config, false, "              ... and %zu more",
                        summary->deleted_count - limit);
    }
  }
  if (summary->excluded_count > 0) {
    size_t limit = summary->excluded_count < 5 ? summary->excluded_count : 5;
    app_output("  excluded    ignored by .quickignore:", config, false);
    for (size_t i = 0; i < limit; i++) {
      app_output_format(config, false, "              - %s", summary->excluded[i]);
    }
  }
}

static void deploy_manifest_record_success(const quick_deploy_plan_t *plan) {
  deploy_dry_summary_t summary;
  if (deploy_dry_summary_build(plan, false, &summary) == APP_SUCCESS) {
    char *manifest = deploy_manifest_path(plan);
    if (manifest) {
      (void)deploy_manifest_write(manifest, &summary.current);
      free(manifest);
    }
  }
  deploy_summary_destroy(&summary);
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
    deploy_dry_summary_t summary;
    app_error summary_err = deploy_dry_summary_build(&plan, no_delete, &summary);
    if (summary_err != APP_SUCCESS) {
      quick_print_error(config, "failed to build dry-run transfer summary");
      quick_deploy_plan_destroy(&plan);
      quick_profile_config_destroy(&profiles);
      return summary_err;
    }
    if (app_config_is_json_output(config)) {
      deploy_print_json_plan(&plan, no_delete, checksum, &summary);
    } else {
      deploy_print_human_plan(config, &plan, no_delete, checksum, &summary);
    }
    deploy_summary_destroy(&summary);
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
      deploy_manifest_record_success(&plan);
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
    if (err != APP_SUCCESS && result.failure_phase != QUICK_DEPLOY_PHASE_NONE) {
      app_output_format(config, true, "phase       %s",
                        deploy_phase_label(result.failure_phase));
    }
    if (err != APP_SUCCESS && result.cleanup_attempted) {
      if (result.cleanup_ok) {
        app_output_format(config, true, "cleanup     %s", result.cleanup_message ? result.cleanup_message : "remote staging cleaned");
        app_output("retry       staging cleaned; rerun quick deploy when the transient error is resolved", config, true);
      } else {
        app_output_format(config, true, "cleanup     failed: %s", result.cleanup_message ? result.cleanup_message : "remote staging remains");
        app_output("retry       clean the reported staging path before retrying deploy", config, true);
      }
    }
    quick_deploy_result_destroy(&result);
    free(deployer);
  }

  quick_deploy_plan_destroy(&plan);
  quick_profile_config_destroy(&profiles);
  return err;
}
