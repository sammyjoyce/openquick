#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "terminal_vt_scenarios.h"

static bool detect_tui_enabled(const char *binary) {
  int pipe_fds[2] = {-1, -1};
  if (pipe(pipe_fds) != 0) {
    perror("pipe");
    return false;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return false;
  }

  if (pid == 0) {
    close(pipe_fds[0]);
    if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
      _exit(126);
    }
    close(pipe_fds[1]);
    char *const args[] = {(char *)binary, "--json", "info", NULL};
    execv(binary, args);
    _exit(127);
  }

  close(pipe_fds[1]);
  char output[4096];
  size_t used = 0;
  while (used + 1 < sizeof(output)) {
    const ssize_t n =
        read(pipe_fds[0], output + used, sizeof(output) - used - 1);
    if (n > 0) {
      used += (size_t)n;
      continue;
    }
    if (n == 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    break;
  }
  close(pipe_fds[0]);
  output[used] = '\0';

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      return false;
    }
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return false;
  }
  return strstr(output, "\"tui\":true") != NULL;
}

static bool parse_tui_enabled_arg(const char *binary, const char *value) {
  if (strcmp(value, "auto") == 0) {
    return detect_tui_enabled(binary);
  }
  return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
         strcmp(value, "yes") == 0;
}

int main(int argc, char **argv) {
  const char *binary = NULL;
  const char *tui_enabled_arg = "auto";

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--binary") == 0 && i + 1 < argc) {
      binary = argv[++i];
    } else if (strcmp(argv[i], "--tui-enabled") == 0 && i + 1 < argc) {
      tui_enabled_arg = argv[++i];
    } else {
      fprintf(
          stderr,
          "usage: %s --binary PATH [--tui-enabled auto|0|1]\nunknown arg: %s\n",
          argv[0], argv[i]);
      return 2;
    }
  }

  if (!binary) {
    fprintf(stderr, "usage: %s --binary PATH [--tui-enabled auto|0|1]\n",
            argv[0]);
    return 2;
  }

  struct stat st;
  if (stat(binary, &st) != 0) {
    fprintf(stderr, "terminal-vt tests: binary does not exist: %s\n", binary);
    return 2;
  }

  const bool tui_enabled = parse_tui_enabled_arg(binary, tui_enabled_arg);
  test_stats_t stats = {0};
  printf("TAP version 13\n");
  run_tui_bare_invocation(&stats, binary, tui_enabled);
  run_tui_bare_invocation_json(&stats, binary, tui_enabled);
  run_tui_menu_test(&stats, binary, tui_enabled);
  run_tui_stress_smoke(&stats, binary, tui_enabled);
  run_tui_menu_search(&stats, binary, tui_enabled);
  run_tui_menu_mnemonic(&stats, binary, tui_enabled);
  run_tui_menu_separator(&stats, binary, tui_enabled);
  run_tui_menu_resize(&stats, binary, tui_enabled);
  run_tui_menu_handler_resize(&stats, binary, tui_enabled);
  run_tui_menu_sigint(&stats, binary, tui_enabled);
  run_tui_menu_sigterm(&stats, binary, tui_enabled);
  printf("1..%d\n", stats.passed + stats.failed + stats.skipped);
  fprintf(stderr, "%d passed, %d failed, %d skipped\n", stats.passed,
          stats.failed, stats.skipped);
  return stats.failed == 0 ? 0 : 1;
}
