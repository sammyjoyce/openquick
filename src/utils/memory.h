/*
 * Optional helpers for handling sensitive data (passwords, tokens, keys).
 *
 * The rest of the codebase uses plain malloc/free/strdup; call this helper
 * before releasing a buffer that actually held secret material and must not
 * linger in memory.
 */

#pragma once

#include <stddef.h>

// Zero memory in a way the compiler cannot optimise away. Safe to call with
// ptr == NULL or len == 0.
void app_secret_zero(void *ptr, size_t len);
