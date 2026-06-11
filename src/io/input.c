/*
 * Input handling implementation.
 */

#include "input.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define app_fileno _fileno
#define app_fstat _fstat
#define app_stat_t struct _stat
#define app_is_regular_file(mode) (((mode) & _S_IFMT) == _S_IFREG)
#else
#include <sys/stat.h>
#include <unistd.h>
#define app_fileno fileno
#define app_fstat fstat
#define app_stat_t struct stat
#define app_is_regular_file(mode) S_ISREG(mode)
#endif

#include "../utils/logging.h"

// Compile-time assertions
static_assert(INPUT_MAX_SIZE >= 512 * 1024, "Input max size too small");
static_assert(INPUT_BUFFER_INITIAL_SIZE >= 8192,
              "Initial input buffer too small");

char *app_read_input_from_stdin(void) {
  if (stdin == NULL) {
    LOG_ERROR("stdin is NULL");
    return nullptr;
  }

  // Optimize initial allocation by checking if stdin is a regular file
  size_t capacity = INPUT_BUFFER_INITIAL_SIZE;
  app_stat_t st;
  if (app_fstat(app_fileno(stdin), &st) == 0 &&
      app_is_regular_file(st.st_mode) && st.st_size > 0) {
    const size_t file_size = (size_t)st.st_size;
    capacity = file_size < INPUT_MAX_SIZE - 1 ? file_size + 1 : INPUT_MAX_SIZE;
  }

  if (capacity == 0 || capacity > INPUT_MAX_SIZE) {
    LOG_ERROR("Invalid initial buffer capacity");
    return nullptr;
  }

  char *buffer = malloc(capacity);
  if (buffer == NULL) {
    errno = ENOMEM;
    return nullptr;
  }

  size_t total_size = 0;
  while (1) {
    if (total_size >= INPUT_MAX_SIZE - 1) {
      LOG_ERROR("Input exceeds maximum size of %zu bytes",
                (size_t)INPUT_MAX_SIZE - 1);
      free(buffer);
      errno = E2BIG;
      return nullptr;
    }

    size_t remaining = capacity - total_size - 1;
    if (remaining < INPUT_BUFFER_READ_CHUNK_SIZE) {
      size_t new_capacity = capacity * 2;
      if (new_capacity > INPUT_MAX_SIZE) {
        new_capacity = INPUT_MAX_SIZE;
      }
      if (new_capacity <= capacity) {
        LOG_ERROR("Input exceeds maximum size of %zu bytes",
                  (size_t)INPUT_MAX_SIZE - 1);
        free(buffer);
        errno = E2BIG;
        return nullptr;
      }

      char *new_buffer = realloc(buffer, new_capacity);
      if (new_buffer == NULL) {
        free(buffer);
        errno = ENOMEM;
        return nullptr;
      }
      buffer = new_buffer;
      capacity = new_capacity;
      remaining = capacity - total_size - 1;
    }

    size_t bytes_to_read = remaining < INPUT_BUFFER_READ_CHUNK_SIZE
                               ? remaining
                               : INPUT_BUFFER_READ_CHUNK_SIZE;
    if (bytes_to_read == 0) {
      LOG_ERROR("Input exceeds maximum size of %zu bytes",
                (size_t)INPUT_MAX_SIZE - 1);
      free(buffer);
      errno = E2BIG;
      return nullptr;
    }
    size_t bytes_read = fread(buffer + total_size, 1, bytes_to_read, stdin);

    if (bytes_read == 0) {
      if (feof(stdin)) {
        break;  // End of file reached
      }
      if (ferror(stdin)) {
        LOG_ERROR("Error reading from stdin: %s", strerror(errno));
        free(buffer);
        return nullptr;
      }
    }
    if (bytes_read > bytes_to_read ||
        bytes_read > (INPUT_MAX_SIZE - 1) - total_size) {
      LOG_ERROR("Input read exceeded expected bounds");
      free(buffer);
      errno = EIO;
      return nullptr;
    }

    total_size += bytes_read;
  }

  buffer[total_size] = '\0';

  // Shrink buffer to actual size to save memory
  if (total_size + 1 < capacity) {
    char *final_buffer = realloc(buffer, total_size + 1);
    if (final_buffer != NULL) {
      buffer = final_buffer;
    }
  }

  LOG_DEBUG("Read %zu bytes from stdin", total_size);
  return buffer;
}

char *app_read_input_from_file(const char *filename) {
  if (filename == NULL) {
    LOG_ERROR("filename is NULL");
    return nullptr;
  }

  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    LOG_ERROR("Failed to open file %s: %s", filename, strerror(errno));
    return nullptr;
  }

  // Get file size
  app_stat_t st;
  if (app_fstat(app_fileno(file), &st) != 0) {
    LOG_ERROR("Failed to stat file %s: %s", filename, strerror(errno));
    fclose(file);
    return nullptr;
  }

  if (st.st_size < 0 ||
      (uintmax_t)st.st_size > (uintmax_t)INPUT_MAX_SIZE - 1U) {
    LOG_ERROR("File %s exceeds maximum size of %zu bytes", filename,
              (size_t)INPUT_MAX_SIZE - 1);
    fclose(file);
    errno = E2BIG;
    return nullptr;
  }

  size_t file_size = st.st_size;
  char *buffer = malloc(file_size + 1);
  if (buffer == NULL) {
    fclose(file);
    errno = ENOMEM;
    return nullptr;
  }

  size_t bytes_read = fread(buffer, 1, file_size, file);
  if (bytes_read != file_size) {
    LOG_ERROR("Failed to read entire file %s", filename);
    free(buffer);
    fclose(file);
    return nullptr;
  }

  buffer[file_size] = '\0';
  fclose(file);

  LOG_DEBUG("Read %zu bytes from file %s", file_size, filename);
  return buffer;
}
