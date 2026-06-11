/*
 * Private contract between cli_term.c (backend-agnostic policy) and the two
 * interchangeable backends (cli_term_terminfo.c / cli_term_ansi.c). Exactly one
 * backend is compiled in, selected by the build system.
 */

#pragma once

#include "cli_term.h"

// Probe terminal capabilities for term->fd. Must set color_count and the
// supports_* flags; the terminfo backend also caches cap strings. Called only
// when the stream is a TTY and styling has not been ruled out.
void app_cli_backend_probe(app_cli_term_t *term);

// Release backend resources acquired in probe (e.g. del_curterm). Safe to call
// even if probe was never run.
void app_cli_backend_release(app_cli_term_t *term);

// Emit one attribute-enable sequence (e.g. bold). Only called when the
// corresponding supports_* flag is set.
void app_cli_backend_emit_attr(app_cli_term_t *term, app_cli_attr_bit attr);

// Emit an indexed foreground/background color. `index` is already resolved for
// term->profile (0..15 for ANSI16, 0..255 for ANSI256).
void app_cli_backend_emit_indexed(app_cli_term_t *term, bool background,
                                  uint16_t index);

// Emit the "reset all attributes" sequence.
void app_cli_backend_emit_reset(app_cli_term_t *term);

// Raw write helper backends use so they share stdout/stderr routing.
void app_cli_term_raw(app_cli_term_t *term, const char *s, size_t n);
