#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Host-only setup orchestration. Run from repo root: ./scripts/setup/bootstrap.sh <command>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN="$SCRIPT_DIR/bin"
LIB_YOLO="$SCRIPT_DIR/../yolo"

run_core_setup() {
    bash "$BIN/setup_onnxruntime.sh"

    if [ "${SKIP_YOLO:-0}" = "1" ]; then
        echo "[bootstrap] SKIP_YOLO=1 — skipping DeepStream-Yolo build."
        return 0
    fi

    # shellcheck source=/dev/null
    source "$LIB_YOLO/common_paths.sh"
    if [ -n "$(find_deepstream_includes_dir)" ]; then
        bash "$BIN/setup_yolo.sh"
        return 0
    fi

    echo "[bootstrap] DeepStream SDK dev headers not found — skipping DeepStream-Yolo build."
    echo "[bootstrap] ONNX Runtime setup finished. Optional codecs (no DeepStream):"
    echo "              ./scripts/setup/bootstrap.sh kvazaar"
    echo "              ./scripts/setup/bootstrap.sh openhevc"
    echo "[bootstrap] After DeepStream install: ./scripts/setup/bootstrap.sh yolo"
    echo "[bootstrap] Diagnose: ./scripts/setup/bin/diagnose_deepstream.sh"
}

usage() {
    echo "Usage: $0 <command>"
    echo "Commands:"
    echo "  doctor    - Read-only package check (dpkg)"
    echo "  nvidia    - Host NVIDIA driver stack + DeepStream 7.1 (sudo; see INSTALL.md Step 2)"
    echo "  core      - ONNX Runtime; DeepStream-Yolo only if DeepStream SDK is present"
    echo "  ds-env    - DeepStream YOLO export venv"
    echo "  yolo      - DeepStream-Yolo parser build"
    echo "  kvazaar   - Install kvazaar + gst-kvazaar into lib/kvazaar"
    echo "  openhevc  - Install OpenHEVC + gst-openhevc into lib/openhevc"
    echo "  all       - doctor, core, kvazaar, openhevc, ds-env (stops on first failure)"
    exit 1
}

cd "$REPO_ROOT"

cmd="${1:-}"
case "$cmd" in
    doctor)
        bash "$BIN/doctor_packages.sh"
        ;;
    nvidia)
        # Preserve install paths across sudo (plain sudo drops DEEPSTREAM_DEB from the environment).
        nv_env=()
        for var in DEEPSTREAM_DEB DEEPSTREAM_TARBALL TENSORRT_LOCAL_DEB NGC_API_KEY \
            SKIP_CUDA SKIP_TENSORRT SKIP_DEEPSTREAM DEEPSTREAM_DOWNLOAD_DIR; do
            if [ -n "${!var:-}" ]; then
                nv_env+=("${var}=${!var}")
            fi
        done
        if [ "$(id -u)" -ne 0 ]; then
            if [ "${#nv_env[@]}" -gt 0 ]; then
                exec sudo env "${nv_env[@]}" bash "$BIN/install_nvidia_deepstream.sh" "${@:2}"
            else
                exec sudo bash "$BIN/install_nvidia_deepstream.sh" "${@:2}"
            fi
        else
            bash "$BIN/install_nvidia_deepstream.sh" "${@:2}"
        fi
        ;;
    core)
        run_core_setup
        ;;
    ds-env)
        NON_INTERACTIVE=1 bash "$BIN/setup_ds_export_env.sh"
        ;;
    yolo)
        bash "$BIN/setup_yolo.sh"
        ;;
    kvazaar)
        bash "$BIN/install_kvazaar.sh"
        ;;
    openhevc)
        bash "$BIN/install_openhevc.sh"
        ;;
    all)
        bash "$BIN/doctor_packages.sh"
        run_core_setup
        bash "$BIN/install_kvazaar.sh"
        bash "$BIN/install_openhevc.sh"
        NON_INTERACTIVE=1 bash "$BIN/setup_ds_export_env.sh"
        ;;
    *)
        usage
        ;;
esac
