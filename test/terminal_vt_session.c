#include "terminal_vt_session.h"

static void ghostty_write_pty(GhosttyTerminal terminal, void *userdata,
                              const uint8_t *data, size_t len) {
  (void)terminal;
  vt_session_t *session = userdata;
  if (!session || session->io_failed || session->master_fd < 0) {
    return;
  }

  if (!write_nonblocking_all(session->master_fd, data, len, PTY_TIMEOUT_MS)) {
    session->io_failed = true;
    close_if_open(&session->master_fd);
  }
}

bool vt_session_start(vt_session_t *session, const char *binary,
                      const char *const *args, size_t argc, uint16_t cols,
                      uint16_t rows) {
  memset(session, 0, sizeof(*session));
  session->master_fd = -1;
  session->cols = cols;
  session->rows = rows;

  GhosttyTerminalOptions opts = {
      .cols = cols,
      .rows = rows,
      .max_scrollback = 1000,
  };
  if (ghostty_terminal_new(NULL, &session->terminal, opts) != GHOSTTY_SUCCESS) {
    fputs("failed to create Ghostty VT terminal\n", stderr);
    return false;
  }

  ghostty_terminal_set(session->terminal, GHOSTTY_TERMINAL_OPT_USERDATA,
                       session);
  ghostty_terminal_set(session->terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                       (const void *)ghostty_write_pty);

  struct winsize ws = {
      .ws_row = rows,
      .ws_col = cols,
      .ws_xpixel = (unsigned short)(cols * 8),
      .ws_ypixel = (unsigned short)(rows * 16),
  };

  char **argv = make_argv(binary, args, argc);
  if (!argv) {
    ghostty_terminal_free(session->terminal);
    session->terminal = NULL;
    return false;
  }

  const pid_t pid = forkpty(&session->master_fd, NULL, NULL, &ws);
  if (pid < 0) {
    perror("forkpty");
    free(argv);
    ghostty_terminal_free(session->terminal);
    session->terminal = NULL;
    return false;
  }

  if (pid == 0) {
    apply_child_env(true);
    execv(binary, argv);
    perror("execv");
    _exit(127);
  }

  session->pid = pid;
  free(argv);
  if (!set_nonblocking(session->master_fd)) {
    perror("fcntl O_NONBLOCK");
    vt_session_close(session);
    return false;
  }
  return true;
}

static void vt_session_note_exit(vt_session_t *session, int status) {
  session->child_exited = true;
  if (WIFEXITED(status)) {
    session->exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    session->exit_code = 128 + WTERMSIG(status);
  } else {
    session->exit_code = -1;
  }
}

static bool vt_session_try_reap(vt_session_t *session) {
  if (!session || session->child_exited || session->pid <= 0) {
    return session && session->child_exited;
  }

  while (true) {
    int status = 0;
    const pid_t waited = waitpid(session->pid, &status, WNOHANG);
    if (waited == session->pid) {
      vt_session_note_exit(session, status);
      return true;
    }
    if (waited == 0) {
      return false;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == ECHILD) {
      session->child_exited = true;
      session->exit_code = -1;
      return true;
    }
    return false;
  }
}

void vt_session_close(vt_session_t *session) {
  if (!session) {
    return;
  }

  if (!session->child_exited && session->pid > 0) {
    kill(session->pid, SIGTERM);
    for (int i = 0; i < 20; i++) {
      if (vt_session_try_reap(session)) {
        break;
      }
      usleep(25000);
    }
    if (!session->child_exited) {
      kill(session->pid, SIGKILL);
      while (true) {
        int status = 0;
        const pid_t waited = waitpid(session->pid, &status, 0);
        if (waited == session->pid) {
          vt_session_note_exit(session, status);
          break;
        }
        if (waited < 0 && errno == EINTR) {
          continue;
        }
        session->child_exited = true;
        session->exit_code = -1;
        break;
      }
    }
  }

  close_if_open(&session->master_fd);
  if (session->terminal) {
    ghostty_terminal_free(session->terminal);
    session->terminal = NULL;
  }
  buffer_free(&session->transcript);
}

