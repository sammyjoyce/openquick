#pragma once

#include <stdbool.h>

#include "../core/ops.h"
#include "tui_app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Outcome of the Get Started / onboarding entry menu. */
typedef enum {
  QUICK_ONBOARD_NONE = 0,
  QUICK_ONBOARD_LOCAL,        /* Try OpenQuick locally (recommended) */
  QUICK_ONBOARD_USE_EXISTING, /* Adopt the current/selected folder */
  QUICK_ONBOARD_CONNECT_HOST, /* Connect to an existing host */
  QUICK_ONBOARD_NEW_HOST,     /* Set up a new host */
  QUICK_ONBOARD_DASHBOARD,    /* Open the advanced dashboard */
  QUICK_ONBOARD_DISMISSED,    /* Esc/quit */
} quick_onboard_choice_t;

/* Show the Welcome / Get Started screen. Returns the selected path. Dismissible
 * with Esc (returns QUICK_ONBOARD_DISMISSED). */
quick_onboard_choice_t quick_tui_welcome(quick_tui_app_state_t *state,
                                         const quick_context_result_t *ctx);

/* Record and run a Welcome/Get started choice. This is the shared dispatcher
 * for startup onboarding and the dashboard entry point. */
bool quick_tui_onboarding_dispatch_choice(
    quick_tui_app_state_t *state, quick_onboard_choice_t choice,
    quick_onboarding_return_t return_destination);

/* Local quickstart wizard: create or adopt a project, preview it locally, and
 * show the resolved URL + next steps. adopt_default seeds "Use existing folder"
 * when the launcher already detected an adoptable folder. */
void quick_tui_local_quickstart(quick_tui_app_state_t *state,
                                bool adopt_default);

/* Run the onboarding launcher once at startup when appropriate, dispatching the
 * chosen outcome. Returns true if the user asked to open the advanced dashboard
 * (or the welcome screen is not applicable); false if the user exited. */
bool quick_tui_onboarding_launch(quick_tui_app_state_t *state);

#ifdef __cplusplus
}
#endif
