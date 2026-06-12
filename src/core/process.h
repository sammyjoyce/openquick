#pragma once

#include <signal.h>
#include <stdbool.h>

#include "error.h"

// Captured child-process output. out/err are always NUL-terminated on a
// successful launch, even when the corresponding stream is empty.
typedef struct {
  char *out;
  char *err;
  int exit_code;
} quick_process_result_t;

typedef enum {
  QUICK_STREAM_STDOUT = 0,
  QUICK_STREAM_STDERR = 1,
} quick_stream_kind_t;

// Called with one complete line as soon as it is available. The line includes
// its trailing '\n' when the child emitted one; a final partial line is flushed
// without adding a newline.
typedef void (*quick_stream_cb)(quick_stream_kind_t kind, const char *line,
                                void *userdata);

void quick_process_result_destroy(quick_process_result_t *result);

app_error quick_process_stream(char *const argv[], const char *cwd,
                               const char *stdin_text,
                               quick_stream_cb on_line, void *userdata,
                               quick_process_result_t *result);

app_error quick_process_stream_cancelable(
    char *const argv[], const char *cwd, const char *stdin_text,
    quick_stream_cb on_line, void *userdata,
    const volatile sig_atomic_t *cancel_flag, quick_process_result_t *result);

app_error quick_process_capture(char *const argv[], const char *cwd,
                                quick_process_result_t *result);
app_error quick_process_capture_input(char *const argv[], const char *cwd,
                                      const char *stdin_text,
                                      quick_process_result_t *result);
app_error quick_process_run_inherit(char *const argv[], const char *cwd,
                                    int *exit_code);
