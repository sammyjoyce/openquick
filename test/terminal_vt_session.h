#ifndef TERMINAL_VT_SESSION_H
#define TERMINAL_VT_SESSION_H

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "terminal_vt_support.h"

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
#include <util.h>
#else
#include <pty.h>
#endif

#include <ghostty/vt.h>

#define PTY_TIMEOUT_MS 8000
#define READ_CHUNK 4096

typedef struct {
  int master_fd;
  pid_t pid;
  GhosttyTerminal terminal;
  uint16_t cols;
  uint16_t rows;
  buffer_t transcript;
  bool child_exited;
  bool io_failed;
  int exit_code;
} vt_session_t;

bool vt_session_start(vt_session_t *session, const char *binary,
                      const char *const *args, size_t argc, uint16_t cols,
                      uint16_t rows);
void vt_session_close(vt_session_t *session);
void vt_drain(vt_session_t *session, int idle_ms);
bool vt_expect_text(vt_session_t *session, const char *needle, int timeout_ms,
                    char **last_snapshot);
bool vt_send(vt_session_t *session, const char *bytes);
bool vt_resize(vt_session_t *session, uint16_t cols, uint16_t rows);
int vt_wait_for_exit(vt_session_t *session, int timeout_ms);

#endif
