# Troubleshooting And Portability

Open this when ncurses code behaves differently across terminals, platforms,
builds, or non-interactive environments.

## Terminal Capability Checks

Run these before blaming application logic:

```bash
printf '%s\n' "$TERM"
infocmp "$TERM"
stty -a
```

Check whether stdin and stdout are real TTYs. Curses programs usually require
an interactive terminal, not a plain pipe or log capture.

## Common Failures

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Shell prompt does not echo after exit | Missing `endwin()` or echo restoration | Add cleanup on every initialized path; use `stty sane` for recovery. |
| Screen does not update | Drawing happened without the right refresh | Refresh the owner window, or use `wnoutrefresh()` plus `doupdate()`. |
| Old modal content remains | Underlying windows were not touched/redrawn | Redraw exposed windows after deleting the modal. |
| Arrow keys are not `KEY_*` | Missing/wrong `keypad()` or bad `TERM` | Enable keypad on the input window and verify terminfo. |
| Colors are absent | Terminal or terminfo lacks color capability | Check `has_colors()` and degrade without color. |
| Colors have wrong background | Default color assumptions differ | Use default-color extensions deliberately, or set explicit background pairs. |
| Unicode drawing is broken | Locale, library, terminal, or font mismatch | Set locale, use ncursesw/wide APIs, and provide ACS/ASCII fallback. |
| Mouse events never arrive | Terminal mouse reporting or implementation support missing | Enable `mousemask()`, handle `KEY_MOUSE`, and provide keyboard alternatives. |
| Layout breaks on resize | Dimensions cached or windows not rebuilt | Handle resize, recompute layout, and redraw all regions. |
| CI run hangs | Program expects interactive input | Use timeouts or test non-visual logic separately from real-terminal passes. |

## Implementation Variance

- `ncursesw` is the normal choice for wide-character applications. Linking
  against narrow curses while using wide-character APIs causes build or runtime
  trouble.
- PDCurses is curses-compatible but not identical to ncurses. Check mouse,
  color, resize, and terminal-mode behavior on the target platform.
- Default-color and extended-color APIs are extensions. Guard assumptions when
  portability matters.
- Mouse support is not portable across all curses implementations. Gate
  ncurses-specific code with `NCURSES_MOUSE_VERSION` where needed.
- Terminfo entries can be missing, stale, or mismatched with the actual
  terminal emulator.

## Build And Link Diagnostics

- Prefer `pkg-config --cflags --libs ncursesw` for wide-character builds when
  available.
- If `pkg-config` is absent, inspect platform package names and installed
  headers/libraries rather than guessing include paths.
- Include `<curses.h>` for portable curses code. For wide-character
  (`ncursesw`) support, define `_XOPEN_SOURCE_EXTENDED` before including
  `<curses.h>` to expose wide-character functions and types (e.g. `cchar_t`,
  `get_wch`). Use implementation-specific headers only when the project
  intentionally targets that implementation.
- Avoid mixing headers from one curses implementation with libraries from
  another.

## Real-Terminal Validation

Non-interactive tests can prove parsing and pure logic, but they do not fully
prove curses behavior. Before declaring visual/input work done, test in a real
terminal:

1. Start and exit normally.
2. Interrupt or trigger the main error path and confirm terminal recovery.
3. Press arrows, function keys, ESC, Enter, and ordinary characters.
4. Resize smaller and larger.
5. Test with color enabled and with a restricted terminal description when
   practical.
6. Cover/uncover or switch away and back to force redraw confidence.
