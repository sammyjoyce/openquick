/*
 * Adapter from curses-free action descriptors to curses-free tui_menu_item_t
 * data. Rendering and event handling stay in tui_menu.c.
 */
#pragma once

#include <stdbool.h>

#include "../ui/action_item.h"
#include "tui_menu.h"

bool tui_menu_item_from_action(const app_action_item_t *action,
                               tui_menu_item_t *out);
