#!/usr/bin/env python3
"""Validate that example sites include robust user-facing UI states."""
from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "examples" / "sites" / "states-demo" / "index.html"
QUICK_JSON = ROOT / "examples" / "sites" / "states-demo" / "quick.json"

REQUIRED_SNIPPETS = [
    'data-state="loading"',
    'data-state="empty"',
    'data-state="error"',
    'data-state="permission-denied"',
    'data-state="rate-limited"',
    'data-state="offline"',
    'role="status"',
    'role="alert"',
    'OpenQuickError',
    'unhandledrejection',
    'navigator.onLine',
    'try {',
    'catch (error)',
]


def main() -> int:
    missing_files = [str(path.relative_to(ROOT)) for path in (EXAMPLE, QUICK_JSON) if not path.exists()]
    if missing_files:
        print("missing example files: " + ", ".join(missing_files), file=sys.stderr)
        return 1
    text = EXAMPLE.read_text(encoding="utf-8")
    missing = [snippet for snippet in REQUIRED_SNIPPETS if snippet not in text]
    if missing:
        print("states example missing required snippets:", file=sys.stderr)
        for snippet in missing:
            print(f"  - {snippet}", file=sys.stderr)
        return 1
    forbidden = ["console.error(", "throw error", "throw new Error"]
    present = [snippet for snippet in forbidden if snippet in text]
    if present:
        print("states example should render failures instead of emitting console errors:", file=sys.stderr)
        for snippet in present:
            print(f"  - {snippet}", file=sys.stderr)
        return 1
    print("examples states demo ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
