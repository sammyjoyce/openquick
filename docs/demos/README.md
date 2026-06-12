# Demo Gallery

Animated demonstrations of the CLI. The `.gif` files are not checked in. Build the project and run `./scripts/create-demo.sh` to produce them locally, then uncomment the image lines below.

## Demos

### Basic usage

<!-- ![Basic usage](basic-usage.gif) -->

Help, version, scaffold, dry-run deploy planning, and URL resolution.

```bash
quick --help
quick --version
quick init /tmp/openquick-demo-site --name demo-site
cd /tmp/openquick-demo-site
quick deploy --dry-run
quick open --plain
```

### Human and JSON output

<!-- ![Output formats](json-output.gif) -->

The same command in human-readable and `--json` form.

```bash
quick info
quick --json info
```

### Diagnostics

<!-- ![Diagnostics](doctor.gif) -->

`doctor` reports environment status. The current implementation is informational and always exits `0`; use `--json` and parse the checks if you need a hard gate.

```bash
quick doctor
quick --json doctor
```

### OpenCLI contract

<!-- ![Contract](contract.gif) -->

The machine-readable CLI contract the binary prints on demand.

```bash
quick opencli
```

### Error handling

<!-- ![Error handling](error-handling.gif) -->

An unknown command (exit 2) and an unknown option (exit 7).

```bash
quick frobnicate
quick --unknown-option
```

## Recording the interactive TUI

The product dashboard needs a real terminal and live input, so record it by hand rather than through the script:

```bash
zig build run                                    # try it first; bare quick opens the dashboard on a TTY
asciinema rec docs/demos/recordings/menu.cast     # then record a session
# drive Sites/Deploy/New site/Doctor/Serve/Settings, choose Exit (or q then y), then Ctrl-D
agg docs/demos/recordings/menu.cast docs/demos/menu.gif
```

## Generating the GIFs

```bash
# Dependencies:
#   asciinema  - install with your OS package manager
#   agg        - cargo install --git https://github.com/asciinema/agg

zig build
./scripts/create-demo.sh
```

The script records each non-interactive demo above and writes the GIFs into this directory.

## Embedding

```markdown
![Demo name](docs/demos/demo-name.gif)
```
