/*
 * Shared color math for the design system.
 *
 * Pure value-level helpers: an 8-bit RGB triple, perceptual luminance, and
 * downsampling from truecolor to the xterm-256 and ANSI-16 palettes. No
 * terminal I/O, no config, no allocation. Used by both the CLI styling layer
 * and (for palette seeding) the ncurses TUI.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct app_rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} app_rgb_t;

// Color capability of an output stream, from most to least capable.
typedef enum app_cli_color_profile_id {
  APP_CLI_COLOR_PROFILE_NONE = 0,
  APP_CLI_COLOR_PROFILE_ANSI16,
  APP_CLI_COLOR_PROFILE_ANSI256,
  APP_CLI_COLOR_PROFILE_TRUECOLOR,
} app_cli_color_profile_id;

// Perceptual luminance on a 0..255 scale (Rec. 601 weights).
int app_color_luma8(app_rgb_t rgb);

// Heuristic: is this color light enough to read dark text against it? Used to
// classify a terminal background reported via OSC 11.
bool app_color_is_light(app_rgb_t rgb);

// Nearest xterm-256 index (0..255): considers both the 6x6x6 color cube and
// the grayscale ramp and returns whichever is closer.
uint8_t app_color_rgb_to_xterm256(app_rgb_t rgb);

// Nearest standard ANSI-16 index (0..15). Theme tokens should prefer their own
// semantic hint; this is the fallback for arbitrary user-supplied RGB.
uint8_t app_color_rgb_to_ansi16(app_rgb_t rgb);

// Convert an OSC 11 channel (value/max, e.g. 0xffff/0xffff) to 8-bit.
uint8_t app_color_channel_to_u8(unsigned value, unsigned max);

// Read up to `max_digits` (1..) hexadecimal digits starting at *p (bounded by
// `end`), accumulating into *value and reporting the largest value a field of
// that many digits could hold via *field_max (e.g. two digits => 0xff).
// Advances *p past the consumed digits. Returns false if no hex digit was read.
// Shared by the OSC 11 response parser and the #RRGGBB / channel theme color
// parser so both scale through app_color_channel_to_u8 consistently.
bool app_color_read_hex(const char **p, const char *end, int max_digits,
                        unsigned *value, unsigned *field_max);
