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

stage=
tmp=$(mktemp "${TMPDIR:-/tmp}/openquick-deploy.XXXXXX.zip")
cleanup() {
  rm -f "$tmp"
  if [ -n "$stage" ]; then
    rm -rf "$stage"
  fi
}
trap cleanup EXIT HUP INT TERM
rm -f "$tmp"
stage=$(mktemp -d "${TMPDIR:-/tmp}/openquick-deploy-stage.XXXXXX")

mkdir -p "$(dirname "$out")"
cp -R "$src_dir/." "$stage/"
(
  cd "$stage"
  TZ=UTC
  LC_ALL=C
  export TZ LC_ALL
  find SKILL.md references -exec touch -t 198001010000 {} +
  find SKILL.md references -print | sort | zip -X -q "$tmp" -@
)

mv "$tmp" "$out"
cleanup
trap - EXIT HUP INT TERM
