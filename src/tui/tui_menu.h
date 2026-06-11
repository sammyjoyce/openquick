/*
 * tui_menu.h - reference-grade ncurses menu component.
 *
 * Public API for a blocking, modal menu that supports separators, mnemonics
 * (&Label syntax), incremental search, mouse, scroll indicators, and
 * SIGINT-aware interrupt handling. The layout is a flat "dawn-style" stack:
 * a centered title, a meta line, a rule, the list, and a key-hint footer.
 *
 * The menu is "reference" rather than a framework: one entry point
 * (tui_show_menu), one config struct in, one result struct out. The
 * implementation is split into a pure model layer (tui_menu_model.c,
 * no ncurses) and a view+controller layer (tui_menu.c).
 *
 * Ownership contract: all pointers reachable from `config` (including
 * config->title, items[i].label, items[i].description) must remain valid
 * until tui_show_menu returns. The menu makes no copies of caller data.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../core/error.h"
#include "../core/types.h"

/*
 * Compile-time version of the supported tui-menu seam (see docs/CONTRACTS.md).
 * Bump when this header's public API changes in a consumer-visible way so that
 * downstream code linking libtui-menu.a can feature-detect:
 *
 *   #if defined(TUI_MENU_VERSION) && \
 *       TUI_MENU_VERSION >= TUI_MENU_VERSION_ENCODE(1, 0, 0)
 *     // use the 1.0 menu API
 *   #endif
 */
#define TUI_MENU_VERSION_MAJOR 1
#define TUI_MENU_VERSION_MINOR 0
#define TUI_MENU_VERSION_PATCH 0
#define TUI_MENU_VERSION_ENCODE(major, minor, patch) \
  (((major) * 1000000) + ((minor) * 1000) + (patch))
#define TUI_MENU_VERSION                                                  \
  TUI_MENU_VERSION_ENCODE(TUI_MENU_VERSION_MAJOR, TUI_MENU_VERSION_MINOR, \
                          TUI_MENU_VERSION_PATCH)

typedef struct tui_window tui_window_t; /* defined in tui.h */

/* The title is uppercased into a fixed stack buffer for the centered header;
 * titles longer than this (in bytes) are truncated with a trailing '~'. */
#define TUI_MENU_TITLE_MAX 64

typedef enum {
  TUI_MENU_ITEM_NORMAL = 0,
  TUI_MENU_ITEM_SEPARATOR, /* visual rule; navigation skips silently */
} tui_menu_item_kind_t;

typedef struct {
  const char *label; /* NUL-terminated UTF-8; "&x" marks mnemonic 'x'; "&&" =>
                        literal '&' */
  const char *description; /* optional UTF-8; the selected item's description
                              is surfaced on the meta line under the title */
  int id; /* returned via result.selected_id when chosen; use non-zero ids for
             selectable actions. 0 is reserved for TUI_MENU_ID_NONE. */
  bool disabled; /* zero-init => ENABLED. Polarity is intentional. */
  tui_menu_item_kind_t kind;
} tui_menu_item_t;

enum {
  TUI_MENU_ID_NONE = 0,
};

typedef struct {
  const char *title;
  const char *subtitle; /* optional breadcrumb shown on the meta line under
                           the title when the selected item has no
                           description (e.g. "myapp · v0.1.0") */
  const tui_menu_item_t *items;
  int item_count;
  int default_index; /* -1 picks first enabled */
  int frame_height;  /* requested frame height; 0 uses the current/default */
  int frame_width;   /* requested frame width; 0 uses the current/default */
  bool enable_search;
  bool enable_mouse;
  bool enable_menu_key; /* Esc opens an overlay instead of cancelling; the
                           caller handles TUI_MENU_MENU. q still cancels. */
  bool show_numeric_keys;
} tui_menu_config_t;

typedef enum {
  TUI_MENU_OK = 0,
  TUI_MENU_CANCELLED,
  TUI_MENU_MENU, /* Esc with enable_menu_key: caller should open its overlay */
  TUI_MENU_INTERRUPTED,
  TUI_MENU_TOO_SMALL,
  TUI_MENU_INVALID_ARG,
  TUI_MENU_NO_MEMORY,
} tui_menu_status_t;

typedef struct {
  tui_menu_status_t status;
  int selected_id; /* TUI_MENU_ID_NONE unless status == TUI_MENU_OK. */
  int selected_index;
} tui_menu_result_t;

/* If window == NULL, the menu owns its frame and recreates it on KEY_RESIZE.
 * If window != NULL, the menu draws into the caller's window. When
 * frame_height/frame_width are set, the caller's window is restored to those
 * requested dimensions on entry and on KEY_RESIZE. It returns
 * TUI_MENU_TOO_SMALL when the terminal cannot host the requested frame. */
APP_NODISCARD tui_menu_result_t tui_show_menu(tui_window_t *window,
                                              const tui_menu_config_t *config);
