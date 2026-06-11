#!/bin/bash
# create-demo.sh - Record animated demos of the CLI
# Requires: asciinema, agg (asciinema gif generator)
#
# This records the non-interactive demos shown in docs/demos/README.md.
# The gallery README is maintained by hand, not regenerated here. The TUI
# `menu` is interactive, so record it manually (see docs/demos/README.md).

set -euo pipefail

# Configuration
DEMO_DIR="docs/demos"
BINARY="./zig-out/bin/myapp"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Ensure dependencies are installed
check_dependencies() {
    local deps=("asciinema" "agg")
    local missing=()

    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            missing+=("$dep")
        fi
    done

    if [ ${#missing[@]} -ne 0 ]; then
        echo -e "${RED}Missing dependencies: ${missing[*]}${NC}"
        echo "Install with:"
        echo "  install asciinema with your OS package manager"
        echo "  cargo install --git https://github.com/asciinema/agg"
        exit 1
    fi
}

# Build the project
build_project() {
    echo -e "${BLUE}Building project...${NC}"
    zig build

    if [ ! -f "$BINARY" ]; then
        echo -e "${RED}Binary not found at $BINARY${NC}"
        exit 1
    fi
}

# Create demo directory
setup_demo_dir() {
    mkdir -p "$DEMO_DIR"
    mkdir -p "$DEMO_DIR/recordings"
}

# Record a demo
record_demo() {
    local name="$1"
    local title="$2"
    local script="$3"

    echo -e "${BLUE}Recording demo: $title${NC}"

    # Create a temporary script file
    local script_file="/tmp/demo-$name.sh"
    cat > "$script_file" << EOF
#!/bin/bash
# Demo: $title

# Clear screen and show title
clear
echo -e "${GREEN}Demo: $title${NC}"
echo ""
sleep 2

# Run the demo commands
$script

# Show completion
echo ""
echo -e "${GREEN}Demo complete!${NC}"
sleep 2
EOF

    chmod +x "$script_file"

    # Record with asciinema
    asciinema rec \
        --title "$title" \
        --command "$script_file" \
        --overwrite \
        "$DEMO_DIR/recordings/$name.cast"

    # Convert to GIF
    echo -e "${BLUE}Converting to GIF...${NC}"
    agg \
        --theme monokai \
        --font-size 14 \
        --line-height 1.4 \
        "$DEMO_DIR/recordings/$name.cast" \
        "$DEMO_DIR/$name.gif"

    # Clean up
    rm -f "$script_file"

    echo -e "${GREEN}Created $DEMO_DIR/$name.gif${NC}"
}

# Demo 1: Basic usage
demo_basic_usage() {
    record_demo "basic-usage" "Basic Command Usage" '
'$BINARY' --help
sleep 3
'$BINARY' --version
sleep 2
'$BINARY' hello
sleep 2
'$BINARY' hello "Demo User"
sleep 2
'$BINARY' echo "This is a test message"
sleep 2
'
}

# Demo 2: Human and JSON output
demo_json_output() {
    record_demo "json-output" "Human and JSON Output" '
'$BINARY' info
sleep 3
'$BINARY' --json info
sleep 3
'
}

# Demo 3: Diagnostics
demo_doctor() {
    record_demo "doctor" "Diagnostics" '
'$BINARY' doctor
sleep 3
'$BINARY' --json doctor
sleep 3
'
}

# Demo 4: OpenCLI contract
demo_contract() {
    record_demo "contract" "OpenCLI Contract" '
'$BINARY' opencli | head -n 24
sleep 4
'
}

# Demo 5: Error handling
demo_error_handling() {
    record_demo "error-handling" "Error Handling" '
'$BINARY' frobnicate || true
sleep 3
'$BINARY' --unknown-option || true
sleep 2
'
}

# Main execution
main() {
    echo -e "${GREEN}Creating CLI demos...${NC}"

    check_dependencies
    build_project
    setup_demo_dir

    demo_basic_usage
    demo_json_output
    demo_doctor
    demo_contract
    demo_error_handling

    echo -e "${GREEN}All demos created.${NC}"
    echo -e "View them in: ${BLUE}$DEMO_DIR/${NC}"
    echo -e "The gallery is documented by hand in ${BLUE}$DEMO_DIR/README.md${NC}."
}

# Run main function
main "$@"
