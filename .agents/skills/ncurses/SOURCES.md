# Sources And Decisions

## Synthesis Summary

- Skill class: `integration-documentation`.
- Primary execution shape: `reference-backed-expert`.
- Secondary shapes: none.
- Simplicity rationale: the skill now aims to be an ncurses authority, so
  optional deep knowledge is the main complexity. Inline-only guidance was
  rejected because lifecycle, input, rendering, colors, and portability each
  need enough detail to be useful without making `SKILL.md` hard to scan.
- Portability note: the skill uses portable Agent Skills files and no
  provider-specific path variables.

## Source Inventory

| Source | Trust tier | Confidence | Contribution | Constraints |
| --- | --- | --- | --- | --- |
| https://c-for-dummies.com/ncurses/ | Requested learning source | Medium | Identified the ncurses learning hub and supporting source/table pages. | Copyrighted site; use as source map and learning material, not copied code. |
| https://c-for-dummies.com/ncurses/source_code/index.php | Requested learning source | Medium | Mapped topic coverage across initialization, colors, attributes, text, input, windows, pads, mouse, and persistence examples. | Individual examples are illustrative; do not copy into projects without license handling. |
| https://c-for-dummies.com/ncurses/tables/ | Requested learning source | Medium | Identified useful table categories for attributes, colors, ACS characters, special keys, and mouse constants. | Link and paraphrase only. |
| https://c-for-dummies.com/ncurses/source_code/02-01_skeleton.php | Requested learning example | Medium | Informed the lifecycle guidance around initialize, do work, and close curses. | Adapted conceptually rather than copied. |
| https://c-for-dummies.com/ncurses/source_code/08-04_secretkey.php | Requested learning example | Medium | Informed hidden input and `noecho()` guidance. | Adapted conceptually rather than copied. |
| https://c-for-dummies.com/ncurses/source_code/08-06_arrowkeys.php | Requested learning example | Medium | Informed `keypad()` and `KEY_*` guidance. | Adapted conceptually rather than copied. |
| https://invisible-island.net/ncurses/man/ncurses.3x.html | Official ncurses manual | High | Source for ncurses scope, function families, terminfo, windows, pads, color, input, and mouse behavior. | Prefer current official man pages for exact API behavior. |
| https://invisible-island.net/ncurses/man/curs_initscr.3x.html | Official ncurses manual | High | Source for initialization and cleanup semantics. | Prefer current official man page for edge cases. |
| https://invisible-island.net/ncurses/man/curs_getch.3x.html | Official ncurses manual | High | Source for keyboard input, `getch()` return type, function-key behavior, and `ERR`. | Prefer current official man page for edge cases. |
| https://invisible-island.net/ncurses/man/curs_color.3x.html | Official ncurses manual | High | Source for `has_colors()`, `start_color()`, color pairs, limits, and default-color extensions. | Prefer current official man page for edge cases. |
| https://invisible-island.net/ncurses/man/curs_variables.3x.html | Official ncurses manual | High | Source for `stdscr`, `curscr`, `newscr`, `COLS`, `LINES`, `COLORS`, and `COLOR_PAIRS`. | Prefer current official man page for edge cases. |
| https://invisible-island.net/ncurses/man/curs_pad.3x.html | Official ncurses manual | High | Source for pads and viewport refresh behavior. | Prefer current official man page for edge cases. |
| https://invisible-island.net/ncurses/ncurses-intro.html | Official project documentation | High | Source for program structure, refresh model, color-pair recommendations, mouse portability notes, and practical usage. | Use as explanatory support; man pages remain exact API references. |
| https://invisible-island.net/ncurses/ncurses.faq.html | Official project FAQ | High | Source for project background, terminal capability, color, and portability caveats. | Use for FAQ-level behavior and current project context. |

## Source Adaptation Notes

