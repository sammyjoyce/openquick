/*
 * Styled --version output: program name + version, with build metadata
 * (commit, date, TUI availability) on additional lines. Plain when styling is
 * unavailable.
 */

#pragma once

#include <stdio.h>

#include "../../core/config.h"

void app_cli_render_version(const app_config_t *config, FILE *out,
                            const char *program_name);
