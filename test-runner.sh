#!/usr/bin/env bash
set -euo pipefail

zig build test
zig build terminal-test
