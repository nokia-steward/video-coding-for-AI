#!/bin/bash

# Shared script to export a YOLO model to DeepStream-compatible ONNX (static dims).
# Adapted from encoder_cli/scripts/convert_yolo_model.sh but made repo-root relative.
#
# Usage:
#   ./scripts/yolo/convert_yolo_model.sh <width> <height> <output_directory> [version]
#
# Example:
#   ./scripts/yolo/convert_yolo_model.sh 640 480 ./yolo 11m

set -euo pipefail

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "Error: Invalid number of arguments."
    echo "Usage: $0 <width> <height> <output_directory> [version]"
    echo "Example: $0 640 480 ./yolo 11m"
    exit 1
fi

WIDTH="$1"
HEIGHT="$2"
OUTPUT_DIR="$3"
YOLO_VERSION="${4:-8n}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/common_paths.sh"

VCAI_ROOT="$(get_vcai_root "$SCRIPT_DIR")"

VERSION_NUM="$(echo "$YOLO_VERSION" | sed 's/[^0-9]//g')"
VERSION_SIZE="$(echo "$YOLO_VERSION" | sed 's/[0-9]//g')"
if [ -z "$VERSION_NUM" ] || ! [[ "$VERSION_NUM" =~ ^[0-9]+$ ]]; then
    echo "Error: Invalid version format: $YOLO_VERSION"
    exit 1
fi
if [ -z "$VERSION_SIZE" ]; then
    VERSION_SIZE="n"
fi
if [ "$VERSION_NUM" -lt 5 ] || [ "$VERSION_NUM" -gt 11 ]; then
    echo "Error: Unsupported YOLO version: $VERSION_NUM"
    exit 1
fi

# YOLO models require dims multiple of 32
YOLO_WIDTH=$(( (WIDTH + 31) / 32 * 32 ))
YOLO_HEIGHT=$(( (HEIGHT + 31) / 32 * 32 ))

MODEL_NAME="yolov${VERSION_NUM}${VERSION_SIZE}"
ULTRALYTICS_MODEL_NAME="$MODEL_NAME"
if [ "$VERSION_NUM" -eq 11 ]; then
    # ultralytics naming for v11 uses yolo11*
    ULTRALYTICS_MODEL_NAME="yolo${VERSION_NUM}${VERSION_SIZE}"
fi

OUTPUT_FILENAME="${MODEL_NAME}_${YOLO_WIDTH}x${YOLO_HEIGHT}.onnx"
FULL_OUTPUT_PATH="${OUTPUT_DIR}/${OUTPUT_FILENAME}"

echo "=== YOLO export to ONNX ==="
echo "Repo root: $VCAI_ROOT"
echo "Model: $MODEL_NAME (ultralytics: $ULTRALYTICS_MODEL_NAME)"
echo "Target dims: ${WIDTH}x${HEIGHT} (YOLO: ${YOLO_WIDTH}x${YOLO_HEIGHT})"
echo "Output: $FULL_OUTPUT_PATH"

if [ -f "$FULL_OUTPUT_PATH" ]; then
    echo "Model already exists, skipping: $FULL_OUTPUT_PATH"
    exit 0
fi

mkdir -p "$OUTPUT_DIR"
cd "$OUTPUT_DIR"

ENV_DIR="$VCAI_ROOT/.env/ds_export"
if [ ! -d "$ENV_DIR" ]; then
    echo "Error: DeepStream export environment not found at: $ENV_DIR"
    echo "Run: $VCAI_ROOT/scripts/setup/bootstrap.sh ds-env"
    exit 1
fi

echo "Activating DeepStream export environment..."
# Ensure we always leave the shell in a clean state (no venv active),
# even if the export script fails or we exit early after activation.
ds_export_active=0
cleanup_ds_export() {
    if [ "${ds_export_active}" -eq 1 ]; then
        deactivate >/dev/null 2>&1 || true
    fi
}
trap cleanup_ds_export EXIT

# shellcheck source=/dev/null
source "$ENV_DIR/bin/activate"
ds_export_active=1

if [ "$VERSION_NUM" -eq 11 ]; then
    # YOLOv11 requires ultralytics >= 8.3.0
    ULTRALYTICS_OK=$(python3 -c "import ultralytics; v=ultralytics.__version__; major,minor=map(int,v.split('.')[:2]); print('OK' if (major>8 or (major==8 and minor>=3)) else 'OLD')" 2>/dev/null || echo "UNKNOWN")
    if [ "$ULTRALYTICS_OK" != "OK" ]; then
        echo "Error: YOLOv11 requires ultralytics >= 8.3.0"
        exit 1
    fi
