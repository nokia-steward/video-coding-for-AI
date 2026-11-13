#!/usr/bin/env bash
# Generate appendix inventory pages from committed tree (run from repo root).
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

PATCHES_OUT="docs/appendix/e-patch-inventory.md"
SCRIPTS_OUT="docs/appendix/e-scripts-inventory.md"

{
  echo "# Appendix E1 — Patch file inventory"
  echo ""
  echo "Generated from \`git ls-files patches/\`. Regenerate: \`./docs/generate_inventories.sh\`"
  echo ""
  echo "| File |"
  echo "|------|"
  git ls-files patches/ | while read -r f; do
    echo "| \`$f\` |"
  done
} > "$PATCHES_OUT"

{
  echo "# Appendix E2 — Scripts file inventory"
  echo ""
  echo "Generated from \`git ls-files scripts/\`. Regenerate: \`./docs/generate_inventories.sh\`"
  echo ""
  echo "| File |"
  echo "|------|"
  git ls-files scripts/ | while read -r f; do
    echo "| \`$f\` |"
  done
} > "$SCRIPTS_OUT"

echo "Wrote $PATCHES_OUT and $SCRIPTS_OUT"
