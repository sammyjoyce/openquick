/*
 * Optional helpers for handling sensitive data. See memory.h for the policy:
 * normal allocations belong in plain malloc/free; this helper is reserved for
 * buffers that genuinely held secret material.
 */

#include "memory.h"

void app_secret_zero(void *ptr, size_t len) {
  if (ptr == nullptr || len == 0) {
    return;
  }

#if defined(__STDC_LIB_EXT1__)
  memset_s(ptr, len, 0, len);
#elif defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__)
  explicit_bzero(ptr, len);
#else
  volatile unsigned char *p = ptr;
  while (len--) {
    *p++ = 0;
  }
  __asm__ __volatile__("" : : "r"(ptr) : "memory");
#endif
}
