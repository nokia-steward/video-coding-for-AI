#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Wrapper maintained for backward compatibility.
# The implementation lives in the shared scripts folder: scripts/yolo/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VCAI_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

exec "$VCAI_ROOT/scripts/yolo/generate_yolo_config.sh" "$@"