void vt_drain(vt_session_t *session, int idle_ms) {
  if (!session) {
    return;
  }

  if (session->io_failed || session->master_fd < 0) {
    vt_session_try_reap(session);
    return;
  }

  const int64_t deadline = monotonic_ms() + idle_ms;
  while (true) {
    const int64_t now = monotonic_ms();
    int wait_ms = 0;
    if (idle_ms > 0) {
      wait_ms = now >= deadline ? 0 : (int)(deadline - now);
      if (wait_ms > 50) {
        wait_ms = 50;
      }
    }

    struct pollfd fd = {.fd = session->master_fd, .events = POLLIN};
    const int polled = poll(&fd, 1, wait_ms);
    if (polled < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (polled == 0) {
      if (idle_ms > 0 && monotonic_ms() < deadline) {
        continue;
      }
      break;
    }

    if (!(fd.revents & (POLLIN | POLLHUP | POLLERR))) {
      break;
    }

    bool read_any = false;
    while (true) {
      uint8_t chunk[READ_CHUNK];
      const ssize_t n = read(session->master_fd, chunk, sizeof(chunk));
      if (n > 0) {
        read_any = true;
        if (!buffer_append(&session->transcript, chunk, (size_t)n)) {
          fputs("terminal-vt tests: transcript append failed\n", stderr);
          session->io_failed = true;
          close_if_open(&session->master_fd);
          break;
        }
        ghostty_terminal_vt_write(session->terminal, chunk, (size_t)n);
        continue;
      }
      if (n == 0) {
        close_if_open(&session->master_fd);
        break;
      }
      if (errno == EIO) {
        close_if_open(&session->master_fd);
        break;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        break;
      }
      close_if_open(&session->master_fd);
      break;
    }

    if (session->io_failed || session->master_fd < 0) {
      break;
    }
    if (!read_any && idle_ms == 0) {
      break;
    }
  }

  vt_session_try_reap(session);
}

static char *vt_snapshot_text(vt_session_t *session) {
  GhosttyFormatterTerminalOptions opts =
      GHOSTTY_INIT_SIZED(GhosttyFormatterTerminalOptions);
  opts.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
  opts.trim = true;

  GhosttyFormatter formatter = NULL;
  if (ghostty_formatter_terminal_new(NULL, &formatter, session->terminal,
                                     opts) != GHOSTTY_SUCCESS) {
    return NULL;
  }

  uint8_t *raw = NULL;
  size_t len = 0;
  if (ghostty_formatter_format_alloc(formatter, NULL, &raw, &len) !=
      GHOSTTY_SUCCESS) {
    ghostty_formatter_free(formatter);
    return NULL;
  }

  char *text = calloc(len + 1, 1);
  if (text) {
    memcpy(text, raw, len);
  }
  ghostty_free(NULL, raw, len);
  ghostty_formatter_free(formatter);
  return text;
}

bool vt_expect_text(vt_session_t *session, const char *needle, int timeout_ms,
                    char **last_snapshot) {
  const int64_t deadline = monotonic_ms() + timeout_ms;
  while (monotonic_ms() <= deadline) {
    vt_drain(session, 50);
    char *snapshot = vt_snapshot_text(session);
    if (snapshot && contains_text(snapshot, needle)) {
      if (last_snapshot) {
        free(*last_snapshot);
        *last_snapshot = snapshot;
      } else {
        free(snapshot);
      }
      return true;
    }
    if (last_snapshot) {
      free(*last_snapshot);
      *last_snapshot = snapshot;
    } else {
      free(snapshot);
    }
    if (session->io_failed) {
      break;
    }
    if (session->child_exited) {
      break;
    }
  }
  return false;
}

bool vt_send(vt_session_t *session, const char *bytes) {
  if (session->io_failed || session->master_fd < 0) {
    return false;
  }
  const size_t len = strlen(bytes);
  if (!write_nonblocking_all(session->master_fd, bytes, len, PTY_TIMEOUT_MS)) {
    session->io_failed = true;
    return false;
  }
  vt_drain(session, 50);
  return true;
}

bool vt_resize(vt_session_t *session, uint16_t cols, uint16_t rows) {
  struct winsize ws = {
      .ws_row = rows,
      .ws_col = cols,
      .ws_xpixel = (unsigned short)(cols * 8),
      .ws_ypixel = (unsigned short)(rows * 16),
  };
  if (ghostty_terminal_resize(session->terminal, cols, rows, 8, 16) !=
      GHOSTTY_SUCCESS) {
    return false;
  }
  if (ioctl(session->master_fd, TIOCSWINSZ, &ws) != 0) {
    const int saved_errno = errno;
    if (ghostty_terminal_resize(session->terminal, session->cols, session->rows,
                                8, 16) != GHOSTTY_SUCCESS) {
      fputs("failed to roll back Ghostty terminal size\n", stderr);
    }
    errno = saved_errno;
    return false;
  }
  kill(session->pid, SIGWINCH);
  session->cols = cols;
  session->rows = rows;
  return true;
}

int vt_wait_for_exit(vt_session_t *session, int timeout_ms) {
  const int64_t deadline = monotonic_ms() + timeout_ms;
  while (monotonic_ms() <= deadline) {
    vt_drain(session, 50);
    if (session->child_exited) {
      return session->exit_code;
    }
    if (session->io_failed) {
      return -1;
    }
  }
  return -1;
}
