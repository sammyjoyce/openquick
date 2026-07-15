#include "process.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
// Process streaming is currently POSIX-only.
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} quick_proc_buffer_t;

static void quick_proc_buffer_destroy(quick_proc_buffer_t *buffer) {
  if (!buffer) {
    return;
  }
  free(buffer->data);
  *buffer = (quick_proc_buffer_t){0};
}

static app_error quick_proc_buffer_reserve(quick_proc_buffer_t *buffer,
                                           size_t needed) {
  if (needed <= buffer->capacity) {
    return APP_SUCCESS;
  }
  size_t cap = buffer->capacity ? buffer->capacity : 256U;
  while (cap < needed) {
    if (cap > SIZE_MAX / 2U) {
      return APP_ERROR_OVERFLOW;
    }
    cap *= 2U;
  }
  char *grown = realloc(buffer->data, cap);
  if (!grown) {
    return APP_ERROR_MEMORY;
  }
  buffer->data = grown;
  buffer->capacity = cap;
  return APP_SUCCESS;
}

static app_error quick_proc_buffer_append(quick_proc_buffer_t *buffer,
                                          const char *data, size_t len) {
  if (len == 0) {
    return APP_SUCCESS;
  }
  if (buffer->size > SIZE_MAX - len - 1U) {
    return APP_ERROR_OVERFLOW;
  }
  app_error err = quick_proc_buffer_reserve(buffer, buffer->size + len + 1U);
  if (err != APP_SUCCESS) {
    return err;
  }
  memcpy(buffer->data + buffer->size, data, len);
  buffer->size += len;
  buffer->data[buffer->size] = '\0';
  return APP_SUCCESS;
}

static app_error quick_proc_buffer_take(quick_proc_buffer_t *buffer,
                                        char **out) {
  if (!out) {
    return APP_ERROR_INVALID_ARG;
  }
  app_error err = quick_proc_buffer_reserve(buffer, buffer->size + 1U);
  if (err != APP_SUCCESS) {
    return err;
  }
  buffer->data[buffer->size] = '\0';
  *out = buffer->data;
  buffer->data = NULL;
  buffer->size = 0;
  buffer->capacity = 0;
  return APP_SUCCESS;
}

void quick_process_result_destroy(quick_process_result_t *result) {
  if (!result) {
    return;
  }
  free(result->out);
  free(result->err);
  *result = (quick_process_result_t){0};
}

#ifndef _WIN32
static int quick_proc_close_fd(int fd) {
  if (fd >= 0) {
    return close(fd);
  }
  return 0;
}

static void quick_proc_close_pair(int fds[2]) {
  if (!fds) {
    return;
  }
  quick_proc_close_fd(fds[0]);
  quick_proc_close_fd(fds[1]);
  fds[0] = -1;
  fds[1] = -1;
}

static app_error quick_proc_set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return APP_ERROR_IO;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    return APP_ERROR_IO;
  }
  return APP_SUCCESS;
}

static app_error quick_write_fd_all(int fd, const char *text) {
  if (!text) {
    return APP_SUCCESS;
  }

  struct sigaction ignore_action = {0};
  struct sigaction old_action = {0};
  ignore_action.sa_handler = SIG_IGN;
  sigemptyset(&ignore_action.sa_mask);
  if (sigaction(SIGPIPE, &ignore_action, &old_action) != 0) {
    return APP_ERROR_IO;
  }

  app_error err = APP_SUCCESS;
  const char *p = text;
  size_t remaining = strlen(text);
  while (remaining > 0) {
    ssize_t n = write(fd, p, remaining);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EPIPE) {
        break;
      }
      err = APP_ERROR_IO;
      break;
    }
    if (n == 0) {
      err = APP_ERROR_IO;
      break;
    }
    p += n;
    remaining -= (size_t)n;
  }

  if (sigaction(SIGPIPE, &old_action, NULL) != 0 && err == APP_SUCCESS) {
    err = APP_ERROR_IO;
  }
  return err;
}

static app_error quick_line_buffer_feed(quick_proc_buffer_t *line,
                                        quick_stream_kind_t kind,
                                        const char *data, size_t len,
                                        quick_stream_cb cb, void *userdata) {
  if (!cb) {
    return APP_SUCCESS;
  }
  for (size_t i = 0; i < len; i++) {
    app_error err = quick_proc_buffer_append(line, &data[i], 1U);
    if (err != APP_SUCCESS) {
      return err;
    }
    if (data[i] == '\n') {
      cb(kind, line->data ? line->data : "", userdata);
      line->size = 0;
      if (line->data) {
        line->data[0] = '\0';
      }
    }
  }
  return APP_SUCCESS;
}

