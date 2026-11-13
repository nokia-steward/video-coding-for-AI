#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Canonical entry for YOLO readiness check; implementation in scripts/yolo/
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VCAI_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
exec "$VCAI_ROOT/scripts/yolo/check_yolo_ready.sh" "$@"
