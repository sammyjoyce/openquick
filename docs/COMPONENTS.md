# Components

Curspan ships a catalog of terminal-UI components. Each one is a self-contained
`cs_<name>.h` / `cs_<name>.c` pair that you **own**: copy it into your project
with [`curspan add`](#getting-a-component) and edit it like your own code. Every
component renders through one neutral [surface](#the-surface) and styles itself
through [theme roles](THEMING.md), so the same call draws on a piped CLI, a
truecolor terminal, and inside a TUI window.

- [The surface](#the-surface)
- [Drawing API](#drawing-api)
- [The catalog](#the-catalog)
- [Component reference](#component-reference)
- [Glyphs](#glyphs)
- [Getting a component](#getting-a-component)

## The surface

A `cs_surface_t` is a styled, role-aware drawing target. It hides whether output
is a byte stream (CLI: SGR escapes, degraded by color profile) or an ncurses
window (TUI: color pairs). Components never name a raw color or an escape code —
the surface owns capability detection and color degradation; components stay pure
layout and semantics.

```c
#include "curspan.h"

cs_surface_t *s = cs_surface_stream_new(stdout, config, NULL); // NULL => default theme
cs_heading_render(&(cs_heading_t){.text = "Report", .underline = true}, s);
cs_surface_free(s);
```

Create one of two backends:

| Constructor | Backend | Notes |
| --- | --- | --- |
| `cs_surface_stream_new(FILE *stream, const app_config_t *config, const cs_theme_t *theme)` | byte stream (CLI) | `config` supplies the color policy (`NO_COLOR` / `--plain` / `--json` …); pass `NULL` for capability-only detection. `theme` is copied; `NULL` => default. |
| `cs_surface_curses_new(tui_window_t *window, const cs_theme_t *theme)` | ncurses window (TUI) | Only declared under `ENABLE_TUI`. Requires `tui_init()` + `tui_init_colors()` first. |

Free a surface with `cs_surface_free(s)`. The surface borrows the `FILE *` /
window — freeing the surface never closes your stream or destroys your window.

Introspect what a surface can do and branch on it to degrade gracefully:

```c
cs_caps_t caps = cs_surface_caps(s);
// caps.tty, caps.color, caps.unicode, caps.interactive,
// caps.profile (none/16/256/truecolor), caps.color_count, caps.width
size_t cols = cs_surface_width(s);
```

## Drawing API

The surface API is intentionally small. Styling is **additive until
`cs_surface_reset`** — set a role and/or attributes, write, then reset.

```c
void cs_surface_set_role(cs_surface_t *s, cs_role_t role);     // foreground role
void cs_surface_set_role_bg(cs_surface_t *s, cs_role_t role);  // background role (where supported)
void cs_surface_set_attr(cs_surface_t *s, cs_attr_t attrs);    // CS_ATTR_BOLD/DIM/UNDERLINE/ITALIC
void cs_surface_reset(cs_surface_t *s);

void cs_surface_write(cs_surface_t *s, const char *utf8);
void cs_surface_write_n(cs_surface_t *s, const char *utf8, size_t n);
void cs_surface_repeat(cs_surface_t *s, const char *glyph, size_t count); // rules / padding
void cs_surface_newline(cs_surface_t *s);
void cs_surface_move(cs_surface_t *s, int x, int y);  // curses only; no-op on a stream

// Convenience: write `text` in `role` (+ optional attrs) then reset.
void cs_surface_styled(cs_surface_t *s, cs_role_t role, cs_attr_t attrs, const char *text);
```

`move()` is a no-op on a stream surface, which flows top-to-bottom; on a curses
surface it positions the cursor. Writing a component that needs absolute
positioning therefore ties it to the TUI — most catalog components avoid it.

## The catalog

| Component | Header | Renders |
| --- | --- | --- |
| [`cs_rule`](#cs_rule) | `cs_rule.h` | a horizontal divider, optionally with a centered label |
| [`cs_heading`](#cs_heading) | `cs_heading.h` | a styled section heading, optionally underlined |
| [`cs_badge`](#cs_badge) | `cs_badge.h` | a small inline status label, colored by variant |
| [`cs_note`](#cs_note) | `cs_note.h` | a callout block with a colored gutter and wrapped body |
| [`cs_keyvalue`](#cs_keyvalue) | `cs_keyvalue.h` | an aligned list of key → value rows |
| [`cs_list`](#cs_list) | `cs_list.h` | a bulleted or numbered list with wrap + hanging indent |
| [`cs_table`](#cs_table) | `cs_table.h` | a columnar table with alignment and width budgeting |
| [`cs_progress`](#cs_progress) | `cs_progress.h` | a one-shot progress bar |
| [`cs_spinner`](#cs_spinner) | `cs_spinner.h` | a frame-based activity indicator |

Pull in the whole catalog with `#include "components/components.h"` (or the
umbrella `curspan.h`), or include individual `cs_<name>.h` headers for a leaner
build.

Every component follows the same contract:

- a **props struct** `cs_<name>_t` whose pointer fields are **borrowed** — the
  component copies nothing, so every pointer must outlive the render call;
- a render entry point `void cs_<name>_render(const cs_<name>_t *, cs_surface_t *)`
  (`cs_spinner` also takes a frame index);
- a `0` / `NULL` value for any role or size field means "use the sensible
  default" (the documented role, or the surface width);
- `render(NULL, s)` and `render(p, NULL)` are safe no-ops.

## Component reference

### cs_rule

```c
typedef struct cs_rule {
  const char *label;     // optional; centered in the rule (NULL => plain line)
  cs_role_t role;        // line color role (0 => CS_ROLE_BORDER)
  cs_role_t label_role;  // label color role (0 => CS_ROLE_TITLE)
  size_t width;          // total columns (0 => the surface width)
  const char *glyph;     // line glyph (NULL => unicode "─" / ascii "-")
} cs_rule_t;

cs_rule_render(&(cs_rule_t){.label = "OPTIONS", .width = 40}, s);
```

### cs_heading

```c
typedef struct cs_heading {
  const char *text;
  cs_role_t role;  // heading color role (0 => CS_ROLE_TITLE)
  bool uppercase;  // upper-case the text (ASCII letters only)
  bool underline;  // draw a rule beneath, as wide as the heading
} cs_heading_t;

cs_heading_render(&(cs_heading_t){.text = "Usage", .uppercase = true, .underline = true}, s);
```

### cs_badge

```c
typedef enum cs_badge_variant {
  CS_BADGE_NEUTRAL = 0, CS_BADGE_INFO, CS_BADGE_SUCCESS, CS_BADGE_WARNING, CS_BADGE_ERROR,
} cs_badge_variant_t;

typedef struct cs_badge {
  const char *text;
  cs_badge_variant_t variant;
  bool no_marker;  // omit the leading glyph, render just the bracketed label
} cs_badge_t;

cs_badge_render(&(cs_badge_t){.text = "OK", .variant = CS_BADGE_SUCCESS}, s);
```

### cs_note

```c
typedef enum cs_note_variant {
  CS_NOTE_INFO = 0, CS_NOTE_SUCCESS, CS_NOTE_WARNING, CS_NOTE_ERROR,
} cs_note_variant_t;

typedef struct cs_note {
  cs_note_variant_t variant;
  const char *title;  // optional
  const char *body;   // wrapped to the content width
  size_t width;       // total columns (0 => the surface width)
} cs_note_t;

cs_note_render(&(cs_note_t){.variant = CS_NOTE_WARNING,
                            .title = "Heads up",
                            .body = "This cannot be undone.",
                            .width = 50}, s);
```

### cs_keyvalue

```c
typedef struct cs_keyvalue_pair { const char *key; const char *value; } cs_keyvalue_pair_t;

typedef struct cs_keyvalue {
  const cs_keyvalue_pair_t *pairs;
  size_t count;
  cs_role_t key_role;     // 0 => CS_ROLE_MUTED
  cs_role_t value_role;   // 0 => CS_ROLE_TEXT
  const char *separator;  // between key column and value (NULL => "  ")
} cs_keyvalue_t;

cs_keyvalue_pair_t info[] = {{"Application", "myapp"}, {"Version", "0.1.0"}};
cs_keyvalue_render(&(cs_keyvalue_t){.pairs = info, .count = 2}, s);
```

Keys pad to the widest key so values align in a column.

### cs_list

```c
typedef enum cs_list_style { CS_LIST_BULLET = 0, CS_LIST_NUMBERED } cs_list_style_t;

typedef struct cs_list {
  const char *const *items;
  size_t count;
  cs_list_style_t style;
  cs_role_t marker_role;  // 0 => CS_ROLE_ACCENT
  cs_role_t text_role;    // 0 => CS_ROLE_TEXT
  size_t width;           // 0 => the surface width
  int start;              // numbered lists: first number (0 => 1)
} cs_list_t;

const char *steps[] = {"Clone the repo", "Build with zig", "Run the binary"};
cs_list_render(&(cs_list_t){.items = steps, .count = 3, .style = CS_LIST_NUMBERED}, s);
```

Long items wrap with a hanging indent aligned under the first character after the
marker.

### cs_table

```c
typedef enum cs_align { CS_ALIGN_LEFT = 0, CS_ALIGN_RIGHT } cs_align_t;

typedef struct cs_table_column {
  const char *header;
  cs_align_t align;
  int max_width;  // 0 => unbounded (still subject to the overall width budget)
} cs_table_column_t;

typedef struct cs_table {
  const cs_table_column_t *columns;
  size_t column_count;
  const char *const *cells;  // row-major, column_count entries per row
  size_t row_count;
  bool header;            // render the header row + a separator rule
  cs_role_t header_role;  // 0 => CS_ROLE_TITLE
  cs_role_t text_role;    // 0 => CS_ROLE_TEXT
  cs_role_t border_role;  // 0 => CS_ROLE_BORDER
  size_t width;           // total budget (0 => the surface width)
  const char *gap;        // inter-column gap (NULL => "  ")
} cs_table_t;

cs_table_column_t cols[] = {{.header = "Name"}, {.header = "Status", .align = CS_ALIGN_RIGHT}};
const char *cells[] = {"build", "ok", "tests", "ok"};
cs_table_render(&(cs_table_t){.columns = cols, .column_count = 2,
                              .cells = cells, .row_count = 2,
                              .header = true, .width = 40}, s);
```

Columns take their natural width (header vs. widest cell, capped by `max_width`),
then shrink proportionally to fit the budget; over-long cells truncate. A
non-zero `row_count` must be matched by a non-NULL `cells`.

### cs_progress

A one-shot bar (distinct from the interactive `tui_progress`): render it once,
inline, as part of other output.

```c
typedef struct cs_progress {
  const char *label;     // optional, shown before the bar
  double value;          // 0..1 fraction (used when total <= 0)
  long current;          // with total > 0, fraction = current/total
  long total;            // 0 => use `value`
  size_t width;          // total columns (0 => the surface width)
  cs_role_t bar_role;    // filled portion (0 => CS_ROLE_SUCCESS)
  cs_role_t track_role;  // empty portion (0 => CS_ROLE_MUTED)
  bool show_percent;     // append the percentage
} cs_progress_t;

cs_progress_render(&(cs_progress_t){.label = "Building", .current = 7, .total = 10,
                                    .width = 40, .show_percent = true}, s);
```

The fraction is clamped to `[0, 1]` (and a non-finite fraction is treated as 0).

### cs_spinner

Stateless: the caller owns the frame counter and the redraw cadence. Render a
frame, sleep, advance, and redraw (move the cursor back with `\r` on a stream).

```c
typedef enum cs_spinner_style {
  CS_SPINNER_DOTS = 0,  // braille dots (unicode), ASCII line fallback
  CS_SPINNER_LINE,      // pipe, slash, dash, backslash
} cs_spinner_style_t;

typedef struct cs_spinner {
  const char *label;  // optional, shown after the spinner glyph
  cs_spinner_style_t style;
  cs_role_t role;  // spinner glyph color (0 => CS_ROLE_ACCENT)
} cs_spinner_t;

int cs_spinner_frame_count(cs_spinner_style_t style, bool unicode);
void cs_spinner_render(const cs_spinner_t *spinner, int frame, cs_surface_t *s);

for (int f = 0; working; f++) {
  fputc('\r', stdout);
  cs_spinner_render(&(cs_spinner_t){.label = "Working"}, f, s);
  fflush(stdout);
  /* sleep, do a slice of work */
}
```

`cs_spinner_render` writes inline with no trailing newline; braille frames
degrade to an ASCII line-spinner when unicode is unavailable.

## Glyphs

Components draw box and marker shapes through `cs_glyphs.h` (header-only). Each
getter takes the surface's `unicode` capability and returns a UTF-8 glyph or an
ASCII fallback, so a component draws the same shapes on a modern and a legacy
terminal without re-deriving the fallbacks:

| Getter | Unicode | ASCII | Getter | Unicode | ASCII |
| --- | --- | --- | --- | --- | --- |
| `cs_glyph_hline` | `─` | `-` | `cs_glyph_check` | `✓` | `+` |
| `cs_glyph_vline` | `│` | `\|` | `cs_glyph_cross` | `✗` | `x` |
| `cs_glyph_gutter` | `┃` | `\|` | `cs_glyph_warning` | `⚠` | `!` |
| `cs_glyph_bullet` | `•` | `*` | `cs_glyph_info` | `ℹ` | `i` |
| `cs_glyph_arrow` | `❯` | `>` | `cs_glyph_bar_full` | `█` | `#` |
| `cs_glyph_bar_empty` | `░` | `.` | | | |

```c
cs_caps_t caps = cs_surface_caps(s);
cs_surface_repeat(s, cs_glyph_hline(caps.unicode), cs_surface_width(s));
```

## Getting a component

Components are open code — you copy the source into your project and own it. The
[`curspan` CLI](../registry/registry.json) reads the registry and copies a
component plus its full dependency closure:

```bash
zig build curspan                       # build zig-out/bin/curspan
./zig-out/bin/curspan list              # the catalog, grouped by category
./zig-out/bin/curspan info table        # a component's files + dependency closure
./zig-out/bin/curspan add table --dest . # copy cs_table + its deps into ./src/...
```

`add` prints the exact `build.zig` source lines to add for the copied `.c`
files. See [the registry section of ARCHITECTURE.md](ARCHITECTURE.md#distribution-the-registry-and-curspan-add)
for how distribution works.

## See also

- [THEMING.md](THEMING.md) — the roles every component styles itself through
- [ARCHITECTURE.md](ARCHITECTURE.md) — where the surface and catalog sit
- [examples/custom-tui.md](../examples/custom-tui.md) — composing components on a TUI surface
