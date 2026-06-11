# Demo Gallery

Animated demonstrations of the CLI. The `.gif` files are not checked in. Build the project and run `./scripts/create-demo.sh` to produce them locally, then uncomment the image lines below.

## Demos

### Basic usage

<!-- ![Basic usage](basic-usage.gif) -->

Help, version, and the `hello` / `echo` commands.

```bash
myapp --help
myapp --version
myapp hello
myapp hello "Demo User"
myapp echo "This is a test message"
```

### Human and JSON output

<!-- ![Output formats](json-output.gif) -->

The same command in human-readable and `--json` form.

```bash
myapp info
myapp --json info
```

### Diagnostics

<!-- ![Diagnostics](doctor.gif) -->

`doctor` reports environment status. The current implementation is informational and always exits `0`; use `--json` and parse the checks if you need a hard gate.

```bash
myapp doctor
myapp --json doctor
```

### OpenCLI contract

<!-- ![Contract](contract.gif) -->

The machine-readable CLI contract the binary prints on demand.

```bash
myapp opencli
```

### Error handling

<!-- ![Error handling](error-handling.gif) -->

An unknown command (exit 2) and an unknown option (exit 7).

```bash
myapp frobnicate
myapp --unknown-option
```

## Recording the interactive menu

The TUI menu needs a real terminal and live input, so record it by hand rather than through the script:

```bash
zig build run                                    # try it first; opens the TUI on a TTY
asciinema rec docs/demos/recordings/menu.cast     # then record a session
# drive the menu, press q to quit, then Ctrl-D to stop recording
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
