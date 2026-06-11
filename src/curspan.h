/*
 * curspan.h — the Curspan framework umbrella header.
 *
 * Curspan is a high-level framework for building command-line tools and
 * terminal UIs in C23. One include gives you the public surface:
 *
 *   - theming   : design tokens -> semantic roles -> named, overridable themes
 *                 (cs_theme.h)
 *   - surface   : a neutral render target that draws to a CLI stream or a TUI
 *                 window and degrades color per terminal (surface.h)
 *   - components: a catalog of themeable widgets that render on either surface
 *                 (components.h)
 *
 * Components are "open code": you own the source. The `curspan` CLI copies a
 * component and its dependency closure into your project from registry.json,
 * the same way ShadCN distributes UI components. See docs/COMPONENTS.md and
 * docs/THEMING.md.
 *
 * This header is safe to include in any build configuration; the TUI-only and
 * CLI-only surfaces are selected at compile time.
 */

#pragma once

#include "components/components.h"
#include "style/cs_theme.h"
#include "surface/surface.h"

// Compile-time framework version, for feature detection across updates.
#define CURSPAN_VERSION_MAJOR 0
#define CURSPAN_VERSION_MINOR 1
#define CURSPAN_VERSION_PATCH 0
#define CURSPAN_VERSION_ENCODE(major, minor, patch) \
  (((major) * 1000000) + ((minor) * 1000) + (patch))
#define CURSPAN_VERSION                                                \
  CURSPAN_VERSION_ENCODE(CURSPAN_VERSION_MAJOR, CURSPAN_VERSION_MINOR, \
                         CURSPAN_VERSION_PATCH)
