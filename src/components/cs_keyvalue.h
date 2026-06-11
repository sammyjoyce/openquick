/*
 * cs_keyvalue — an aligned list of key → value rows.
 *
 *   cs_keyvalue_render(&(cs_keyvalue_t){
 *       .pairs = (cs_keyvalue_pair_t[]){
 *           {"Application", "myapp"},
 *           {"Version",     "0.1.0"},
 *       },
 *       .count = 2,
 *   }, s);
 *
 * Keys are right-padded to the widest key so values line up. One row per pair,
 * each with a trailing newline. The `info` command's output is this shape.
 */

#pragma once

#include "../surface/surface.h"

typedef struct cs_keyvalue_pair {
  const char *key;
  const char *value;
} cs_keyvalue_pair_t;

typedef struct cs_keyvalue {
  const cs_keyvalue_pair_t *pairs;
  size_t count;
  cs_role_t key_role;     // default: CS_ROLE_MUTED
  cs_role_t value_role;   // default: CS_ROLE_TEXT
  bool key_role_set;      // true when `key_role` is intentional
  bool value_role_set;    // true when `value_role` is intentional
  const char *separator;  // between key column and value (NULL => "  ")
} cs_keyvalue_t;

void cs_keyvalue_render(const cs_keyvalue_t *kv, cs_surface_t *s);
