/*
 * Curses-free terminal facts. This file deliberately avoids terminfo/curses;
 * those remain backend-specific concerns.
 */
#include "terminal.h"

#include <stdio.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

static int app_terminal_fd(app_terminal_stream_t stream) {
  switch (stream) {
  case APP_TERMINAL_STDIN:
    return fileno(stdin);
  case APP_TERMINAL_STDOUT:
    return fileno(stdout);
  case APP_TERMINAL_STDERR:
    return fileno(stderr);
  }
  return -1;
}

bool app_terminal_stream_is_tty(app_terminal_stream_t stream) {
  const int fd = app_terminal_fd(stream);
  return fd >= 0 && isatty(fd);
}

bool app_terminal_is_interactive(void) {
  return app_terminal_stream_is_tty(APP_TERMINAL_STDIN) &&
         app_terminal_stream_is_tty(APP_TERMINAL_STDOUT);
}

void app_terminal_discard_pending_input(void) {
  const int fd = app_terminal_fd(APP_TERMINAL_STDIN);
  if (fd < 0 || !isatty(fd)) {
    return;
  }
#ifndef _WIN32
  // Drop bytes the TTY queued but no reader consumed, typically a terminfo
  // probe reply (DSR/u7) that arrives after curses has stopped reading.
  (void)tcflush(fd, TCIFLUSH);
#endif
  // Windows consoles do not emit the terminfo cursor-position replies this
  // guards against, and PDCurses' flushinp() already drains the curses queue,
  // so there is nothing further to discard there.
}

app_terminal_size_t app_terminal_query_size(void) {
  app_terminal_size_t size = {0};
#ifdef _WIN32
  CONSOLE_SCREEN_BUFFER_INFO info;
  HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (handle != INVALID_HANDLE_VALUE &&
      GetConsoleScreenBufferInfo(handle, &info)) {
    size.cols = info.srWindow.Right - info.srWindow.Left + 1;
    size.rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    size.known = size.cols > 0 && size.rows > 0;
  }
#else
  struct winsize ws;
  if (ioctl(app_terminal_fd(APP_TERMINAL_STDOUT), TIOCGWINSZ, &ws) == 0) {
    size.cols = ws.ws_col;
    size.rows = ws.ws_row;
    size.known = size.cols > 0 && size.rows > 0;
  }
#endif
  return size;
}
