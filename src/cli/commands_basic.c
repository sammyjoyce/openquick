/*
 * Built-in "hello" and "echo" commands.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/config.h"
#include "../core/error.h"
#include "../io/output.h"
#include "commands.h"

app_error app_cmd_hello(const app_config_t *config, int argc,
                        char *const argv[]);
app_error app_cmd_echo(const app_config_t *config, int argc,
                       char *const argv[]);

app_error app_cmd_hello(const app_config_t *config, int argc,
                        char *const argv[]) {
  const char *name = argc > 0 ? argv[0] : "World";
  app_output_format(config, false, "Hello, %s!", name);
  return APP_SUCCESS;
}

// Echo joins its arguments into a single line. We grow a buffer instead of
// using a fixed 4 KiB stack buffer so very long arg lists print intact.
app_error app_cmd_echo(const app_config_t *config, int argc,
                       char *const argv[]) {
  size_t total = 0;
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i] ? argv[i] : "";
    const size_t separator = i == 0 ? 0U : 1U;
    const size_t len = strlen(arg);
    if (SIZE_MAX - total < separator || SIZE_MAX - total - separator < len) {
      return APP_ERROR_OVERFLOW;
    }
    total += separator + len;
  }

  if (SIZE_MAX - total < 1U) {
    return APP_ERROR_OVERFLOW;
  }

  char *buf = malloc(total + 1);
  if (!buf) {
    return APP_ERROR_MEMORY;
  }

  size_t used = 0;
  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i] ? argv[i] : "";
    if (i > 0) {
      buf[used++] = ' ';
    }
    const size_t len = strlen(arg);
    memcpy(buf + used, arg, len);
    used += len;
  }
  buf[used] = '\0';

  app_output(buf, config, false);
  free(buf);
  return APP_SUCCESS;
}