| Decision | Record |
| --- | --- |
| Source intent | C for Dummies teaches ncurses through small examples; Invisible Island documents exact ncurses behavior. |
| Local target | Produce a project-local skill that acts as the authority on ncurses behavior and routes to focused runtime references. |
| Fidelity boundary | Preserve ncurses semantics and tutorial topic coverage, not the tutorial's sample-code structure. |
| Local replacement | Remove project-specific wrapper and build guidance; express direct ncurses rules and topic references. |
| Omitted material | Screenshots, purchase/book metadata, full sample code bodies, and unrelated language bindings. |
| Rights and attribution | Keep links and paraphrase; do not copy copyrighted tutorial examples. |

## Coverage Matrix

| Dimension | Coverage |
| --- | --- |
| API surface and behavior contracts | Lifecycle, screen model, refresh, windows, pads, subwindows, text output, attributes, colors, input, mouse, resize, variables, terminfo, and cleanup. |
| Config/runtime options | `TERM`, terminfo, locale, interactive TTY, color capability, default-color extensions, `ESCDELAY` behavior, timeout modes, ncursesw, and PDCurses variance. |
| Common downstream use cases | Full-screen apps, menus, prompts, hidden input, keyboard shortcuts, modal windows, progress/status displays, scrollable viewports, color themes, mouse interaction, and resize-aware layouts. |
| Known issues and workarounds | Broken terminal state, echo left off, garbled arrows, delayed escape key, missing colors, black-background assumptions, stale redraws, crop on small terminals, bad `TERM`, missing terminfo, wide-character mismatch, mouse unsupported, and implementation differences. |
| Version/platform variance | ncurses versus ncursesw, PDCurses compatibility, terminfo differences, terminal emulator capabilities, color-pair limits, extended color support, mouse feature macros, and non-interactive CI limitations. |

## Decisions

| Status | Decision | Rationale |
| --- | --- | --- |
| Adopted | Rename skill root to `.agents/skills/ncurses/`. | The user requested an ncurses-focused authority and removal of the previous project-specific framing. |
| Adopted | Use a reference-backed expert layout. | Ncurses authority guidance needs focused optional depth without overloading `SKILL.md`. |
| Adopted | Use official ncurses manuals as authoritative sources. | C for Dummies is useful learning material, but exact behavior should come from ncurses documentation. |
| Adopted | Keep C for Dummies as learning-source provenance. | The original request explicitly asked to base the skill on that material. |
| Adopted | Add `agents/openai.yaml`. | `skill-creator` recommends UI metadata with display name, short description, and default prompt for created skills. |
| Rejected | Include project build-system rules. | The user asked to remove those references and make the skill about ncurses itself. |
| Rejected | Add scripts or validators beyond structural validation. | Ncurses behavior requires real terminal/runtime checks, not a deterministic skill script. |

## Description Optimization

Final description:

> Authoritative ncurses guidance for implementing, debugging, reviewing, or explaining curses terminal programs. Use for initscr/endwin lifecycle, windows, pads, refresh behavior, getch/keypad/input modes, colors, attributes, ACS or Unicode drawing, mouse events, resizing, terminfo, TERM, ncursesw, PDCurses, and curses portability issues.

Should trigger:

- "Fix ncurses leaving echo disabled after an error."
- "Why does getch not return KEY_UP?"
- "Add colors and attributes to this curses screen."
- "Debug a pad not repainting after resize."
- "Port this curses code to ncursesw or PDCurses."

Should not trigger:

- "Print ANSI colored text with printf."
- "Write normal CLI argument parsing."
- "Design a web dashboard."
- "Open a PR for unrelated docs."
- "Fix terminal shell startup with no curses code."

Edits made:

- Removed project-specific trigger terms.
- Added exact ncurses trigger vocabulary across lifecycle, input, rendering,
  colors, mouse, resizing, terminfo, and portability.

## Gaps And Stopping Rationale

- The focused references cover the core ncurses domains needed for an authority
  skill; adding one file per man page would be noisy and lower signal.
- Exact edge cases should still be checked against current official man pages
  during implementation.
- The C for Dummies GitHub mirror did not establish a license during the first
  pass, so source examples remain learning references only.

## Changelog

- 2026-05-26: Reworked the initial skill into an ncurses-focused authority,
  renamed the skill to `ncurses`, removed project-specific build/wrapper rules,
  and added focused runtime references.
