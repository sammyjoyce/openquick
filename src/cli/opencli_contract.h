/*
 * OpenCLI contract metadata tables.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "commands.h"

typedef struct {
  const char *name;
  const char *url;
} app_opencli_contact_t;

typedef struct {
  const char *name;
  const char *identifier;
} app_opencli_license_t;

typedef struct {
  const char *title;
  const char *description;
  const char *version;
  app_opencli_contact_t contact;
  app_opencli_license_t license;
} app_opencli_info_t;

typedef struct {
  bool group_options;
  const char *option_argument_separator;
} app_opencli_conventions_t;

typedef struct {
  const char *name;
  const char *description;
} app_opencli_metadata_field_t;

typedef struct {
  const char *name;
  const app_opencli_metadata_field_t *fields;
  size_t field_count;
} app_opencli_metadata_group_t;

typedef struct {
  const char *opencli_version;
  app_opencli_info_t info;
  app_opencli_conventions_t conventions;
  const app_command_arg_t *root_arguments;
  size_t root_argument_count;
  const char *const *extra_examples;
  size_t extra_example_count;
  bool interactive;
  const app_opencli_metadata_group_t *metadata;
  size_t metadata_count;
} app_opencli_contract_t;

const app_opencli_contract_t *app_opencli_contract(void);

// The canonical environment-variable documentation, also published as the
// OpenCLI `metadata.environment` group. Root --help (plain and styled) renders
// the same rows so the documented environment never drifts from the machine
// contract. count receives the entry count.
const app_opencli_metadata_field_t *app_opencli_environment_docs(size_t *count);
