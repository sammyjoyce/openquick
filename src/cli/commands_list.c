#include <ctype.h>
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

app_error app_cmd_list(const app_config_t *config, int argc,
                       char *const argv[]);

typedef struct {
  const quick_list_item_t **items;
  size_t count;
  const char *sort;
} list_view_t;

static bool list_contains_ci(const char *haystack, const char *needle) {
  if (!needle || needle[0] == '\0') {
    return true;
  }
  if (!haystack) {
    return false;
  }
  size_t nlen = strlen(needle);
  for (const char *p = haystack; *p; p++) {
    size_t i = 0;
    while (i < nlen && p[i] &&
           tolower((unsigned char)p[i]) ==
               tolower((unsigned char)needle[i])) {
      i++;
    }
    if (i == nlen) {
      return true;
    }
  }
  return false;
}

static bool list_item_matches(const quick_list_item_t *item,
                              const char *filter) {
  if (!filter || filter[0] == '\0') {
    return true;
  }
  return list_contains_ci(item->name, filter) ||
         list_contains_ci(item->url, filter) ||
         list_contains_ci(item->subdomain, filter) ||
         list_contains_ci(item->deployer, filter) ||
         list_contains_ci(item->release, filter);
}

static int list_cmp_str(const char *a, const char *b) {
  if (!a) a = "";
  if (!b) b = "";
  return strcmp(a, b);
}

static const char *g_list_sort_key = "name";

static int list_item_compare(const void *a, const void *b) {
  const char *sort = g_list_sort_key ? g_list_sort_key : "name";
  const quick_list_item_t *ia = *(const quick_list_item_t *const *)a;
  const quick_list_item_t *ib = *(const quick_list_item_t *const *)b;
  int cmp = 0;
  if (strcmp(sort, "updated") == 0 || strcmp(sort, "updated_at") == 0) {
    cmp = list_cmp_str(ib->updated_at, ia->updated_at);
  } else if (strcmp(sort, "source") == 0) {
    cmp = (int)ia->source - (int)ib->source;
  } else {
    cmp = list_cmp_str(ia->name, ib->name);
  }
  if (cmp != 0) {
    return cmp;
  }
  cmp = list_cmp_str(ia->name, ib->name);
  if (cmp != 0) {
    return cmp;
  }
  return list_cmp_str(ia->source == QUICK_LIST_SOURCE_LOCAL ? "local" : "remote",
                      ib->source == QUICK_LIST_SOURCE_LOCAL ? "local" : "remote");
}

static void list_view_destroy(list_view_t *view) {
  free(view->items);
  *view = (list_view_t){0};
}

static app_error list_view_build(const quick_list_result_t *result,
                                 const char *filter, const char *sort,
                                 list_view_t *view) {
  *view = (list_view_t){.sort = sort && sort[0] ? sort : "name"};
  view->items = calloc(result->count ? result->count : 1U,
                       sizeof(*view->items));
  if (!view->items) {
    return APP_ERROR_MEMORY;
  }
  for (size_t i = 0; i < result->count; i++) {
    if (list_item_matches(&result->items[i], filter)) {
      view->items[view->count++] = &result->items[i];
    }
  }
  g_list_sort_key = view->sort;
  qsort(view->items, view->count, sizeof(*view->items), list_item_compare);
  return APP_SUCCESS;
}

static void list_write_item_json(const quick_list_item_t *item) {
  fputc('{', stdout);
  bool fc = false;
  app_json_write_string_field(stdout, "name", item->name, &fc);
  app_json_write_string_field(stdout, "url", item->url, &fc);
  app_json_write_string_field(stdout, "release", item->release, &fc);
  app_json_write_string_field(stdout, "updated_at", item->updated_at, &fc);
  app_json_write_string_field(stdout, "deployer", item->deployer, &fc);
  app_json_write_string_field(stdout, "subdomain", item->subdomain, &fc);
  app_json_write_string_field(stdout, "source",
                              item->source == QUICK_LIST_SOURCE_LOCAL ? "local"
                                                                      : "remote",
                              &fc);
  if (item->have_public) {
    app_json_write_bool_field(stdout, "public", item->is_public, &fc);
  }
  if (item->stale) {
    app_json_write_bool_field(stdout, "stale", true, &fc);
  }
  fputc('}', stdout);
}

