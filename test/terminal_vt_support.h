#ifndef TERMINAL_VT_SUPPORT_H
#define TERMINAL_VT_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} buffer_t;

typedef struct {
  int passed;
  int failed;
  int skipped;
} test_stats_t;

void buffer_free(buffer_t *buf);
bool buffer_append(buffer_t *buf, const void *data, size_t len);
const char *buffer_cstr(const buffer_t *buf);

int64_t monotonic_ms(void);
void print_tail(FILE *stream, const char *label, const char *text, size_t len,
                size_t max_len);
void test_pass(test_stats_t *stats, const char *name);
void test_skip(test_stats_t *stats, const char *name, const char *why);
int test_fail(test_stats_t *stats, const char *name, const char *fmt, ...);
bool contains_text(const char *haystack, const char *needle);
bool set_nonblocking(int fd);
void close_if_open(int *fd);
bool write_nonblocking_all(int fd, const void *data, size_t len,
                           int timeout_ms);

char **make_argv(const char *binary, const char *const *args, size_t argc);
void apply_child_env(bool interactive);

#endif
