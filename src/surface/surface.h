/*
 * cs_surface — the neutral render surface every Curspan component draws to.
 *
 * A surface is a styled, role-aware drawing target that hides whether output
 * goes to a byte stream (CLI: SGR escapes, degrades by color profile) or an
 * ncurses window (TUI: color pairs). Components call the same small API on
 * either, so one component renders identically on a piped CLI, a truecolor
 * terminal, and inside a TUI screen — the surface owns capability detection and
 * color degradation; components stay pure layout + semantics.
 *
 *   cs_surface_t *s = cs_surface_stream_new(stdout, config, &theme);
 *   cs_surface_set_role(s, CS_ROLE_TITLE);
 *   cs_surface_write(s, "Hello");
 *   cs_surface_reset(s);
 *   cs_surface_newline(s);
 *   cs_surface_free(s);
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../style/cs_theme.h"

// Forward declarations keep this header free of <ncurses.h> and config.h, so it
// is safe to include from the umbrella header in any build configuration.
typedef struct app_config app_config_t;
typedef struct tui_window tui_window_t;

// Text attributes (bit mask). Bit positions mirror the CLI attribute bits so
// the stream backend maps them directly; the curses backend maps to A_BOLD etc.
typedef uint32_t cs_attr_t;
enum {
  CS_ATTR_NONE = 0,
  CS_ATTR_BOLD = 1u << 0,
  CS_ATTR_DIM = 1u << 1,
  CS_ATTR_UNDERLINE = 1u << 2,
  CS_ATTR_ITALIC = 1u << 3,
};

// What a surface can do. Components branch on these to degrade gracefully (e.g.
// ASCII glyphs when !unicode, no interactive prompts when !interactive).
typedef struct cs_caps {
  bool tty;                          // attached to a terminal
  bool color;                        // styling will actually be emitted
  bool unicode;                      // UTF-8 box/glyph drawing is safe
  bool interactive;                  // can read keys (TUI / TTY)
  app_cli_color_profile_id profile;  // none/16/256/truecolor
  int color_count;                   // palette size, when known
  size_t width;                      // usable columns
} cs_caps_t;

typedef struct cs_surface cs_surface_t;

// Create a stream surface over `stream` (e.g. stdout/stderr). `config` supplies
// the color policy (NO_COLOR/--plain/--json/...); pass NULL for capability-only
// detection. `theme` is copied; pass NULL for the default theme. Never returns
// NULL on success; returns NULL only on allocation failure.
cs_surface_t *cs_surface_stream_new(FILE *stream, const app_config_t *config,
                                    const cs_theme_t *theme);

#ifdef ENABLE_TUI
// Create a curses surface that draws into `window`. Requires tui_init() and
// tui_init_colors() to have run. `theme` is copied; pass NULL for the default.
cs_surface_t *cs_surface_curses_new(tui_window_t *window,
                                    const cs_theme_t *theme);
#endif

void cs_surface_free(cs_surface_t *s);

// Introspection.
cs_caps_t cs_surface_caps(const cs_surface_t *s);
size_t cs_surface_width(const cs_surface_t *s);
const cs_theme_t *cs_surface_theme(const cs_surface_t *s);

// Styling. set_role/set_role_bg/set_attr are additive until cs_surface_reset.
void cs_surface_set_role(cs_surface_t *s, cs_role_t role);
void cs_surface_set_role_bg(cs_surface_t *s, cs_role_t role);
void cs_surface_set_attr(cs_surface_t *s, cs_attr_t attrs);
void cs_surface_reset(cs_surface_t *s);

// Output. Writes are UTF-8. repeat() emits `glyph` `count` times (for rules and
// padding); newline() moves to the next row at the left margin; move()
// positions the cursor (curses only — a no-op on a stream, which flows
// top-to-bottom).
void cs_surface_write(cs_surface_t *s, const char *utf8);
void cs_surface_write_n(cs_surface_t *s, const char *utf8, size_t n);
void cs_surface_repeat(cs_surface_t *s, const char *glyph, size_t count);
void cs_surface_newline(cs_surface_t *s);
void cs_surface_move(cs_surface_t *s, int x, int y);

// Convenience: write `text` in `role` (with optional attrs) then reset.
void cs_surface_styled(cs_surface_t *s, cs_role_t role, cs_attr_t attrs,
                       const char *text);
