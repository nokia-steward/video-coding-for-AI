#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Wrapper maintained for backward compatibility.
# The implementation lives in the shared scripts folder: scripts/yolo/common_paths.sh
#
# This file is frequently sourced by other scripts; keep function names stable.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VCAI_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# shellcheck source=/dev/null
source "$VCAI_ROOT/scripts/yolo/common_paths.sh"

detect_and_set_cuda_version() {
    echo "Detecting CUDA version..."
    detect_and_export_cuda_vars
    echo "CUDA version: ${CUDA_VER}"
    echo "CUDA architecture: ${CUDA_ARCH_BIN}"
}

get_cuda_library_paths() {
    # Retained for compatibility; best-effort list.
    local cuda_version="${1:-${CUDA_VER:-}}"
    local major_version
    major_version="$(echo "$cuda_version" | cut -d. -f1 || true)"
    local candidates=(
        "/usr/local/cuda-${cuda_version}/lib64"
        "/usr/local/cuda-${major_version}/lib64"
        "/usr/local/cuda/lib64"
        "/opt/cuda/lib64"
        "/usr/lib/x86_64-linux-gnu"
    )
    for path in "${candidates[@]}"; do
        if [ -d "$path" ]; then
            echo "$path"
        fi
    done
}

if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    detect_and_set_cuda_version
    echo ""
    echo "CUDA library paths:"
    get_cuda_library_paths "${CUDA_VER:-}"
fi
