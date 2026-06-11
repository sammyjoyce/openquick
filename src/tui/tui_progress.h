/*
 * tui_progress.h - lightweight progress bar API for the TUI layer.
 */
#pragma once

#include <stddef.h>

#include "../core/error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tui_progress tui_progress_t;

// Create a progress indicator with an optional title and maximum value.
// Returns NULL on allocation failure.
APP_NODISCARD tui_progress_t *tui_progress_create(const char *title, int max);

// Update the progress bar. Pass current value and optional status text.
void tui_progress_update(tui_progress_t *progress, int current,
                         const char *status);

// Destroy (free) the progress indicator and associated window.
void tui_progress_destroy(tui_progress_t *progress);

#ifdef __cplusplus
}  // extern "C"
#endif
