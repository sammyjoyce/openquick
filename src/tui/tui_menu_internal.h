/*
 * tui_menu_internal.h - exposed for unit tests and for the view/controller
 * layer in tui_menu.c. Not part of the public API.
 *
 * The state struct is forward-declared opaque so the model layer
 * (tui_menu_model.c) can be tested without dragging in ncurses headers.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#include "tui_menu.h"

typedef struct tui_menu_state tui_menu_state_t;

/* Lifecycle. */
APP_NODISCARD tui_menu_status_t
tui_menu_state_create(const tui_menu_config_t *cfg, tui_menu_state_t **out);
void tui_menu_state_destroy(tui_menu_state_t *s);

/* Read-only accessors used by view + tests. */
int tui_menu_state_visible_count(const tui_menu_state_t *s);
int tui_menu_state_selected_index(
    const tui_menu_state_t *s); /* index into cfg->items[], or -1 */
int tui_menu_state_selected_visible(
    const tui_menu_state_t *s); /* index into visible_indices[] */
int tui_menu_state_top_visible(const tui_menu_state_t *s);
void tui_menu_state_ensure_selection_visible(tui_menu_state_t *s,
                                             int viewport_rows);
bool tui_menu_state_select_visible(tui_menu_state_t *s, int visible_row);
const wchar_t *tui_menu_state_label_wcs(const tui_menu_state_t *s,
                                        int items_index);
wchar_t tui_menu_state_mnemonic(const tui_menu_state_t *s, int items_index);
bool tui_menu_state_search_active(const tui_menu_state_t *s);
const wchar_t *tui_menu_state_search_query(const tui_menu_state_t *s);

/* Mutation - pure data ops, no ncurses. */
void tui_menu_state_step(
    tui_menu_state_t *s,
    int direction); /* +1/-1, skips separators + disabled */
void tui_menu_state_page(tui_menu_state_t *s, int direction, int page_size);
void tui_menu_state_home(tui_menu_state_t *s);
void tui_menu_state_end(tui_menu_state_t *s);
void tui_menu_state_search_open(tui_menu_state_t *s);
void tui_menu_state_search_close(tui_menu_state_t *s);
void tui_menu_state_search_append(tui_menu_state_t *s, wchar_t ch);
void tui_menu_state_search_backspace(tui_menu_state_t *s);

/* Mnemonic dispatch. Returns the items[] index that should auto-confirm
 * (>= 0), or -1 if no unique match (selection may have advanced to cycle
 * among candidates). out_beep is set to true when the caller should beep
 * (disabled match, or ambiguous match). */
int tui_menu_state_mnemonic_jump(tui_menu_state_t *s, wchar_t key,
                                 bool *out_beep);

/* Numeric accelerator: jump to visible row 0..8 (no auto-confirm). */
void tui_menu_state_numeric_jump(tui_menu_state_t *s, int visible_row);

/* Numeric labels count listable (non-separator) rows so the displayed
 * accelerators stay contiguous (1, 2, 3, ...) even when separators split
 * the list. number_for_row returns the 1-based label for a visible row (0
 * for separators / out of range); row_for_number is its inverse, mapping a
 * pressed digit back to the visible row it labels (-1 if none). */
int tui_menu_state_number_for_row(const tui_menu_state_t *s, int visible_row);
int tui_menu_state_row_for_number(const tui_menu_state_t *s, int number);

/* Return the items[] index at the given visible row, or -1. */
int tui_menu_state_visible_at(const tui_menu_state_t *s, int visible_row);
const tui_menu_config_t *tui_menu_state_config(const tui_menu_state_t *s);
