# Input Modes And Keys

Open this when implementing or debugging keyboard input, hidden prompts,
timeouts, escape behavior, line input, or mouse events.

## Key Reads

- Store `getch()`/`wgetch()` results in `int`.
- Handle three result classes explicitly:
  - ordinary character values;
  - special key constants such as `KEY_UP`;
  - `ERR` for timeout, non-blocking no-input, or read failure.
- Call `keypad(win, TRUE)` on the same window that reads special keys.
- Prefer `wgetch(win)` for window-owned input so keypad mode and timeout state
  stay local to the input owner.
- Use `ungetch()` sparingly; it is a small pushback mechanism, not an event
  queue architecture.

## Terminal Modes

| Mode | Use |
| --- | --- |
| `cbreak()` | Get characters without waiting for Enter while leaving signal keys active. |
| `raw()` | Read almost everything literally, including control characters. Use only when needed. |
| `echo()` | Let curses echo typed characters. Avoid for full-screen apps except scoped prompts. |
| `noecho()` | Keep input from appearing automatically. Required for hidden input and custom drawing. |
| `nodelay(win, TRUE)` | Poll without blocking; handle `ERR` as "no input". |
| `timeout(ms)` / `wtimeout(win, ms)` | Bound input waits without writing a manual sleep loop. |
| `halfdelay(tenths)` | Wait up to tenths of a second for input in cbreak-like mode. |

Restore modes after temporary prompts, especially echo, cursor visibility, and
timeouts.

## Escape And Function Keys

- Arrow and function keys are not portable escape strings. Use `keypad()` and
  compare against `KEY_*`.
- Escape can be both a standalone key and a prefix for function-key sequences.
  Expect timing behavior; do not assume instantaneous ESC recognition.
- If ESC feels delayed, inspect ncurses escape-delay settings and terminal
  behavior before hard-coding escape sequence parsing.
- NumLock, terminal emulator settings, and `TERM` can affect keypad behavior.

## Line Input

- Prefer bounded input functions or custom editing loops. Unbounded string input
  is unsafe.
- Treat `getnstr()`/`wgetnstr()` as simple line input, not a full editor.
- For password-like prompts, use `noecho()` and bounded buffers, then restore
  the prior echo/cursor state.
- Redraw prompts after resize or repaint events; input routines do not make a
  complete UI layout manager.

## Mouse Events

- Mouse support is not a base curses guarantee. For ncurses-specific mouse code,
  gate portability-sensitive sections with `NCURSES_MOUSE_VERSION`.
- Enable only the event masks needed with `mousemask()`.
- Read mouse details with `getmouse()` after `getch()` returns `KEY_MOUSE`.
- Expect terminal emulator settings to decide whether mouse events are sent at
  all.
- Always provide keyboard alternatives for mouse-only actions.

## Debug Checklist

| Symptom | Check |
| --- | --- |
| Arrow keys print letters or escape text | `keypad()` is missing, on the wrong window, or `TERM` is wrong. |
| ESC is slow | Escape-prefix timing is active; inspect delay settings before parsing raw sequences. |
| Input never returns | Window is blocking; use `wtimeout()` or `nodelay()` intentionally. |
| Input loop spins CPU | Non-blocking `ERR` path lacks pacing or useful work. |
| Password text appears | Echo was not disabled or was restored too early. |
| Mouse never fires | Terminal emulator mouse mode, `mousemask()`, implementation support, or missing `KEY_MOUSE` handling. |
