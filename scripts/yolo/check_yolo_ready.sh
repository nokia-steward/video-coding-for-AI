#!/bin/bash

# Validates that the repo's yolo/ directory contains the required assets
# (DeepStream-Yolo parser .so, labels, ONNX, config files).
#
# Usage:
#   ./scripts/yolo/check_yolo_ready.sh <video_width> <video_height> [yolo_version]
#
# Example:
#   ./scripts/yolo/check_yolo_ready.sh 640 480 11m

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/common_paths.sh"

VIDEO_WIDTH="${1:-}"
VIDEO_HEIGHT="${2:-}"
YOLO_VERSION="${3:-8n}"

if [ -z "$VIDEO_WIDTH" ] || [ -z "$VIDEO_HEIGHT" ]; then
    echo "Error: width and height are required."
    echo "Usage: $0 <video_width> <video_height> [yolo_version]"
    exit 1
fi

YOLO_WIDTH=$(( (VIDEO_WIDTH + 31) / 32 * 32 ))
YOLO_HEIGHT=$(( (VIDEO_HEIGHT + 31) / 32 * 32 ))

ROOT="$(get_vcai_root "$SCRIPT_DIR")"
YOLO_DIR="$ROOT/yolo"

YOLO_LIB="$YOLO_DIR/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo/libnvdsinfer_custom_impl_Yolo.so"
LABELS="$YOLO_DIR/labels.txt"
ONNX="$YOLO_DIR/yolov${YOLO_VERSION}_${YOLO_WIDTH}x${YOLO_HEIGHT}.onnx"
ENGINE="$YOLO_DIR/yolov${YOLO_VERSION}_${YOLO_WIDTH}x${YOLO_HEIGHT}.onnx_b1_gpu0_fp16.engine"
CFG="$YOLO_DIR/yolo_config_${YOLO_VERSION}.yml"
INFER_CFG="$YOLO_DIR/yolo_infer_config_${YOLO_VERSION}.yml"

echo "=== YOLO readiness check ==="
echo "Repo root: $ROOT"
echo "Version: $YOLO_VERSION"
echo "Video dims: ${VIDEO_WIDTH}x${VIDEO_HEIGHT} (YOLO: ${YOLO_WIDTH}x${YOLO_HEIGHT})"
echo

missing=0

check_file() {
    local path="$1"
    local label="$2"
    if [ -f "$path" ]; then
        echo "✓ $label: $path"
    else
        echo "✗ Missing $label: $path"
        missing=$((missing + 1))
    fi
}

check_file "$YOLO_LIB" "DeepStream-Yolo parser library"
check_file "$LABELS" "labels.txt"
check_file "$ONNX" "ONNX model"
check_file "$CFG" "DeepStream YAML config"
check_file "$INFER_CFG" "nvinfer YAML config"

if [ -f "$ENGINE" ]; then
    echo "✓ TensorRT engine (cached): $ENGINE"
else
    echo "ℹ TensorRT engine not found (will be generated on first run): $ENGINE"
fi

echo
if [ "$missing" -eq 0 ]; then
    echo "✓ YOLO setup looks ready."
    exit 0
fi

echo "Some required files are missing."
echo
echo "Suggested fix commands:"
echo "  $ROOT/scripts/yolo/setup_yolo.sh"
echo "  $ROOT/scripts/yolo/convert_yolo_model.sh $VIDEO_WIDTH $VIDEO_HEIGHT \"$ROOT/yolo\" $YOLO_VERSION"
echo "  $ROOT/scripts/yolo/generate_yolo_config.sh $VIDEO_WIDTH $VIDEO_HEIGHT $YOLO_VERSION"
exit 1

