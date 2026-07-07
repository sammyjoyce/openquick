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

required_files="
SKILL.md
references/host-setup.md
references/quick-sdk.md
references/site-management.md
references/troubleshooting.md
"

for file in $required_files; do
  if [ ! -f "$src_dir/$file" ]; then
    echo "missing skill source file: $src_dir/$file" >&2
    exit 1
  fi
done

checklist_section=$(awk '
  /^## Agent deploy safety checklist$/ {
    in_checklist = 1
    print
    next
  }
  in_checklist && /^## / {
    exit
  }
  in_checklist {
    print
  }
' "$src_dir/SKILL.md")

if [ -z "$checklist_section" ]; then
  echo "missing agent deploy safety checklist in SKILL.md" >&2
  exit 1
fi
if ! printf '%s\n' "$checklist_section" | grep -q "quick deploy --dry-run"; then
  echo "skill checklist must require a dry-run before deploy" >&2
  exit 1
fi
if ! printf '%s\n' "$checklist_section" | grep -q "quick doctor --profile"; then
  echo "skill checklist must require targeted doctor checks" >&2
  exit 1
fi

tmp=$(mktemp "${TMPDIR:-/tmp}/openquick-deploy.XXXXXX.zip")
trap 'rm -f "$tmp"' EXIT HUP INT TERM
rm -f "$tmp"

mkdir -p "$(dirname "$out")"
(
  cd "$src_dir"
  zip -X -q -r "$tmp" SKILL.md references
)

mv "$tmp" "$out"
trap - EXIT HUP INT TERM
