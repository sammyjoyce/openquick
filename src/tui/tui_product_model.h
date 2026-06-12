#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "../core/ops.h"

#ifdef __cplusplus
extern "C" {
#endif

bool quick_tui_format_site_row(const quick_list_item_t *item, char *buffer,
                               size_t size);
bool quick_tui_validate_profile_field(const char *field, const char *value,
                                      char *message, size_t message_size);
const char *quick_tui_deploy_phase_label(quick_deploy_phase_t phase);
const char *quick_tui_doctor_status_icon(quick_doctor_status_t status);

#ifdef __cplusplus
}
#endif
