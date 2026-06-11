/*
 * OSC 11 terminal-background I/O: the pure response parser and the
 * query/response round-trip over a terminal fd. Split out of cli_term.c so the
 * detection POLICY (when to probe, caching, env gates) stays separate from the
 * escape-parsing and termios/poll/read mechanics.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../../style/color_math.h"

// Parse an OSC 11 response into an RGB background color. Accepts
// "<ESC>]11;rgb:RRRR/GGGG/BBBB<ST>" with 1-4 hex digits per channel and either
// BEL or ESC-backslash terminators (and the rgba: variant). Returns false if no
// valid color is found. Pure function (no I/O); exposed for testing.
bool app_cli_osc11_parse(const char *buf, size_t len, app_rgb_t *out_rgb);

// Perform the OSC 11 query/response round-trip on an already-open terminal fd:
// put it in raw mode, write the query, poll up to timeout_ms for a reply, parse
// it, and restore the original termios. Returns false on timeout/error. Exposed
// so the round-trip can be tested over a PTY without a real /dev/tty.
bool app_cli_osc11_query_fd(int fd, int timeout_ms, app_rgb_t *out_rgb);
