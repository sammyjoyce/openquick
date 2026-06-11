/*
 * Terminal color handling for the application.
 *
 * Provides ANSI color codes with intelligent terminal detection to enhance
 * readability. Colors are automatically disabled for non-TTY output, when
 * NO_COLOR is set, or when explicitly disabled via config. This ensures clean
 * output in logs and pipes while providing helpful visual cues in interactive
 * terminals.
 */

#pragma once

#include <stdbool.h>

// ANSI color codes for semantic highlighting of output.
// Red for errors, green for success, yellow for warnings, blue for info.
// These standard codes work across most modern terminals and are ignored
// by terminals that don't support them, ensuring graceful degradation.
// Prefixed with APP_ to avoid conflicts with ncurses color definitions.
#define APP_COLOR_RED "\033[0;31m"
#define APP_COLOR_GREEN "\033[0;32m"
#define APP_COLOR_YELLOW "\033[1;33m"
#define APP_COLOR_BLUE "\033[0;34m"
#define APP_COLOR_BOLD "\033[1m"
#define APP_COLOR_RESET "\033[0m"

// Forward declaration to access color preferences from config.
// This avoids circular dependencies while respecting user preferences.
typedef struct app_config app_config_t;

// Check if colors should be used based on terminal capabilities and config.
// Returns false for: non-TTY output, NO_COLOR env var, --no-color flag, or
// dumb terminals. This multi-layer check ensures colors only appear when
// helpful.
bool app_use_colors(const app_config_t *config);

// Tri-state outcome of the color-forcing environment variables.
typedef enum {
  APP_COLOR_FORCE_AUTO = 0,  // no override; fall back to TTY detection
  APP_COLOR_FORCE_ON,        // force color on, even off a TTY
  APP_COLOR_FORCE_OFF,       // force color off (a hard disable, like NO_COLOR)
} app_color_force_t;

// Resolve FORCE_COLOR / CLICOLOR_FORCE / CLICOLOR into a single tri-state,
// following the widely-adopted conventions (chalk's FORCE_COLOR and the
// bixense CLICOLOR spec):
//   - FORCE_COLOR=0 or =false  -> OFF   (explicit disable, wins like NO_COLOR)
//   - FORCE_COLOR set otherwise -> ON   (including empty, 1, 2, 3, true)
//   - CLICOLOR_FORCE set, not 0 -> ON
//   - CLICOLOR=0               -> OFF
//   - none of the above        -> AUTO
// NO_COLOR is handled separately by each caller and takes top precedence over
// this result. FORCE_COLOR is the strongest signal here, then CLICOLOR_FORCE,
// then CLICOLOR.
app_color_force_t app_color_env_force(void);
