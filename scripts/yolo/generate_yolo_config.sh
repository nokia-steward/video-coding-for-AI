#!/bin/bash

# Shared script to generate model-specific YOLO YAML configuration files under yolo/.
#
# Generates:
# - yolo/yolo_infer_config_<ver>.yml
# - yolo/yolo_config_<ver>.yml
#
# Usage:
#   ./scripts/yolo/generate_yolo_config.sh <video_width> <video_height> [yolo_version]
#
# Example:
#   ./scripts/yolo/generate_yolo_config.sh 640 480 11m

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/common_paths.sh"

VIDEO_WIDTH="${1:-832}"
VIDEO_HEIGHT="${2:-480}"
YOLO_VERSION="${3:-8n}"

# YOLO dims must be multiple of 32
YOLO_WIDTH=$(( (VIDEO_WIDTH + 31) / 32 * 32 ))
YOLO_HEIGHT=$(( (VIDEO_HEIGHT + 31) / 32 * 32 ))

VCAI_ROOT="$(get_vcai_root "$SCRIPT_DIR")"
YOLO_DIR="$VCAI_ROOT/yolo"
mkdir -p "$YOLO_DIR"

INFER_CONFIG_FILE="$YOLO_DIR/yolo_infer_config_${YOLO_VERSION}.yml"
YAML_CONFIG_FILE="$YOLO_DIR/yolo_config_${YOLO_VERSION}.yml"

echo "Generating YOLO config:"
echo "  Repo root: $VCAI_ROOT"
echo "  Version: $YOLO_VERSION"
echo "  Video dims: ${VIDEO_WIDTH}x${VIDEO_HEIGHT}"
echo "  YOLO dims: ${YOLO_WIDTH}x${YOLO_HEIGHT}"
echo "  -> $INFER_CONFIG_FILE"
echo "  -> $YAML_CONFIG_FILE"

cat > "$INFER_CONFIG_FILE" <<EOF
################################################################################
# YOLO Inference Configuration (shared)
################################################################################

property:
  custom-lib-path: $VCAI_ROOT/yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo/libnvdsinfer_custom_impl_Yolo.so
  onnx-file: $VCAI_ROOT/yolo/yolov${YOLO_VERSION}_${YOLO_WIDTH}x${YOLO_HEIGHT}.onnx
  labelfile-path: $VCAI_ROOT/yolo/labels.txt
  model-engine-file: $VCAI_ROOT/yolo/yolov${YOLO_VERSION}_${YOLO_WIDTH}x${YOLO_HEIGHT}.onnx_b1_gpu0_fp16.engine

  # 0=FP32, 1=FP16, 2=INT8
  network-mode: 1
  num-detected-classes: 80
  gpu-id: 0
  batch-size: 1
  gie-unique-id: 1

  process-mode: 1
  net-scale-factor: 0.0039215697906911373
  model-color-format: 0
  infer-dims: 3;${YOLO_HEIGHT};${YOLO_WIDTH}
  uff-input-blob-name: images

  maintain-aspect-ratio: 1
  symmetric-padding: 1
  cluster-mode: 2
  parse-bbox-func-name: NvDsInferParseYolo

class-attrs-all:
  nms-iou-threshold: 0.3
  pre-cluster-threshold: 0.1
  topk: 300
EOF

cat > "$YAML_CONFIG_FILE" <<EOF
################################################################################
# YOLO Configuration (shared)
################################################################################

streammux:
  batch-size: 1
  batched-push-timeout: 40000
  width: $VIDEO_WIDTH
  height: $VIDEO_HEIGHT

primary-gie:
  plugin-type: 0
  config-file-path: $INFER_CONFIG_FILE
EOF

echo "✓ Generated YOLO config files"

