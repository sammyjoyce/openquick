#ifndef CLI_CONTRACT_H
#define CLI_CONTRACT_H

#include <stdbool.h>
#include <stddef.h>

#define ARRAY_LEN(items) (sizeof(items) / sizeof((items)[0]))

typedef struct {
  int exit_code;
  char *out;
  char *err;
} command_result_t;

typedef struct {
  const char *name;
  const char *value;
} env_var_t;

typedef struct {
  const char *binary;
  int passed;
  int failed;
} test_context_t;

typedef bool (*test_fn_t)(test_context_t *ctx);

typedef struct {
  const char *name;
  test_fn_t fn;
} test_case_t;

// Helpers (helpers.c)
char *cc_format_string(const char *fmt, ...);
char *cc_copy_string(const char *value);
command_result_t cc_run_cli(test_context_t *ctx, const char *const *args,
                            size_t arg_count, const env_var_t *env,
                            size_t env_count);
command_result_t cc_run_cli_with_stdin(test_context_t *ctx,
                                       const char *const *args,
                                       size_t arg_count, const char *stdin_text,
                                       const env_var_t *env, size_t env_count);
void cc_command_result_free(command_result_t *result);
bool cc_expect_exit(const command_result_t *result, int expected);
bool cc_expect_not_exit(const command_result_t *result, int unexpected);
bool cc_expect_stdout_contains(const command_result_t *result,
                               const char *needle);
bool cc_expect_stderr_contains(const command_result_t *result,
                               const char *needle);
// Assert needle appears exactly once in stderr. Used to verify a JSON error
// path emits a single envelope (one "format_version") rather than several
// concatenated objects that would break single-document parsers.
bool cc_expect_stderr_occurs_once(const command_result_t *result,
                                  const char *needle);
bool cc_expect_stdout_empty(const command_result_t *result);
bool cc_write_temp_config(const char *content, char **path_out);
bool cc_file_exists(const char *path);
char *cc_read_text_file(const char *path);
char *cc_binary_name(const char *path);
char *cc_replace_all(const char *input, const char *needle,
                     const char *replacement);
void cc_strip_carriage_returns(char *text);

// Test cases (cases.c)
extern const test_case_t cli_contract_cases[];
extern const size_t cli_contract_cases_count;

#endif
