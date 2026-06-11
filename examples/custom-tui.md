# Example: Composing Components on a Surface

Curspan components draw through one neutral [`cs_surface`](../docs/COMPONENTS.md#the-surface).
A surface targets either a **CLI byte stream** or a **TUI ncurses window**, so the
same component calls render in both places — the surface owns capability detection
and color degradation. This example composes the catalog on each backend, then
shows where to reach for the interactive TUI primitives.

- [On a CLI stream](#1-on-a-cli-stream)
- [On a TUI window](#2-on-a-tui-window)
- [Interactive primitives](#3-interactive-primitives)
- [Theming](#4-theming)
- [Practices](#5-practices)

## 1. On a CLI stream

A stream surface needs no TUI build (it works with `-Denable-tui=false`). Build
one over `stdout`, render components, free it. The `app_config_t` carries the
color policy (`NO_COLOR` / `--plain` / `--json` …), so the surface respects it
automatically.

```c
#include "curspan.h"

static app_error show_report(const app_config_t *config) {
    cs_surface_t *s = cs_surface_stream_new(stdout, config, NULL); // NULL => default theme
    if (!s) {
        return APP_ERROR_MEMORY;
    }

    cs_heading_render(&(cs_heading_t){.text = "Build report", .underline = true}, s);

    cs_table_column_t cols[] = {
        {.header = "Step"},
        {.header = "Result", .align = CS_ALIGN_RIGHT},
    };
    const char *cells[] = {
        "compile", "ok",
        "link",    "ok",
        "tests",   "25 passed",
    };
    cs_table_render(&(cs_table_t){.columns = cols, .column_count = 2,
                                  .cells = cells, .row_count = 3,
                                  .header = true, .width = 48}, s);

    cs_surface_newline(s);
    cs_note_render(&(cs_note_t){.variant = CS_NOTE_SUCCESS,
                                .title = "Done",
                                .body = "Everything is green.",
                                .width = 48}, s);

    cs_surface_free(s);   // borrows stdout — does not close it
    return APP_SUCCESS;
}
```

The same code degrades on its own: amber on a truecolor terminal, the nearest
palette index on a 256/16-color one, and plain escape-free text under a pipe.

## 2. On a TUI window

The interactive ncurses interface is compiled by default (which defines
`ENABLE_TUI`); pass `-Denable-tui=false` for a CLI/headless-only build. Call
`tui_init()` first (it sets up the color pairs) and `tui_cleanup()` before you
return on **every** path. A curses surface wraps a `tui_window_t`, so the same
components draw inside the window:

```c
#ifdef ENABLE_TUI
static app_error show_report_tui(void) {
    app_error err = tui_init();   // also initializes color pairs
    if (err != APP_SUCCESS) {
        return err;
    }

    tui_window_t *win = tui_create_window(tui_get_max_y() - 2, tui_get_max_x() - 2, 1, 1);
    if (!win) {
        tui_cleanup();
        return APP_ERROR_MEMORY;
    }
    tui_draw_border(win);
    tui_set_window_title(win, "Build report");

    cs_surface_t *s = cs_surface_curses_new(win, NULL);  // NULL => default theme
    if (!s) {
        tui_destroy_window(win);
        tui_cleanup();
        return APP_ERROR_MEMORY;
    }

    cs_surface_move(s, 2, 1);   // position inside the border (curses only)
    cs_heading_render(&(cs_heading_t){.text = "Build report"}, s);

    cs_keyvalue_pair_t rows[] = {{"compile", "ok"}, {"link", "ok"}, {"tests", "ok"}};
    cs_surface_move(s, 2, 3);
    cs_keyvalue_render(&(cs_keyvalue_t){.pairs = rows, .count = 3}, s);

    tui_refresh_window(win);
    tui_get_char();             // wait for a key

    cs_surface_free(s);         // borrows the window — does not destroy it
    tui_destroy_window(win);
    tui_cleanup();
    return APP_SUCCESS;
}
#endif
```

`cs_surface_move(x, y)` positions the cursor on a curses surface and is a no-op on
a stream (which flows top to bottom), so a component that avoids `move` works on
both. Wire the handler to a command with `.requires_terminal = true`, and guard
the TUI call so a non-TUI build still links:

```c
app_error app_cmd_report(const app_config_t *config, int argc, char *const argv[]) {
    (void)argc; (void)argv;
#ifdef ENABLE_TUI
    if (config && /* a TTY is available */ true) {
        return show_report_tui();
    }
#endif
    return show_report(config);  // fall back to the stream surface
}
```

See [adding-a-command.md](adding-a-command.md) for the full registration steps.

## 3. Interactive primitives

A surface is for **drawing**. For **input** — menus, prompts, confirmations —
reach for the `tui_` primitives directly. They manage the event loop; render
components around them.

### Dialogs

```c
tui_show_message("Success", "Operation completed.");

if (tui_confirm("Delete File", "Are you sure?")) {
    // tui_confirm returns bool
}

char username[256] = {0};
if (tui_input_dialog("Login", "Enter username:", username, sizeof(username)) == APP_SUCCESS) {
    // username is filled in
}
```

### Interactive progress

`cs_progress` renders a bar **once** (great inside other output). For a
long-running task that owns the screen, use the interactive `tui_progress`:

```c
tui_progress_t *progress = tui_progress_create("Processing", 100);
for (int i = 0; i <= 100; i++) {
    char status[64];
    snprintf(status, sizeof(status), "Item %d of 100", i);
    tui_progress_update(progress, i, status);
    napms(50);  // stand-in for real work
}
tui_progress_destroy(progress);
```

### Menu

The menu is the one reusable primitive also shipped as a library
(`tui-menu-lib`) and covered by the
[TUI menu contract](../docs/CONTRACTS.md#tui-menu-contract). All pointers in the
config must outlive the call; the menu copies nothing.

```c
enum { MENU_OPTION_ONE = 1, MENU_OPTION_TWO, MENU_EXIT };

const tui_menu_item_t options[] = {
    {.label = "Option &1", .description = "First option",  .id = MENU_OPTION_ONE},
    {.label = "Option &2", .description = "Second option", .id = MENU_OPTION_TWO},
    {.label = "&Disabled", .description = "Unavailable",   .id = 3, .disabled = true},
    {.label = "E&xit",     .description = "Leave the menu", .id = MENU_EXIT},
};

tui_menu_result_t result = tui_show_menu(
    NULL,  // NULL => the menu owns its frame and handles resize
    &(tui_menu_config_t){
        .title = "Select Option",
        .items = options,
        .item_count = (int)(sizeof(options) / sizeof(options[0])),
        .default_index = 0,
        .enable_search = true,
    });

if (result.status == TUI_MENU_OK) {
    switch (result.selected_id) {
        case MENU_OPTION_ONE: /* ... */ break;
        case MENU_OPTION_TWO: /* ... */ break;
        case MENU_EXIT:       /* ... */ break;
    }
}
```

`&` in a label marks the mnemonic key (`&&` is a literal `&`). `result.status` is one of `TUI_MENU_OK`, `TUI_MENU_CANCELLED`, `TUI_MENU_INTERRUPTED`, `TUI_MENU_TOO_SMALL`, `TUI_MENU_INVALID_ARG`, or `TUI_MENU_NO_MEMORY`.

## 4. Theming

Components never name a raw color — they style themselves through theme roles, and
the surface degrades each role for the terminal. Pick or override a theme and pass
it to the surface (it is copied):

```c
cs_theme_t theme = cs_theme_default();
cs_theme_set_role_spec(&theme, CS_ROLE_ACCENT, "#7dd3fc");   // re-accent
cs_surface_t *s = cs_surface_stream_new(stdout, config, &theme);
```

`cs_theme_by_name("mono", &theme)` switches to the grayscale theme; both
front-ends and every component share the same roles. Full detail in
[THEMING.md](../docs/THEMING.md).

## 5. Practices

- **Pair every `tui_init()` with `tui_cleanup()`**, including on every error path, or the terminal is left in raw mode.
- **Handle a failed init.** `tui_init()` returns an `app_error`; fall back to the stream surface rather than aborting:

  ```c
  if (tui_init() != APP_SUCCESS) {
      return show_report(config);  // plain, escape-free output
  }
  ```

- **Respect interrupts.** Check `tui_interrupted()` in long loops and exit cleanly.
- **Branch on capability, not on the build.** `cs_surface_caps(s)` reports
  `unicode`, `color`, `width`, and `interactive`; components already use it (e.g.
  ASCII glyph fallbacks), so a composed screen degrades without `#ifdef`s.
- **Test both builds.** Confirm `zig build` (default TUI) and `zig build -Denable-tui=false` both compile and behave. Drive TUI screens with the scenario harness in [TESTING.md](../docs/TESTING.md).

## See also

- [Components](../docs/COMPONENTS.md) - the catalog and the full surface API
- [Theming](../docs/THEMING.md) - roles, named themes, and color degradation
- [Public Contracts](../docs/CONTRACTS.md) - the stable framework and menu surfaces
- [Architecture Overview](../docs/ARCHITECTURE.md) - where the surface and `tui/` sit
