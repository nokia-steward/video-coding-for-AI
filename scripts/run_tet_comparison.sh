#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Thin entrypoint that forwards to the refactored framework implementation.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FRAMEWORK_MAIN="${ROOT_DIR}/scripts/tet_comparison_framework/lib/run_tet_comparison_main.sh"

if [ ! -f "$FRAMEWORK_MAIN" ]; then
    echo "ERROR: Missing TET framework entry script: $FRAMEWORK_MAIN" >&2
    exit 1
fi

exec bash "$FRAMEWORK_MAIN" "$@"
