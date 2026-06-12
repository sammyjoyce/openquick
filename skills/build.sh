#!/bin/sh
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
src_dir="$script_dir/openquick-deploy"
out=${1:-"$script_dir/openquick-deploy.skill"}

case "$out" in
  /*) ;;
  *) out="$(pwd)/$out" ;;
esac

if ! command -v zip >/dev/null 2>&1; then
  echo "zip command not found" >&2
  exit 127
fi

if [ ! -f "$src_dir/SKILL.md" ] || [ ! -f "$src_dir/references/quick-sdk.md" ]; then
  echo "missing skill source files in $src_dir" >&2
  exit 1
fi

tmp=$(mktemp "${TMPDIR:-/tmp}/openquick-deploy.XXXXXX.zip")
trap 'rm -f "$tmp"' EXIT HUP INT TERM
rm -f "$tmp"

mkdir -p "$(dirname "$out")"
(
  cd "$src_dir"
  zip -X -q -r "$tmp" SKILL.md references/quick-sdk.md
)

mv "$tmp" "$out"
trap - EXIT HUP INT TERM
