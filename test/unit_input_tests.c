/*
 * Unit tests for bounded input helpers.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "../src/core/types.h"
#include "../src/io/input.h"
#include "unit_support.h"

// Create the fixture readable/writable by the owner only. A plain fopen would
// create it world-writable (0666 & ~umask), which is needlessly permissive for
// a throwaway test file.
static FILE *open_owner_only(const char *path) {
#ifdef _WIN32
  return fopen(path, "wb");
#else
  const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    return NULL;
  }
  FILE *file = fdopen(fd, "wb");
  if (!file) {
    close(fd);
  }
  return file;
#endif
}

static bool write_repeated_file(const char *path, size_t size) {
  FILE *file = open_owner_only(path);
  if (!file) {
    return false;
  }

  char block[4096];
  memset(block, 'x', sizeof(block));
  while (size > 0) {
    const size_t chunk = size < sizeof(block) ? size : sizeof(block);
    if (fwrite(block, 1, chunk, file) != chunk) {
      fclose(file);
      return false;
    }
    size -= chunk;
  }

  return fclose(file) == 0;
}

static bool test_read_file_accepts_max_payload(void) {
  const char *path = ".zig-cache/unit-input-max-ok.tmp";
  const size_t payload_size = INPUT_MAX_SIZE - 1U;
  if (!write_repeated_file(path, payload_size)) {
    return false;
  }

  errno = 0;
  char *data = app_read_input_from_file(path);
  const bool ok = data && strlen(data) == payload_size && errno == 0;
  free(data);
  (void)remove(path);
  return ok;
}

static bool test_read_file_rejects_oversized_payload(void) {
  const char *path = ".zig-cache/unit-input-too-large.tmp";
  if (!write_repeated_file(path, INPUT_MAX_SIZE)) {
    return false;
  }

  errno = 0;
  char *data = app_read_input_from_file(path);
  const bool ok = data == NULL && errno == E2BIG;
  free(data);
  (void)remove(path);
  return ok;
}

void run_input_unit_tests(unit_stats_t *stats) {
  unit_record(stats, test_read_file_accepts_max_payload(),
              "input file accepts documented maximum payload");
  unit_record(stats, test_read_file_rejects_oversized_payload(),
              "input file rejects oversized payload");
}
