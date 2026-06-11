/*
 * Entry point for the CLI contract tests. Parses the --binary flag, then
 * runs every case from cli_contract_cases.c using TAP output.
 */

#include <stdio.h>
#include <string.h>

#include "cli_contract.h"

static void usage(const char *argv0) {
  fprintf(stderr, "usage: %s --binary PATH\n", argv0);
}

static void run_test(test_context_t *ctx, const test_case_t *test) {
  if (test->fn(ctx)) {
    ctx->passed++;
    printf("ok %d - %s\n", ctx->passed + ctx->failed, test->name);
  } else {
    ctx->failed++;
    printf("not ok %d - %s\n", ctx->passed + ctx->failed, test->name);
  }
}

int main(int argc, char **argv) {
  const char *binary = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--binary") == 0 && i + 1 < argc) {
      binary = argv[++i];
    } else {
      usage(argv[0]);
      fprintf(stderr, "unknown arg: %s\n", argv[i]);
      return 2;
    }
  }
  if (!binary) {
    usage(argv[0]);
    return 2;
  }

  test_context_t ctx = {.binary = binary, .passed = 0, .failed = 0};

  printf("TAP version 13\n");
  for (size_t i = 0; i < cli_contract_cases_count; i++) {
    run_test(&ctx, &cli_contract_cases[i]);
  }
  printf("1..%zu\n", cli_contract_cases_count);
  fprintf(stderr, "%d passed, %d failed\n", ctx.passed, ctx.failed);
  return ctx.failed == 0 ? 0 : 1;
}