static void quick_line_buffer_flush(quick_proc_buffer_t *line,
                                    quick_stream_kind_t kind,
                                    quick_stream_cb cb, void *userdata) {
  if (cb && line && line->size > 0) {
    if (line->data) {
      line->data[line->size] = '\0';
      cb(kind, line->data, userdata);
    }
    line->size = 0;
    if (line->data) {
      line->data[0] = '\0';
    }
  }
}

static app_error quick_read_stream_fd(int fd, quick_stream_kind_t kind,
                                      quick_proc_buffer_t *capture,
                                      quick_proc_buffer_t *line,
                                      quick_stream_cb cb, void *userdata,
                                      bool *open) {
  char buf[4096];
  for (;;) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return APP_SUCCESS;
      }
      return APP_ERROR_IO;
    }
    if (n == 0) {
      *open = false;
      quick_line_buffer_flush(line, kind, cb, userdata);
      return APP_SUCCESS;
    }
    app_error err = quick_proc_buffer_append(capture, buf, (size_t)n);
    if (err != APP_SUCCESS) {
      return err;
    }
    err = quick_line_buffer_feed(line, kind, buf, (size_t)n, cb, userdata);
    if (err != APP_SUCCESS) {
      return err;
    }
  }
}
#endif

app_error quick_process_stream_cancelable(
    char *const argv[], const char *cwd, const char *stdin_text,
    quick_stream_cb on_line, void *userdata,
    const volatile sig_atomic_t *cancel_flag, quick_process_result_t *result) {
  if (!argv || !argv[0] || !result) {
    return APP_ERROR_INVALID_ARG;
  }
  *result = (quick_process_result_t){0};
#ifdef _WIN32
  (void)cwd;
  (void)stdin_text;
  (void)on_line;
  (void)userdata;
  (void)cancel_flag;
  return APP_ERROR_FEATURE_BASE;
#else
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  int in_pipe[2] = {-1, -1};
  app_error err = APP_SUCCESS;
  if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0 ||
      (stdin_text && pipe(in_pipe) != 0)) {
    quick_proc_close_pair(out_pipe);
    quick_proc_close_pair(err_pipe);
    quick_proc_close_pair(in_pipe);
    return APP_ERROR_IO;
  }

  pid_t pid = fork();
  if (pid < 0) {
    quick_proc_close_pair(out_pipe);
    quick_proc_close_pair(err_pipe);
    quick_proc_close_pair(in_pipe);
    return APP_ERROR_IO;
  }

  if (pid == 0) {
    close(out_pipe[0]);
    close(err_pipe[0]);
    if (stdin_text) {
      close(in_pipe[1]);
    }
    if ((stdin_text && dup2(in_pipe[0], STDIN_FILENO) < 0) ||
        dup2(out_pipe[1], STDOUT_FILENO) < 0 ||
        dup2(err_pipe[1], STDERR_FILENO) < 0) {
      _exit(126);
    }
    if (stdin_text) {
      close(in_pipe[0]);
    }
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (cwd && cwd[0] != '\0' && chdir(cwd) != 0) {
      _exit(126);
    }
    execvp(argv[0], argv);
    _exit(errno == ENOENT ? 127 : 126);
  }

  close(out_pipe[1]);
  out_pipe[1] = -1;
  close(err_pipe[1]);
  err_pipe[1] = -1;
  if (stdin_text) {
    close(in_pipe[0]);
    in_pipe[0] = -1;
    err = quick_write_fd_all(in_pipe[1], stdin_text);
    close(in_pipe[1]);
    in_pipe[1] = -1;
    if (err != APP_SUCCESS) {
      (void)kill(pid, SIGTERM);
    }
  }

  if (err == APP_SUCCESS) {
    err = quick_proc_set_nonblock(out_pipe[0]);
  }
  if (err == APP_SUCCESS) {
    err = quick_proc_set_nonblock(err_pipe[0]);
  }

  quick_proc_buffer_t out_buf = {0};
  quick_proc_buffer_t err_buf = {0};
  quick_proc_buffer_t out_line = {0};
  quick_proc_buffer_t err_line = {0};
  bool out_open = out_pipe[0] >= 0;
  bool err_open = err_pipe[0] >= 0;
  bool interrupted = false;
  bool term_sent = false;
  int kill_grace_ticks = 0;

  while ((out_open || err_open) && err == APP_SUCCESS) {
    if (cancel_flag && *cancel_flag) {
      interrupted = true;
      if (!term_sent) {
        (void)kill(pid, SIGTERM);
        term_sent = true;
        kill_grace_ticks = 4;
      } else if (kill_grace_ticks <= 0) {
        (void)kill(pid, SIGKILL);
      }
    }

    struct pollfd fds[2];
    nfds_t nfds = 0;
    if (out_open) {
      fds[nfds++] = (struct pollfd){.fd = out_pipe[0],
                                    .events = POLLIN | POLLHUP | POLLERR};
    }
    if (err_open) {
      fds[nfds++] = (struct pollfd){.fd = err_pipe[0],
                                    .events = POLLIN | POLLHUP | POLLERR};
    }
    int pr = poll(fds, nfds, 50);
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      err = APP_ERROR_IO;
      break;
    }
    if (pr == 0) {
      if (term_sent && kill_grace_ticks > 0) {
        kill_grace_ticks--;
      }
      continue;
    }

    nfds_t index = 0;
    if (out_open) {
      short revents = fds[index++].revents;
      if (revents & (POLLIN | POLLHUP | POLLERR)) {
        err = quick_read_stream_fd(out_pipe[0], QUICK_STREAM_STDOUT, &out_buf,
                                   &out_line, on_line, userdata, &out_open);
      }
    }
    if (err == APP_SUCCESS && err_open) {
      short revents = fds[index++].revents;
      if (revents & (POLLIN | POLLHUP | POLLERR)) {
        err = quick_read_stream_fd(err_pipe[0], QUICK_STREAM_STDERR, &err_buf,
                                   &err_line, on_line, userdata, &err_open);
      }
    }
  }

  if (out_pipe[0] >= 0) {
    close(out_pipe[0]);
  }
  if (err_pipe[0] >= 0) {
    close(err_pipe[0]);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      err = APP_ERROR_IO;
      break;
    }
  }

  if (WIFEXITED(status)) {
    result->exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result->exit_code = 128 + WTERMSIG(status);
  } else {
    result->exit_code = 1;
  }

  if (err == APP_SUCCESS) {
    err = quick_proc_buffer_take(&out_buf, &result->out);
  }
  if (err == APP_SUCCESS) {
    err = quick_proc_buffer_take(&err_buf, &result->err);
  }

  quick_proc_buffer_destroy(&out_buf);
  quick_proc_buffer_destroy(&err_buf);
  quick_proc_buffer_destroy(&out_line);
  quick_proc_buffer_destroy(&err_line);

  if (err != APP_SUCCESS) {
    quick_process_result_destroy(result);
    return err;
  }
  if (interrupted) {
    return APP_ERROR_INTERRUPTED;
  }
  return APP_SUCCESS;
