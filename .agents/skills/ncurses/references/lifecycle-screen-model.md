# Lifecycle And Screen Model

Open this when deciding how a curses program starts, cleans up, refreshes,
recovers the terminal, or handles resizing.

## Startup

- Call `setlocale(LC_ALL, "")` before curses initialization when user locale,
  wide characters, or line drawing matters.
- Use `initscr()` for the common single-terminal case.
- Use `newterm()` only when the program intentionally manages a specific input
  and output stream or multiple screens.
- After startup, establish input/output modes deliberately: commonly
  `cbreak()`, `noecho()`, and `keypad(stdscr, TRUE)`.
- Hide the cursor with `curs_set(0)` only if the terminal supports it and the UI
  owns cursor presentation.

## Cleanup

- Track whether curses initialization succeeded. Call `endwin()` exactly for
  paths that entered curses.
- Cleanup is not optional on errors. Broken shell echo, raw-looking input, or a
  missing cursor usually means an exit skipped cleanup or state restoration.
- Restore temporary modes after local prompts: `echo()`/`noecho()`, cursor
  visibility, blocking mode, and timeout settings.
- Use `def_prog_mode()`/`reset_prog_mode()` and
  `def_shell_mode()`/`reset_shell_mode()` when intentionally leaving and
  returning to curses.

## Screen Model

- `stdscr` is the default window. It is not the terminal itself.
- Curses tracks a virtual screen and a physical screen. Drawing updates window
  memory; refresh operations reconcile those windows with the terminal.
- Use `refresh()` for `stdscr`, `wrefresh(win)` for a specific window, and
  `prefresh()`/`pnoutrefresh()` for pads.
- For coordinated multi-window updates, call `wnoutrefresh()` or
  `pnoutrefresh()` for each changed region, then call `doupdate()` once.
- Do not mix raw terminal writes with curses output unless you leave curses or
  fully understand the repaint consequences.

## Resize

- Use `getmaxyx(win, y, x)` or `getmaxy()`/`getmaxx()` at draw time instead of
  caching dimensions forever.
- Treat `LINES` and `COLS` as screen-size snapshots, not layout guarantees.
- Handle `KEY_RESIZE` where supported, then recompute windows, pads, borders,
  and clipping.
- On small terminals, prefer clipped or scrollable content over drawing outside
  a window.

## Recovery Checklist

If the terminal is broken after a run:

1. Run `stty sane` in the shell as immediate recovery.
2. Check for exits after `initscr()`/`newterm()` that bypass `endwin()`.
3. Check prompt code that enables `echo()`, hides the cursor, or changes
   blocking/timeouts without restoring them.
4. Check signal/error paths. Signal handlers should request cleanup through
   ordinary control flow instead of calling complex curses APIs directly.
5. Check for mixed stdout/stderr writes while curses owns the terminal.
