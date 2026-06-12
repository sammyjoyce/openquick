#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "error.h"

const char *quick_json_skip_ws(const char *cursor);
app_error quick_json_read_string_alloc(const char **cursor, char **out);
app_error quick_json_read_bool_value(const char **cursor, bool *out);
app_error quick_json_read_string_or_null(const char **cursor, char **out);
app_error quick_json_skip_value(const char **cursor, int depth);
app_error quick_json_expect_char(const char **cursor, char ch);
app_error quick_json_finish(const char *cursor);