#endif
}

app_error quick_process_stream(char *const argv[], const char *cwd,
                               const char *stdin_text, quick_stream_cb on_line,
                               void *userdata, quick_process_result_t *result) {
  return quick_process_stream_cancelable(argv, cwd, stdin_text, on_line,
                                         userdata, NULL, result);
}

app_error quick_process_capture_input(char *const argv[], const char *cwd,
                                      const char *stdin_text,
                                      quick_process_result_t *result) {
  return quick_process_stream(argv, cwd, stdin_text, NULL, NULL, result);
}

app_error quick_process_capture(char *const argv[], const char *cwd,
                                quick_process_result_t *result) {
  return quick_process_capture_input(argv, cwd, NULL, result);
}

app_error quick_process_run_inherit(char *const argv[], const char *cwd,
                                    int *exit_code) {
  if (!argv || !argv[0]) {
    return APP_ERROR_INVALID_ARG;
  }
#ifdef _WIN32
  (void)cwd;
  if (exit_code) {
    *exit_code = 1;
  }
  return APP_ERROR_FEATURE_BASE;
#else
  pid_t pid = fork();
  if (pid < 0) {
    return APP_ERROR_IO;
  }
  if (pid == 0) {
    if (cwd && cwd[0] != '\0' && chdir(cwd) != 0) {
      _exit(126);
    }
    execvp(argv[0], argv);
    _exit(errno == ENOENT ? 127 : 126);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return APP_ERROR_IO;
    }
  }
  int code = 1;
  if (WIFEXITED(status)) {
    code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    code = 128 + WTERMSIG(status);
  }
  if (exit_code) {
    *exit_code = code;
  }
  return APP_SUCCESS;
#endif
}
