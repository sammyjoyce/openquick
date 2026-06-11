#include "tui_menu_adapter.h"

bool tui_menu_item_from_action(const app_action_item_t *action,
                               tui_menu_item_t *out) {
  if (!action || !out) {
    return false;
  }

  if (action->kind == APP_ACTION_SEPARATOR) {
    *out = (tui_menu_item_t){.kind = TUI_MENU_ITEM_SEPARATOR};
    return true;
  }

  *out = (tui_menu_item_t){.label = action->label,
                           .description = action->description,
                           .id = action->id,
                           .disabled = action->disabled,
                           .kind = TUI_MENU_ITEM_NORMAL};
  return true;
}
