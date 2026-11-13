#!/usr/bin/env bash
# Build the VCAI HTML manual (MkDocs Material).
# Usage: ./docs/build_manual.sh
# Output: manual/
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

if ! command -v mkdocs >/dev/null 2>&1; then
  echo "mkdocs not found. Install: pip install -r requirements-docs.txt" >&2
  exit 1
fi

# Refresh auto-generated appendix inventories from committed paths
./docs/generate_inventories.sh

# --strict fails on included upstream Markdown that uses relative .md links
# (resolved relative to docs/ path). Build without strict; fix upstream or use excerpts.
mkdocs build
echo "Manual built under manual/ — open: file://${REPO_ROOT}/manual/index.html (do not use mkdocs serve)"
