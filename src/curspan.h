/*
 * curspan.h — internal OpenQuick component umbrella header.
 *
 * OpenQuick keeps the C23 terminal rendering components vendored. One include
 * gives the CLI/TUI presentation layer access to:
 *
 *   - theming   : design tokens -> semantic roles -> named, overridable themes
 *                 (cs_theme.h)
 *   - surface   : a neutral render target that draws to a CLI stream or a TUI
 *                 window and degrades color per terminal (surface.h)
 *   - components: a catalog of themeable widgets that render on either surface
 *                 (components.h)
 *
 * Components are vendored source. The local component registry tracks files and
 * dependency closures for validation. See docs/COMPONENTS.md and
 * docs/THEMING.md.
 *
 * This header is safe to include in any build configuration; the TUI-only and
 * CLI-only surfaces are selected at compile time.
 */

#pragma once

#include "components/components.h"
#include "style/cs_theme.h"
#include "surface/surface.h"

// Compile-time component surface version, for feature detection across updates.
#define CURSPAN_VERSION_MAJOR 0
#define CURSPAN_VERSION_MINOR 1
#define CURSPAN_VERSION_PATCH 0
#define CURSPAN_VERSION_ENCODE(major, minor, patch) \
  (((major) * 1000000) + ((minor) * 1000) + (patch))
#define CURSPAN_VERSION                                                \
  CURSPAN_VERSION_ENCODE(CURSPAN_VERSION_MAJOR, CURSPAN_VERSION_MINOR, \
                         CURSPAN_VERSION_PATCH)
