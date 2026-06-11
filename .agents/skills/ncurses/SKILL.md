---
name: ncurses
description: >
  Authoritative ncurses guidance for implementing, debugging, reviewing, or
  explaining curses terminal programs. Use for initscr/endwin lifecycle,
  windows, pads, refresh behavior, getch/keypad/input modes, colors,
  attributes, ACS or Unicode drawing, mouse events, resizing, terminfo, TERM,
  ncursesw, PDCurses, and curses portability issues.
---

# Ncurses Authority

Use this skill as the ncurses authority for terminal UI work. Local project
rules can decide file layout and build commands, but ncurses behavior, terminal
state, input handling, rendering, and portability decisions should follow this
skill unless current official ncurses documentation contradicts it.

Maintenance scope is in `SPEC.md`; source provenance and decision history are
in `SOURCES.md`.

## Core Rule

Ncurses is a stateful terminal protocol library, not a screen canvas. Correct
programs preserve terminal state, draw into curses windows, explicitly refresh,
respect terminfo capabilities, and degrade when the terminal cannot do what the
program wants.

## First Moves

1. Identify the curses implementation: ncurses, ncursesw, PDCurses, or another
   curses-compatible library.
2. Identify the terminal contract: `TERM`, terminfo entry, color support,
   locale, interactive TTY availability, and expected resize/mouse behavior.
3. Choose the narrow topic reference:
   - Read `references/lifecycle-screen-model.md` for initialization, cleanup,
     screen state, refresh, resizing, and terminal recovery.
   - Read `references/input-modes-keys.md` for `getch()`, `keypad()`, echo,
     raw/cbreak modes, timeouts, escape handling, line input, and mouse events.
   - Read `references/rendering-windows-colors.md` for windows, pads,
     subwindows, text output, attributes, ACS/Unicode drawing, colors, and
     efficient refresh.
   - Read `references/troubleshooting-portability.md` for broken terminals,
     bad `TERM`, missing colors, garbled keys, stale redraws, link/include
     issues, ncursesw, PDCurses, and cross-terminal behavior.
4. Use C for Dummies examples as learning examples and quick reminders, not as
   copy-paste source.
5. For obscure API behavior, confirm against current ncurses man pages before
   finalizing.

## Non-Negotiables

- Call `setlocale(LC_ALL, "")` before starting curses when wide characters,
  ACS/line drawing, or user locale matters.
- Enter curses once per screen with `initscr()` or `newterm()`. Do not mix the
  two casually.
- Call `endwin()` on every normal and error path after curses has started.
- Use `cbreak()` or `raw()` deliberately. Do not leave canonical mode, echo,
  cursor visibility, or timeout settings changed by accident.
- Use `int` for `getch()` results so `KEY_*` and `ERR` are representable.
- Call `keypad(win, TRUE)` for windows that read function keys, arrows, or
  keypad keys.
- Treat `stdscr` as a window, not as the terminal. Drawing changes window
  memory; `refresh()`, `wrefresh()`, `pnoutrefresh()`, or `doupdate()` make
  changes visible.
- Check sizes before drawing. Never assume `LINES`, `COLS`, or a child window
  has enough room for labels, borders, prompts, or status lines.
- Use bounded output for untrusted or variable-length text.
- Call `has_colors()` before color paths and `start_color()` before color-pair
  manipulation.
- Pair every attribute/color enable with a matching disable in the same logical
  drawing region.
- Do not assume Unicode, mouse reporting, default colors, or extended colors
  are portable.

## Fast Decision Table

| Need | Default decision |
| --- | --- |
| Full-screen application | `setlocale`, `initscr`, `cbreak`, `noecho`, `keypad(stdscr, TRUE)`, draw, refresh, cleanup with `endwin`. |
| Multiple screen regions | Use separate `WINDOW *` objects and refresh each owner window. |
| Large scrollable content | Use a pad when content is larger than the visible viewport. |
| Smooth multi-window updates | Use `wnoutrefresh()` or `pnoutrefresh()` for each window, then `doupdate()`. |
| Password or hidden input | Use `noecho()` and restore echo/cursor state after the prompt. |
| Arrow/function keys | Enable `keypad()` and compare `int` results against `KEY_*`. |
| Color styling | `has_colors()`, `start_color()`, initialize pairs, then apply `COLOR_PAIR(n)`. |
| Default terminal colors | Prefer default-color extensions only after checking implementation support. |
| Mouse support | Gate ncurses mouse code with `NCURSES_MOUSE_VERSION` when portability matters. |
| Unknown terminal behavior | Inspect `TERM`, terminfo, locale, and whether stdin/stdout are real TTYs. |

## Verification

For code changes, verify the ncurses behavior in the narrowest realistic way:

```bash
infocmp "$TERM"
printf '%s\n' "$TERM"
```

Then run the program in a real terminal and check:

- startup and cleanup restore the shell prompt and echo behavior;
- keys produce the intended `KEY_*`, character, or `ERR` path;
- redraw works after covering/uncovering the terminal and after resize;
- color paths degrade when `has_colors()` is false;
- no drawing writes outside the target window or pad viewport.

If validation is non-interactive only, say that terminal state, keyboard, mouse,
and resize behavior still require a real-terminal pass.
