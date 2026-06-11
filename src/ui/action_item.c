#include "action_item.h"

#include "../cli/commands.h"

size_t app_actions_from_commands(app_action_item_t *out, size_t out_count) {
  size_t command_count = 0;
  const app_command_t *commands = app_commands(&command_count);

  // Skip hidden commands during projection so the TUI Commands list stays in
  // sync with `--help`, and number the survivors contiguously so the caller's
  // actions[id - 1] indexing stays valid.
  size_t written = 0;
  for (size_t i = 0; i < command_count; i++) {
    const app_command_t *command = &commands[i];
    if (!app_command_is_visible(command)) {
      continue;
    }
    if (out && written < out_count) {
      unsigned capabilities = APP_ACTION_CAP_NONE;
      if (command->requires_terminal) {
        capabilities |= APP_ACTION_CAP_INTERACTIVE_TERMINAL;
        capabilities |= APP_ACTION_CAP_TUI;
      }
      out[written] =
          (app_action_item_t){.id = (int)written + 1,
                              .kind = APP_ACTION_COMMAND,
                              .label = command->name,
                              .description = command->summary,
                              .disabled = false,
                              .command_name = command->name,
                              .capabilities = capabilities,
                              .examples = command->examples,
                              .example_count = command->example_count,
                              .arguments = command->arguments,
                              .argument_count = command->argument_count,
                              .options = command->options,
                              .option_count = command->option_count};
    }
    written++;
  }
  return written;
}
