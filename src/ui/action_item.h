/*
 * Curses-free selectable action descriptors. These let CLI command metadata and
 * TUI menu rows share labels/capabilities without sharing renderers or
 * dispatch.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

// The seam borrows argument/option metadata straight from the CLI command
// table, so it needs those struct types. action_item.c already depends on the
// command table to project it; pulling the types here lets a TUI detail view
// present the same usage the CLI `--help` renders without copying any data.
#include "../cli/commands.h"

typedef enum {
  APP_ACTION_COMMAND = 0,
  APP_ACTION_CALLBACK,
  APP_ACTION_SEPARATOR,
} app_action_kind_t;

typedef enum {
  APP_ACTION_CAP_NONE = 0,
  APP_ACTION_CAP_INTERACTIVE_TERMINAL = 1u << 0,
  APP_ACTION_CAP_TUI = 1u << 1,
} app_action_capability_t;

typedef struct {
  int id;
  app_action_kind_t kind;
  const char *label;
  const char *description;
  bool disabled;
  const char *command_name;
  unsigned capabilities;
  // Borrowed, NUL-terminated example invocation strings (and their count) from
  // the source command table, so a TUI detail view can show the same
  // ready-to-run examples the CLI `--help` prints. NULL when the action carries
  // no examples. The pointers are owned by the static command table.
  const char *const *examples;
  size_t example_count;
  // Borrowed argument and option metadata from the source command, so a TUI
  // detail view can present the same usage (arguments and command options) the
  // CLI `--help` renders. NULL/0 when the command declares none. Like the
  // examples above, these point into the static command table and are never
  // freed by the action.
  const app_command_arg_t *arguments;
  size_t argument_count;
  const app_command_option_t *options;
  size_t option_count;
} app_action_item_t;

// Project the CLI command table into action descriptors. Commands flagged
// hidden_from_help are skipped so the TUI Commands list matches `--help`, and
// the written descriptors are assigned contiguous ids (1..N) matching their
// slot in `out`. Returns the number of visible commands (independent of
// out_count), so a NULL/zero-capacity call probes the count.
size_t app_actions_from_commands(app_action_item_t *out, size_t out_count);
