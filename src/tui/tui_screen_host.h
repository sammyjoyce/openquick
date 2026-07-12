#pragma once

#include <stdbool.h>

#include "tui_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Connect to an existing OpenQuick host: collect + validate + verify + save a
 * profile. Returns true when a profile was saved (verified or kept anyway). */
bool quick_tui_screen_connect_host(quick_tui_app_state_t *state);

/* Set up a new host: review the plan, install quickd via the shared op with
 * phase progress and rollback, then save the profile on success. Returns true
 * on a successful install + saved profile. */
bool quick_tui_screen_new_host(quick_tui_app_state_t *state);

#ifdef __cplusplus
}
#endif
