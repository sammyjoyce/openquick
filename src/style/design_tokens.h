/*
 * Shared design palette.
 *
 * The single source of truth for the "amber on near-black" ayu/gruvbox look.
 * The ncurses TUI seeds its truecolor palette from these values (see
 * tui_define_rgb_palette in src/tui/tui.c) and the CLI styling layer maps its
 * semantic tokens onto them, so both surfaces share one identity instead of two
 * look-alike palettes.
 *
 * Values are 8-bit sRGB. app_design_chan_to_curses() converts each channel to
 * the 0..1000 scale ncurses init_color() expects.
 */

#pragma once

#include "color_math.h"

typedef struct app_design_palette {
  app_rgb_t near_black; /* primary background */
  app_rgb_t panel;      /* slightly raised surface */
  app_rgb_t panel_alt;  /* alternate surface */

  app_rgb_t amber; /* accent: titles, program name, numbers */
  app_rgb_t fg;    /* primary warm foreground */
  app_rgb_t muted; /* dim gray: borders, separators, hints */

  app_rgb_t green; /* success */
  app_rgb_t red;   /* error */
  app_rgb_t yellow;
  app_rgb_t blue;
} app_design_palette_t;

extern const app_design_palette_t APP_DESIGN_PALETTE;

// Convert an 8-bit channel (0..255) to the ncurses 0..1000 scale.
static inline int app_design_chan_to_curses(uint8_t v) {
  return (int)((v * 1000 + 127) / 255);
}