fi

if [ ! -f "${MODEL_NAME}.pt" ]; then
    echo "Downloading ${ULTRALYTICS_MODEL_NAME}..."
    python - <<PY
import sys
from ultralytics import YOLO
try:
    YOLO("${ULTRALYTICS_MODEL_NAME}")
except Exception as e:
    print(e, file=sys.stderr)
    sys.exit(1)
PY
    if [ -f "${ULTRALYTICS_MODEL_NAME}.pt" ] && [ ! -f "${MODEL_NAME}.pt" ]; then
        mv "${ULTRALYTICS_MODEL_NAME}.pt" "${MODEL_NAME}.pt"
    fi
fi

# Ensure DeepStream-Yolo export utils exist
EXPORT_SCRIPT=""
case "$VERSION_NUM" in
    5) EXPORT_SCRIPT="$VCAI_ROOT/yolo/DeepStream-Yolo/utils/export_yoloV5.py" ;;
    6) EXPORT_SCRIPT="$VCAI_ROOT/yolo/DeepStream-Yolo/utils/export_yoloV6.py" ;;
    7) EXPORT_SCRIPT="$VCAI_ROOT/yolo/DeepStream-Yolo/utils/export_yoloV7.py" ;;
    8) EXPORT_SCRIPT="$VCAI_ROOT/yolo/DeepStream-Yolo/utils/export_yoloV8.py" ;;
    9) EXPORT_SCRIPT="$VCAI_ROOT/yolo/DeepStream-Yolo/utils/export_yoloV9.py" ;;
    10) EXPORT_SCRIPT="$VCAI_ROOT/yolo/DeepStream-Yolo/utils/export_yoloV10.py" ;;
    11) EXPORT_SCRIPT="$VCAI_ROOT/yolo/DeepStream-Yolo/utils/export_yolo11.py" ;;
esac

if [ ! -f "$EXPORT_SCRIPT" ]; then
    echo "Error: Export script not found: $EXPORT_SCRIPT"
    echo "Run: $VCAI_ROOT/scripts/yolo/setup_yolo.sh"
    exit 1
fi

echo "Exporting with: $EXPORT_SCRIPT"
python "$EXPORT_SCRIPT" \
  --weights "${MODEL_NAME}.pt" \
  --size "${YOLO_HEIGHT}" "${YOLO_WIDTH}" \
  --opset 12 \
  --simplify \
  --batch 1

exported_onnx=""

for candidate in \
    "${MODEL_NAME}.pt.onnx" \
    "${MODEL_NAME}.onnx" \
    "${ULTRALYTICS_MODEL_NAME}.pt.onnx" \
    "${ULTRALYTICS_MODEL_NAME}.onnx"; do
    if [ -f "$candidate" ]; then
        exported_onnx="$candidate"
        break
    fi
done

if [ -z "$exported_onnx" ]; then
    newest_onnx=""
    newest_onnx_mtime=0
    shopt -s nullglob
    for onnx_candidate in *.onnx; do
        if [ "$onnx_candidate" = "$OUTPUT_FILENAME" ]; then
            continue
        fi
        onnx_mtime="$(stat -c %Y -- "$onnx_candidate" 2>/dev/null || echo 0)"
        if [ "$onnx_mtime" -ge "$newest_onnx_mtime" ]; then
            newest_onnx_mtime="$onnx_mtime"
            newest_onnx="$onnx_candidate"
        fi
    done
    shopt -u nullglob

    if [ -n "$newest_onnx" ]; then
        exported_onnx="$newest_onnx"
    fi
fi

if [ -z "$exported_onnx" ]; then
    echo "Error: No exported ONNX file found after running the export script."
    echo "Expected one of:"
    echo "  - ${MODEL_NAME}.pt.onnx"
    echo "  - ${MODEL_NAME}.onnx"
    echo "  - ${ULTRALYTICS_MODEL_NAME}.pt.onnx"
    echo "  - ${ULTRALYTICS_MODEL_NAME}.onnx"
    echo
    echo "ONNX files present in output directory ($OUTPUT_DIR):"
    ls -la -- *.onnx 2>/dev/null || echo "  (none)"
    exit 1
fi

mv -- "$exported_onnx" "$OUTPUT_FILENAME"

echo "✓ Created: $FULL_OUTPUT_PATH"

