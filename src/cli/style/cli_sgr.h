/*
 * Applies a compiled style to a terminal: emit attributes + colors, write text,
 * reset. All functions degrade to plain writes when styling is disabled.
 */

#pragma once

#include "cli_term.h"
#include "cli_theme.h"

// Emit the style's attributes and colors (no reset).
void app_cli_style_begin(app_cli_term_t *term, const app_cli_style_t *style);

// Emit a full reset.
void app_cli_style_end(app_cli_term_t *term);

// begin(style) + write(text) + end(). When styling is off, just writes text.
void app_cli_write_styled(app_cli_term_t *term, const app_cli_style_t *style,
                          const char *text);