static void list_write_remote_status_json(const quick_list_result_t *result,
                                          bool *comma) {
  app_json_write_bool_field(stdout, "remote_ok", result->remote_ok, comma);
  app_json_write_bool_field(stdout, "remote_requested",
                            result->remote_requested, comma);
  if (result->remote_error) {
    app_json_write_raw_field(stdout, "remote_error", "{", comma);
    bool ec = false;
    app_json_write_string_field(stdout, "phase", result->remote_phase, &ec);
    app_json_write_string_field(stdout, "error", result->remote_error, &ec);
    app_json_write_string_field(stdout, "remediation",
                                result->remote_remediation, &ec);
    app_json_end_object(stdout);
  }
}

app_error app_cmd_list(const app_config_t *config, int argc,
                       char *const argv[]) {
  quick_profile_config_t profiles;
  app_error err = quick_cmd_load_profiles(&profiles);
  if (err != APP_SUCCESS) {
    return err;
  }

  quick_list_request_t request = {
      .profiles = &profiles,
      .overrides = {.profile = quick_cmd_value(argc, argv, "--profile")},
      .remote = quick_cmd_flag(argc, argv, "--remote"),
  };
  quick_list_result_t result;
  quick_list_result_init(&result);
  err = quick_op_list(&request, &result);
  quick_profile_config_destroy(&profiles);
  if (err != APP_SUCCESS) {
    quick_print_error(config, "failed to resolve list profile");
    quick_list_result_destroy(&result);
    return err;
  }

  const char *filter = quick_cmd_value(argc, argv, "--filter");
  const char *sort = quick_cmd_value(argc, argv, "--sort");
  if (sort && sort[0] && strcmp(sort, "name") != 0 &&
      strcmp(sort, "updated") != 0 && strcmp(sort, "updated_at") != 0 &&
      strcmp(sort, "source") != 0) {
    quick_print_error(config, "list --sort must be name, updated, updated_at, or source");
    quick_list_result_destroy(&result);
    return APP_ERROR_VALIDATION;
  }
  list_view_t view;
  err = list_view_build(&result, filter, sort, &view);
  if (err != APP_SUCCESS) {
    quick_list_result_destroy(&result);
    return err;
  }

  if (app_config_is_json_output(config) || quick_cmd_flag(argc, argv, "--json")) {
    bool comma = false;
    app_json_begin_object(stdout);
    app_json_write_string_field(stdout, "format_version", "1.0", &comma);
    list_write_remote_status_json(&result, &comma);
    app_json_write_raw_field(stdout, "filter", "{", &comma);
    bool fc = false;
    app_json_write_string_field(stdout, "query", filter ? filter : "", &fc);
    app_json_write_string_field(stdout, "sort", view.sort, &fc);
    char count_buf[64];
    snprintf(count_buf, sizeof(count_buf), "%zu", result.count);
    app_json_write_raw_field(stdout, "total", count_buf, &fc);
    snprintf(count_buf, sizeof(count_buf), "%zu", view.count);
    app_json_write_raw_field(stdout, "matched", count_buf, &fc);
    app_json_end_object(stdout);
    app_json_write_raw_field(stdout, "sites", "[", &comma);
    for (size_t i = 0; i < view.count; i++) {
      if (i > 0) {
        fputc(',', stdout);
      }
      list_write_item_json(view.items[i]);
    }
    fputc(']', stdout);
    app_json_end_object(stdout);
    app_json_end_line(stdout);
  } else {
    if (filter && filter[0]) {
      app_output_format(config, false, "filter      %s", filter);
    }
    app_output_format(config, false, "sort        %s", view.sort);
    app_output("site            source   url                                      updated              by", config, false);
    for (size_t i = 0; i < view.count; i++) {
      const quick_list_item_t *item = view.items[i];
      app_output_format(config, false, "%-15s %-8s %-40s %-20s %s%s",
                        item->name,
                        item->source == QUICK_LIST_SOURCE_LOCAL ? "local" : "remote",
                        item->url,
                        item->updated_at ? item->updated_at : "unknown",
                        item->deployer ? item->deployer : "",
                        item->stale ? " (stale)" : "");
    }
    if (view.count == 0) {
      app_output("(no sites matched)", config, false);
    }
    if (!result.remote_ok && result.remote_requested && result.remote_error) {
      app_output_format(config, true, "remote list unavailable (%s): %s",
                        result.remote_phase ? result.remote_phase : "remote",
                        result.remote_error);
      if (result.remote_remediation) {
        app_output_format(config, true, "remediation: %s",
                          result.remote_remediation);
      }
    }
  }

  list_view_destroy(&view);
  quick_list_result_destroy(&result);
  return APP_SUCCESS;
}
