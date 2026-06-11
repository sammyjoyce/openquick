#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "terminal_vt_support.h"

void buffer_free(buffer_t *buf) {
  if (!buf) {
    return;
  }
  free(buf->data);
  buf->data = NULL;
  buf->len = 0;
  buf->cap = 0;
}

static bool buffer_reserve(buffer_t *buf, size_t needed) {
  if (needed <= buf->cap) {
    return true;
  }

  size_t new_cap = buf->cap == 0 ? 256 : buf->cap;
  while (new_cap < needed) {
    if (new_cap > SIZE_MAX / 2) {
      return false;
    }
    new_cap *= 2;
  }

  char *next = realloc(buf->data, new_cap);
  if (!next) {
    return false;
  }
  buf->data = next;
  buf->cap = new_cap;
  return true;
}

bool buffer_append(buffer_t *buf, const void *data, size_t len) {
  if (len == 0) {
    return true;
  }
  if (buf->len > SIZE_MAX - len - 1) {
    return false;
  }
  const size_t needed = buf->len + len + 1;
  if (!buffer_reserve(buf, needed)) {
    return false;
  }
  memcpy(buf->data + buf->len, data, len);
  buf->len += len;
  buf->data[buf->len] = '\0';
  return true;
}

const char *buffer_cstr(const buffer_t *buf) {
  return buf && buf->data ? buf->data : "";
}

int64_t monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
  }

  const int saved_errno = errno != 0 ? errno : EINVAL;
  fprintf(stderr,
          "terminal-vt tests require clock_gettime(CLOCK_MONOTONIC): %s\n",
          strerror(saved_errno));
  exit(EXIT_FAILURE);
}

void print_tail(FILE *stream, const char *label, const char *text, size_t len,
                size_t max_len) {
  fprintf(stream, "%s", label);
  if (!text || len == 0) {
    fputs("<empty>\n", stream);
    return;
  }

  size_t offset = 0;
  if (len > max_len) {
    offset = len - max_len;
    fprintf(stream, "...<last %zu bytes>...\n", max_len);
  }
  fwrite(text + offset, 1, len - offset, stream);
  fputc('\n', stream);
}

void test_pass(test_stats_t *stats, const char *name) {
  stats->passed++;
  printf("ok %d - %s\n", stats->passed + stats->failed + stats->skipped, name);
}

void test_skip(test_stats_t *stats, const char *name, const char *why) {
  stats->skipped++;
  printf("ok %d - %s # SKIP %s\n",
         stats->passed + stats->failed + stats->skipped, name, why);
}

int test_fail(test_stats_t *stats, const char *name, const char *fmt, ...) {
  stats->failed++;
  printf("not ok %d - %s\n", stats->passed + stats->failed + stats->skipped,
         name);

  va_list args;
  va_start(args, fmt);
  fputs("  ", stderr);
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);
  va_end(args);
  return 1;
}

bool contains_text(const char *haystack, const char *needle) {
  return strstr(haystack ? haystack : "", needle) != NULL;
}

bool set_nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void close_if_open(int *fd) {
  if (*fd >= 0) {
    close(*fd);
    *fd = -1;
  }
}

bool write_nonblocking_all(int fd, const void *data, size_t len,
                           int timeout_ms) {
  const uint8_t *bytes = data;
  size_t written = 0;
  const int64_t deadline = monotonic_ms() + timeout_ms;

  while (written < len) {
    const ssize_t n = write(fd, bytes + written, len - written);
    if (n > 0) {
      written += (size_t)n;
      continue;
    }
    if (n == 0) {
      errno = EIO;
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      const int64_t now = monotonic_ms();
      if (now >= deadline) {
        return false;
      }

      int wait_ms = (int)(deadline - now);
      if (wait_ms > 50) {
        wait_ms = 50;
      }
      struct pollfd pfd = {.fd = fd, .events = POLLOUT};
      const int polled = poll(&pfd, 1, wait_ms);
      if (polled < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if (polled == 0) {
        continue;
      }
      if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

char **make_argv(const char *binary, const char *const *args, size_t argc) {
  char **argv = calloc(argc + 2, sizeof(char *));
  if (!argv) {
    return NULL;
  }

  argv[0] = (char *)binary;
  for (size_t i = 0; i < argc; i++) {
    argv[i + 1] = (char *)args[i];
  }
  argv[argc + 1] = NULL;
  return argv;
}

void apply_child_env(bool interactive) {
  if (interactive) {
    setenv("TERM", "xterm-256color", 1);
    unsetenv("NO_COLOR");
  } else {
    setenv("TERM", "dumb", 1);
    setenv("NO_COLOR", "1", 1);
  }
}
