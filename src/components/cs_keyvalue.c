/*
 * cs_keyvalue — aligned key/value rows. See cs_keyvalue.h.
 */

#include "cs_keyvalue.h"

#include "../ui/text_layout.h"

void cs_keyvalue_render(const cs_keyvalue_t *kv, cs_surface_t *s) {
  if (!kv || !s || !kv->pairs || kv->count == 0) {
    return;
  }
  cs_role_t key_role =
      cs_role_or_default(kv->key_role, kv->key_role_set, CS_ROLE_MUTED);
  cs_role_t value_role =
      cs_role_or_default(kv->value_role, kv->value_role_set, CS_ROLE_TEXT);
  const char *separator = kv->separator ? kv->separator : "  ";

  int key_width = 0;
  for (size_t i = 0; i < kv->count; i++) {
    const char *key = kv->pairs[i].key ? kv->pairs[i].key : "";
    int w = app_text_width_utf8(key);
    if (w > key_width) {
      key_width = w;
    }
  }

  for (size_t i = 0; i < kv->count; i++) {
    const char *key = kv->pairs[i].key ? kv->pairs[i].key : "";
    const char *value = kv->pairs[i].value ? kv->pairs[i].value : "";

    cs_surface_set_role(s, key_role);
    cs_surface_write(s, key);
    cs_surface_reset(s);
    int pad = key_width - app_text_width_utf8(key);
    if (pad > 0) {
      cs_surface_repeat(s, " ", (size_t)pad);
    }

    cs_surface_write(s, separator);

    cs_surface_set_role(s, value_role);
    cs_surface_write(s, value);
    cs_surface_reset(s);
    cs_surface_newline(s);
  }
}
