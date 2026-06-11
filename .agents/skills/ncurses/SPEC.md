# Ncurses Specification

## Intent

This skill is the project-local authority for ncurses knowledge. It guides
agents that implement, debug, review, or explain curses terminal programs by
combining the requested C for Dummies learning material with official ncurses
documentation and practical terminal-state rules.

## Scope

In scope:

- Ncurses lifecycle, terminal state, screen model, refresh behavior, windows,
  pads, subwindows, text output, colors, attributes, ACS/Unicode drawing, input
  modes, keyboard events, mouse events, resizing, terminfo, and portability.
- C and curses-compatible implementation guidance, including ncursesw and
  PDCurses considerations.
- Debugging real terminal failures caused by `TERM`, locale, echo/raw modes,
  refresh ordering, color capability, resize events, or incompatible curses
  implementations.

Out of scope:

- Project-specific build-system rules.
- Repository wrapper APIs that are not part of ncurses itself.
- Copying tutorial source code into a project.
- GUI frameworks, terminal emulators, shells, or non-curses line editing
  libraries unless they directly affect ncurses behavior.

## Users And Trigger Context

- Primary users: coding agents working on curses terminal behavior.
- Common requests: add a curses UI, fix broken terminal cleanup, debug arrow
  keys, add colors, handle resize, review window refresh logic, port from curses
  to ncurses, or explain an ncurses API.
- Should not trigger for: ordinary command-line parsing, simple ANSI color
  printing, non-curses TUIs, or pull request description work with no ncurses
  content.

## Runtime Contract

- Required first actions: identify implementation, terminal capabilities, and
  the affected ncurses topic before editing or explaining.
- Required outputs: concrete ncurses decisions, validation performed, and any
  real-terminal behavior that remains unverified.
- Non-negotiable constraints: preserve terminal state, refresh deliberately,
  check capabilities before relying on colors/mouse/Unicode, and avoid copying
  tutorial examples without explicit license handling.
- Expected runtime files: `SKILL.md` plus one or more focused files in
  `references/` when the task needs detail.

## Source And Evidence Model

Authoritative sources:

- Current ncurses man pages and project documentation from Invisible Island.
- Local source code and observed runtime behavior when debugging a specific
  project.

Learning sources:

- C for Dummies ncurses overview, source index, examples, and tables.

Useful improvement sources:

- Positive examples: fixes that restore terminal state, redraw correctly,
  handle keys as `int`, and degrade across terminals.
- Negative examples: failures that leave echo disabled, skip `endwin`, assume
  colors, misuse refresh, or treat escape sequences as portable key handling.
- Validation results: real-terminal runs, terminfo inspection, and targeted
  compile/link checks.

Data that must not be stored:

- Secrets.
- Customer data.
- Private URLs or identifiers not needed for reproduction.

## Reference Architecture

- `SKILL.md` contains the authority boundary, core rules, router, fast decision
  table, and verification contract.
- `references/lifecycle-screen-model.md` contains startup, cleanup, refresh,
  resize, and screen-state rules.
- `references/input-modes-keys.md` contains input mode, keyboard, line input,
  timeout, escape, and mouse guidance.
- `references/rendering-windows-colors.md` contains output, window, pad, color,
  attribute, ACS, Unicode, and refresh guidance.
- `references/troubleshooting-portability.md` contains failure diagnosis and
  portability workarounds.
- `agents/openai.yaml` contains skill-creator UI metadata for skill lists and
  default invocation.
- `SOURCES.md` contains provenance, decisions, coverage, and changelog.

## Validation

- Lightweight validation: run the skill structural validator and `git diff
  --check` after skill edits.
- Runtime validation for future code changes: compile or run through the
  project build, inspect `TERM`/terminfo when behavior is terminal-dependent,
  and perform a real-terminal pass for input, resize, mouse, redraw, and cleanup
  changes.
- Acceptance gates: valid frontmatter, skill directory name matches `name`, all
  referenced files resolve, runtime references are directly routed from
  `SKILL.md`, and no runtime guidance depends on a project-specific build
  system.

## Known Limitations

- C for Dummies examples are learning material, not complete reference
  semantics.
- Non-interactive CI cannot fully prove keyboard timing, terminal cleanup,
  mouse reporting, resize handling, or visual layout.
- Exact behavior can differ across ncurses, ncursesw, PDCurses, terminal
  emulators, and terminfo entries.

## Maintenance Notes

- Update `SKILL.md` when trigger scope, authority boundary, or core rules
  change.
- Update focused references when an ncurses topic gains new recurring guidance.
- Update `SOURCES.md` when source provenance, official documentation, or
  decisions change.
