/*
 * OSC 11 escape parsing + query/response I/O. See cli_term_osc11.h. The
 * detection policy (caching, env/TTY gating) lives in cli_term.c; this module
 * is only the wire format and the termios round-trip.
 */

#include "cli_term_osc11.h"

#include <string.h>

#ifndef _WIN32
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

bool app_cli_osc11_parse(const char *buf, size_t len, app_rgb_t *out_rgb) {
  if (!buf || !out_rgb) {
    return false;
  }
  const char *end = buf + len;
  // Anchored scan: jump to each ESC, then validate the "]11;" introducer rather
  // than testing every byte offset. The introducer + a minimal one-digit
  // channel ("rgb:0/0/0" + terminator) needs at least 8 bytes after ESC.
  const char *start = buf;
  while (start + 8 <= end) {
    const char *esc = memchr(start, '\x1b', (size_t)(end - start));
    if (!esc || esc + 8 > end) {
      break;
    }
    start = esc + 1;  // resume after this ESC on any failed validation

    if (esc[1] != ']' || esc[2] != '1' || esc[3] != '1' || esc[4] != ';') {
      continue;
    }

    const char *p = esc + 5;
    bool has_alpha = false;
    if (p + 4 <= end && strncmp(p, "rgb:", 4) == 0) {
      p += 4;
    } else if (p + 5 <= end && strncmp(p, "rgba:", 5) == 0) {
      p += 5;
      has_alpha = true;
    } else {
      continue;
    }

    unsigned val[3];
    unsigned max[3];
    bool valid = true;
    for (int i = 0; i < 3; i++) {
      if (!app_color_read_hex(&p, end, 4, &val[i], &max[i])) {
        valid = false;
        break;
      }
      if (i < 2) {
        if (p >= end || *p != '/') {
          valid = false;
          break;
        }
        p++;
      }
    }
    if (!valid) {
      continue;
    }

    if (has_alpha) {
      unsigned alpha_value;
      unsigned alpha_max;
      if (p >= end || *p != '/') {
        continue;
      }
      p++;
      if (!app_color_read_hex(&p, end, 4, &alpha_value, &alpha_max)) {
        continue;
      }
    }

    // Accept either terminator: BEL (\a) or ST (ESC-backslash). Both yield the
    // same RGB, so compute it once after the terminator check.
    bool terminated = (p < end && *p == '\a') ||
                      (p + 2 <= end && p[0] == '\x1b' && p[1] == '\\');
    if (!terminated) {
      continue;
    }
    out_rgb->r = app_color_channel_to_u8(val[0], max[0]);
    out_rgb->g = app_color_channel_to_u8(val[1], max[1]);
    out_rgb->b = app_color_channel_to_u8(val[2], max[2]);
    return true;
  }
  return false;
}

bool app_cli_osc11_query_fd(int fd, int timeout_ms, app_rgb_t *out_rgb) {
#ifndef _WIN32
  if (fd < 0 || !out_rgb) {
    return false;
  }
  struct termios saved;
  if (tcgetattr(fd, &saved) != 0) {
    return false;
  }
  struct termios raw = saved;
  raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  if (tcsetattr(fd, TCSANOW, &raw) != 0) {
    return false;
  }

  static const char query[] = "\x1b]11;?\x1b\\";
  bool ok = false;
  if (write(fd, query, sizeof(query) - 1) == (ssize_t)(sizeof(query) - 1)) {
    char buf[256];
    size_t len = 0;
    int remaining = timeout_ms <= 0 ? 100 : timeout_ms;
    while (remaining > 0 && len + 1 < sizeof(buf)) {
      struct pollfd pfd = {.fd = fd, .events = POLLIN};
      int slice = remaining > 20 ? 20 : remaining;
      int pr = poll(&pfd, 1, slice);
      remaining -= slice;
      if (pr <= 0 || !(pfd.revents & POLLIN)) {
        continue;
      }
      ssize_t n = read(fd, buf + len, sizeof(buf) - len - 1);
      if (n <= 0) {
        continue;
      }
      len += (size_t)n;
      // Stop once a terminator (BEL or ESC-backslash) has arrived.
      if (memchr(buf, '\a', len) != NULL ||
          memmem(buf, len, "\x1b\\", 2) != NULL) {
        break;
      }
    }
    ok = app_cli_osc11_parse(buf, len, out_rgb);
  }

  tcsetattr(fd, TCSANOW, &saved);
  return ok;
#else
  (void)fd;
  (void)timeout_ms;
  (void)out_rgb;
  return false;
#endif
}
