# Public Contracts

OpenQuick has a small set of stability promises for automation and internal
extension points. Everything else (help wording, colors, private helpers) can
change without notice. The contracts:

- the **CLI contract** — the machine-readable `opencli.json` ([CLI contract](#cli-contract));
- the **component/rendering contract** — the vendored `cs_` theming, surface, and
  component APIs used by CLI/TUI output ([Component contract](#component-contract));
- the **TUI menu primitive** — the reusable `tui-menu` library ([TUI menu contract](#tui-menu-contract)).

Keep OpenQuick workflow policy in CLI/server code; keep reusable drawing
mechanics in the `cs_` and `tui-menu` seams.

## Component contract

The internal component surface is the `cs_` namespace plus the local component
registry. Components are vendored source, so the promise is about the *shape* of
the surfaces OpenQuick calls, not a binary ABI.

**Supported (stable to depend on):**

- the **theming API** in `cs_theme.h`: the `cs_role_t` role aliases, `cs_theme_t`,
  and the functions `cs_theme_default`, `cs_theme_by_name`, `cs_theme_names`,
  `cs_theme_mode_resolve`, `cs_theme_set_role`, `cs_theme_set_role_spec`,
  `cs_theme_resolve`. The default theme is the amber identity; `APP_CLI_THEME` /
  `APP_CLI_ACCENT` behave as documented in [THEMING.md](THEMING.md).
- the **surface API** in `surface.h`: `cs_surface_stream_new`,
  `cs_surface_curses_new` (under `ENABLE_TUI`), `cs_surface_free`, the
  introspection calls (`cs_surface_caps`/`width`/`theme`), and the drawing calls
  (`set_role`/`set_role_bg`/`set_attr`/`reset`/`write`/`write_n`/`repeat`/
  `newline`/`move`/`styled`). A surface borrows its `FILE *` / window and copies
  its theme.
- the **component render contract**: each `cs_<name>_t` props struct borrows its
  pointers (copies nothing, so they must outlive the call); `cs_<name>_render`
  (and `cs_spinner_render`'s frame index) is the entry point; a `0`/`NULL` role or
  size means "use the default"; `render(NULL, …)` / `render(…, NULL)` are no-ops.
- the **registry manifest** schema (`registry/registry.json`): the per-entry
  `name`, `category`, `surfaces`, `files`, `dependencies`, and `since` fields.
  `zig build test` validates the manifest, so it never references a missing file
  or an unresolved dependency.
- the component umbrella version macros for feature-detecting the vendored
  component surface across updates.

**Private (may change without notice):**

- the exact glyphs, spacing, and color choices a component renders (style, not
  contract) — pin specific output in your own tests if you depend on it;
- the internal `app_ui_*` role implementation behind the `cs_` aliases, the
  surface's vtable and struct internals, and a component's private helpers;
- the set of built-in theme names beyond `amber` (the default) and `mono`;
- the registry's transport (it is local-first; there is no network/remote-fetch
  promise).

After you copy a component in, it is *your* code — edit it freely. The contract
covers the catalog you copy *from*, not your fork.

## CLI contract

`opencli.json` is the checked-in CLI contract, and the binary prints the same document:

```bash
quick opencli
```

`zig build test` fails when `quick opencli` and `opencli.json` drift, so command and flag metadata must change in the C tables *before* the spec does. The canonical sources:

| Source | Owns |
| --- | --- |
| `src/cli/opencli_contract.c` | OpenCLI info, conventions, root command arguments, extra examples, and metadata |
| `src/cli/commands.c` | `--help`/`--version` metadata, command names and summaries, global value options such as `--config`, command arguments and options, examples, terminal requirements |
| `src/core/config.c` | Global flag metadata |
| `src/core/error.c` | Public exit codes and descriptions |

After editing those tables, regenerate the artifact and re-run the tests:

```bash
zig build run -- opencli > opencli.json
zig build test
```

**Supported (stable to depend on):**

- global options before the command
- command names, arguments, options, and examples in `opencli.json`
- public exit codes in `opencli.json`
- `--json` responses that include `format_version`
- JSON output as the default whenever stdout is not a terminal; pass `--plain`
  before the command to keep human text under pipes or redirection
- `quick opencli` and `quick --json opencli` both write the same schema document because the contract is already JSON
- stdout for command output, stderr for errors and diagnostics
- config precedence: CLI args > environment > config file > defaults

## Dispatch modes

The binary chooses its front-end from argv shape and terminal attachment:

| Invocation | Condition | Behavior |
| --- | --- | --- |
| `quick` | stdin and stdout are TTYs | Launch the interactive TUI menu. |
| `quick <command> ...` | any terminal shape | Dispatch the named CLI command through the command table. |
| `quick` | no interactive TTY | Read a headless JSON request object from stdin and dispatch it. |

`--help` and `--version` are immediate-exit flags and keep their human-readable
stdout behavior even under pipes.

### Headless JSON request protocol

Bare non-TTY invocation is an experimental, versioned machine surface for agents
and CI wrappers that prefer stdin/stdout over shell-token construction. The
request shape is:

```json
{
  "command": "doctor",
  "args": [],
  "flags": {
    "debug": false,
    "quiet": false,
    "verbose": false,
    "json_output": true,
    "plain_output": false,
    "no_color": false
  }
}
```

Only `command` is required. `args` defaults to an empty array. `flags` uses the
same JSON keys as config files and currently accepts booleans only. Unknown flag
names are rejected. String values accept RFC 8259 escapes, including `\uXXXX`
(with UTF-16 surrogate pairs), and are decoded to UTF-8; an escaped NUL
(`\u0000`) is rejected because it cannot be represented in the C-string
arguments.

Responses are whatever the dispatched command writes in JSON mode and therefore
include the same `format_version` convention as `--json` command output. Parse
and dispatch errors are written to stderr as JSON messages and use the public
exit codes from `src/core/error.c`. Empty stdin is a `APP_ERROR_MISSING_ARG`
failure.

**Private (may change without notice):**

- exact help text, example prose, spacing, and colors
- log wording and timing
- internal `app_error` values not listed as public exit codes
- source layout and private helper functions

## TUI menu contract

`zig build tui-menu-lib` builds the narrow reusable primitive and installs:

```text
zig-out/lib/libtui-menu.a
zig-out/lib/pkgconfig/tui-menu.pc
zig-out/include/openquick/core/error.h
zig-out/include/openquick/core/types.h
zig-out/include/openquick/tui/tui.h
zig-out/include/openquick/tui/tui_menu.h
zig-out/include/openquick/tui/tui_progress.h
```

**Supported:**

- the `tui_init()` / `tui_cleanup()` lifecycle from `tui.h`
- `tui_set_color_enabled()` to set the color policy before `tui_init()` — the
  generated app passes `app_use_colors(config)` so the TUI honours the same
  `NO_COLOR` / `FORCE_COLOR` / `CLICOLOR(_FORCE)` / `APP_CLI_COLOR=never` /
  `--no-color` / `--plain` inputs as the CLI; left unset, the TUI falls back
  to terminal capability alone
- `tui_show_menu()` from `tui_menu.h`
- `tui_menu_config_t`, `tui_menu_item_t`, and `tui_menu_result_t`
- the pointer-lifetime rules documented in `tui_menu.h`
- separators, disabled items, mnemonics, search, numeric jumps, resize handling, and interrupt handling
- the `TUI_MENU_VERSION` compile-time macro (with `TUI_MENU_VERSION_ENCODE`) for feature-detecting the seam across component updates

The archive is self-contained: it bundles the shared UI primitives the menu
needs (text layout, color math, design tokens, and semantic UI roles —
`text_layout.c`, `color_math.c`, `design_tokens.c`, `ui_theme.c`), so linking it
requires only a curses library. The archive is also self-contained for a foreign toolchain — it
is compiled with `sanitize_c = .trap`, so its UBSan checks lower to inline traps
instead of calls into Zig's UBSan runtime, and a plain `cc` link resolves with
no `__ubsan_handle_*` symbols even from a Debug/ReleaseSafe build.

The install ships a pkg-config manifest (`tui-menu.pc`, version tracking
`TUI_MENU_VERSION`). Link a consumer either explicitly or via pkg-config:

```bash
# explicit
cc app.c -Izig-out/include \
   -Lzig-out/lib -ltui-menu \
   $(pkg-config --libs ncursesw) -o app

# via the installed .pc (use --static to pull in -lncursesw)
PKG_CONFIG_PATH=zig-out/lib/pkgconfig \
cc app.c $(pkg-config --cflags tui-menu) \
   $(pkg-config --libs --static tui-menu) -o app
```

**Private:**

- `tui_menu_internal.h` and `tui_menu_state_t` internals
- cell-by-cell rendering details
- exact colors, palette slots, and color-pair mappings (the CLI and generated
  TUI intentionally share the same env policy and semantic roles, but not a
  stable color ABI)
- exact footer/help text inside the alternate screen
- terminal-test snapshots, except where a test names a specific invariant

## Not yet

The component catalog is vendored source, not a runtime plugin system. A few
things are out of scope until a real need appears:

- **No binary ABI / plugin system.** Distribution is open code via the registry,
  not a loadable interface. There is no stable ABI promise beyond the source-level
  `cs_` shapes above and the `tui-menu` archive; prefer copying a component and
  editing it over a dynamic extension seam.
- **No network registry.** The registry is local-first (`registry/registry.json`
  in this repo); there is no remote component fetch or third-party registry
  protocol.
- **No new runtime CLI commands or flags** that bypass the byte-pinned
  `opencli.json` contract — reusable rendering belongs in the `cs_` API, not in
  ad-hoc subcommands.
- **No long-running headless service** and **no nested-subcommand engine** until
  multiple projects need the same behavior.

## See also

- [COMPONENTS.md](COMPONENTS.md) - the component catalog and the render surface
- [THEMING.md](THEMING.md) - the theming API and color degradation
- [ARCHITECTURE.md](ARCHITECTURE.md) - where these seams sit in the codebase
- [TESTING.md](TESTING.md) - the tests that enforce the CLI contract
