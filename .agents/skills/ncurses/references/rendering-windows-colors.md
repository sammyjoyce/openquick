# Rendering, Windows, And Colors

Open this when designing output, windows, pads, borders, attributes, colors,
line drawing, Unicode, or efficient repainting.

## Windows And Pads

- Use `stdscr` for simple full-screen output.
- Use `newwin()` when a screen region has its own border, refresh cycle, input
  owner, or lifetime.
- Use `subwin()`/`derwin()` when a child region should share storage with a
  parent. Remember that shared storage changes refresh and lifetime concerns.
- Use pads for content larger than the physical screen or viewport. Pads are
  not automatically refreshed with `refresh()`.
- Delete windows with `delwin()` when their lifetime ends. Delete children
  before parents when ownership is nested.

## Drawing Text

- Prefer window-specific functions (`waddnstr`, `mvwprintw`, `mvwaddnstr`) when
  drawing outside `stdscr`.
- Bound variable text to the available width. Curses will not turn an
  overflowing label into a good layout.
- Check return values where failed drawing matters; many curses calls can
  return `ERR` when coordinates are outside the window.
- After clearing or erasing a window, redraw borders and titles before content.
- Do not rely on `printf()` while curses owns the screen.

## Attributes And Line Drawing

- Use attributes (`A_BOLD`, `A_REVERSE`, `A_UNDERLINE`, and color pairs) as
  scoped drawing state.
- Pair `wattron()` with `wattroff()` or use attribute-set calls to prevent style
  leakage.
- Use ACS constants for portable line drawing when Unicode is uncertain.
- Use wide-character APIs and link against the wide-character curses library
  when true Unicode cells matter.
- Provide ASCII fallbacks when terminal, locale, or font support is unknown.

## Colors

- Call `has_colors()` after curses initialization and before relying on color.
- Call `start_color()` before `init_pair()`, `COLOR_PAIR()`, or color queries.
- Treat color pair 0 specially. Default-color behavior is implementation and
  extension dependent.
- Use default-color extensions such as `use_default_colors()` or
  `assume_default_colors()` only when targeting implementations that support
  them.
- Respect `COLORS` and `COLOR_PAIRS`; do not assume 256 colors or extended
  pairs.
- If color is unavailable, degrade to attributes, labels, or plain layout.

## Refresh Strategy

| Situation | Refresh approach |
| --- | --- |
| Simple `stdscr` app | Draw, then `refresh()`. |
| One independent window | Draw into the window, then `wrefresh(win)`. |
| Multiple overlapping regions | Draw each region, call `wnoutrefresh()` for each, then `doupdate()`. |
| Pad viewport | Draw into the pad, then `prefresh()` or `pnoutrefresh()` with viewport bounds. |
| Modal closes | Clear/delete it, touch/redraw exposed windows, then refresh the owner. |

## Layout Checklist

1. Read current dimensions at draw time.
2. Reserve border and title space before placing content.
3. Clip or wrap variable text.
4. Keep status/help lines within the window.
5. Redraw after resize, clear, or modal removal.
6. Test with a small terminal and a color-poor terminal description.
