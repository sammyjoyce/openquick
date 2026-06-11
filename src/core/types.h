/*
 * Core type definitions for the application.
 * This header centralizes all fundamental type definitions to ensure
 * consistency across the codebase and prevent circular dependencies. By
 * defining types here, we establish a single source of truth for data
 * structures that multiple modules depend on.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declaration to avoid circular dependency with config.h.
// This allows other headers to use app_config_t* without including the full
// definition.
typedef struct app_config app_config_t;

// Generic buffer type for dynamic data.
// Uses separate size/capacity tracking to enable efficient reallocation
// without constantly calling strlen(). This pattern is borrowed from
// std::vector and similar growable containers.
typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} app_buffer_t;

// The build pins -std=c23 (see build.zig). Bare minimum shim is provided so
// the headers remain usable from tools that probe with an older standard; do
// not add fake polyfills for features that need real compiler support.
#if __STDC_VERSION__ < 202311L
#define nullptr ((void *)0)
#endif

// C23 attribute marks functions whose return values must be checked.
#if __STDC_VERSION__ >= 202311L
#define APP_NODISCARD [[nodiscard("Return value should be checked")]]
#else
#define APP_NODISCARD
#endif

// Application-wide constants define default values and limits.
// These are centralized here to make configuration changes easier and ensure
// consistent behavior across all modules.

// Version is defined by the build system via -DAPP_VERSION compiler flag.
// This fallback is only used if the build system doesn't provide it.
#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

// Application name
#ifndef APP_NAME
#define APP_NAME "myapp"
#endif

// Human-readable application title and description
#ifndef APP_TITLE
#define APP_TITLE "Curspan"
#endif

#ifndef APP_DESCRIPTION
// clang-format off
#define APP_DESCRIPTION \
  "A ready-to-use C23 starter for command-line tools and ncurses terminal UIs."
// clang-format on
#endif

// Build date - should be provided by build system
#ifndef APP_BUILD_DATE
#define APP_BUILD_DATE "unknown"
#endif

// Git commit - should be provided by build system
#ifndef APP_GIT_COMMIT
#define APP_GIT_COMMIT "unknown"
#endif

// Buffer and limit constants
#define INPUT_BUFFER_INITIAL_SIZE (128 * 1024)
#define INPUT_BUFFER_READ_CHUNK_SIZE 8192
#define INPUT_MAX_SIZE (512 * 1024)
#define CONFIG_MAX_SIZE (64 * 1024)
#define APP_MAX_COMMAND_ARGS 100
#define BUFFER_INITIAL_SIZE (64 * 1024)
#define TIMESTAMP_BUFFER_SIZE 32
#define ID_BUFFER_SIZE 64
#define PATH_BUFFER_SIZE 4096
