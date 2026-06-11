/*
 * Pure UTF-8 text measurement/truncation/wrapping helpers shared by CLI and
 * TUI renderers. This module emits spans and never writes to FILE/curses.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef bool (*app_text_line_emit_fn)(void *user, const char *bytes,
                                      size_t byte_count, int columns);

int app_text_width_utf8(const char *text);
int app_text_width_utf8_n(const char *text, size_t byte_count);
size_t app_text_truncate_utf8_columns(const char *text, int max_columns,
                                      int *out_columns);
void app_text_wrap_utf8(const char *text, int width_columns,
                        int first_indent_columns, int next_indent_columns,
                        app_text_line_emit_fn emit, void *user);
