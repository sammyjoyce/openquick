/*
 * Shared design palette values. See design_tokens.h.
 *
 * Single source of truth for the amber/fg/muted/green tones: the ncurses TUI
 * seeds its truecolor palette directly from these triples
 * (tui_define_rgb_palette in src/tui/tui.c) and the CLI styling layer maps its
 * semantic tokens onto the same family. The surface and red/yellow/blue accents
 * extend it for CLI use.
 */

#include "design_tokens.h"

const app_design_palette_t APP_DESIGN_PALETTE = {
    .near_black = {15, 14, 13},
    .panel = {28, 26, 23},
    .panel_alt = {36, 32, 27},

    .amber = {200, 175, 130},
    .fg = {210, 205, 195},
    .muted = {90, 85, 80},

    .green = {143, 181, 115}, /* #8FB573 */
    .red = {234, 105, 98},
    .yellow = {216, 166, 87},
    .blue = {125, 169, 199},
};
