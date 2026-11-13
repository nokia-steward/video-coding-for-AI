#!/bin/bash

# Shared script to fetch a semantic-segmentation model into yolo/semseg.
# Mirrors the "model handling" role of scripts/yolo/convert_yolo_model.sh.
#
# Usage:
#   ./scripts/yolo/semseg/convert_semseg_model.sh <width> <height> <output_directory> [model_name]
#
# Example:
#   ./scripts/yolo/semseg/convert_semseg_model.sh 1920 1080 ./yolo/semseg citysemsegformer

set -euo pipefail

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    echo "Error: Invalid number of arguments."
    echo "Usage: $0 <width> <height> <output_directory> [model_name]"
    echo "Example: $0 1920 1080 ./yolo/semseg citysemsegformer"
    exit 1
fi

WIDTH="$1"
HEIGHT="$2"
OUTPUT_DIR="$3"
MODEL_NAME="${4:-citysemsegformer}"

if ! [[ "$WIDTH" =~ ^[0-9]+$ ]] || [ "$WIDTH" -le 0 ]; then
    echo "Error: Invalid width: $WIDTH"
    exit 1
fi
if ! [[ "$HEIGHT" =~ ^[0-9]+$ ]] || [ "$HEIGHT" -le 0 ]; then
    echo "Error: Invalid height: $HEIGHT"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/../common_paths.sh"

VCAI_ROOT="$(get_vcai_root "$SCRIPT_DIR")"

download_file() {
    local url="$1"
    local target="$2"
    local tmp="${target}.tmp.$$"
    rm -f "$tmp"

    if command -v curl >/dev/null 2>&1; then
        curl -L --fail --silent --show-error "$url" -o "$tmp"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$tmp" "$url"
    else
        echo "Error: Neither curl nor wget is available for download."
        return 1
    fi

    if [ ! -s "$tmp" ]; then
        echo "Error: Download produced an empty file for $target"
        rm -f "$tmp"
        return 1
    fi

    mv -f "$tmp" "$target"
}

resolve_ngc_signed_url() {
    local api_url="$1"
    local remote_path="$2"
    python3 - "$api_url" "$remote_path" <<'PY'
import json
import sys
import urllib.request

api_url = sys.argv[1]
remote_path = sys.argv[2]

try:
    with urllib.request.urlopen(api_url, timeout=30) as response:
        data = json.load(response)
except Exception:
    print("")
    sys.exit(0)

file_paths = data.get("filepath", [])
urls = data.get("urls", [])
if not isinstance(file_paths, list) or not isinstance(urls, list):
    print("")
    sys.exit(0)

for path, url in zip(file_paths, urls):
    if path == remote_path:
        print(url)
        sys.exit(0)

print("")
PY
}

mkdir -p "$OUTPUT_DIR"

echo "=== SemSeg model setup ==="
echo "Repo root: $VCAI_ROOT"
echo "Model: $MODEL_NAME"
echo "Video dims requested: ${WIDTH}x${HEIGHT}"

case "$MODEL_NAME" in
    citysemsegformer|city_semsegformer|citysegformer)
        MODEL_NAME="citysemsegformer"
        MODEL_INPUT_WIDTH="1820"
        MODEL_INPUT_HEIGHT="1024"
        MODEL_FILE_PATH="${OUTPUT_DIR}/${MODEL_NAME}_${MODEL_INPUT_WIDTH}x${MODEL_INPUT_HEIGHT}.etlt"
        LEGACY_MODEL_PATH="${OUTPUT_DIR}/${MODEL_NAME}.etlt"
        RAW_LABELS_PATH="${OUTPUT_DIR}/labels_${MODEL_NAME}_raw.txt"
        LABELS_PATH="${OUTPUT_DIR}/labels_${MODEL_NAME}.txt"
        TEMPLATE_PATH="${OUTPUT_DIR}/${MODEL_NAME}_nvinfer_template.txt"
        MODEL_KEY="tlt_encode"

        NGC_LIST_API="https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/citysemsegformer/deployable_v1.0/files"
        MODEL_URL="$(resolve_ngc_signed_url "$NGC_LIST_API" "citysemsegformer.etlt")"
        LABELS_URL="$(resolve_ngc_signed_url "$NGC_LIST_API" "labels.txt")"
        TEMPLATE_URL="$(resolve_ngc_signed_url "$NGC_LIST_API" "nvinfer_config.txt")"

        if [ -z "$MODEL_URL" ] || [ -z "$LABELS_URL" ] || [ -z "$TEMPLATE_URL" ]; then
            echo "Error: Failed to resolve one or more citysemsegformer asset URLs from NGC." >&2
            exit 1
        fi

        if [ ! -f "$MODEL_FILE_PATH" ]; then
            echo "Downloading citysemsegformer ETLT model from NGC..."
            download_file "$MODEL_URL" "$MODEL_FILE_PATH"
            echo "✓ Downloaded model: $MODEL_FILE_PATH"
        else
            echo "Model already exists, skipping: $MODEL_FILE_PATH"
        fi

        if [ ! -f "$RAW_LABELS_PATH" ]; then
            echo "Downloading citysemsegformer labels from NGC..."
            download_file "$LABELS_URL" "$RAW_LABELS_PATH"
            echo "✓ Downloaded raw labels: $RAW_LABELS_PATH"
        else
            echo "Raw labels already exist, skipping: $RAW_LABELS_PATH"
        fi

        if [ ! -f "$TEMPLATE_PATH" ]; then
            echo "Downloading citysemsegformer DeepStream nvinfer template from NGC..."
            download_file "$TEMPLATE_URL" "$TEMPLATE_PATH"
            echo "✓ Downloaded template: $TEMPLATE_PATH"
        else
            echo "Template already exists, skipping: $TEMPLATE_PATH"
        fi

        # Keep runtime labels exactly aligned with model output ordering.
        # Do not prepend synthetic classes here, because that shifts class IDs.
        awk 'NF { gsub(/\r/, ""); print }' "$RAW_LABELS_PATH" > "$LABELS_PATH"
        echo "✓ Prepared runtime labels: $LABELS_PATH"

        if [ ! -f "$LEGACY_MODEL_PATH" ]; then
            ln -s "$(basename "$MODEL_FILE_PATH")" "$LEGACY_MODEL_PATH"
        fi

        echo "SemSeg model setup complete."
        echo "  Model (ETLT): $MODEL_FILE_PATH"
        echo "  Legacy alias: $LEGACY_MODEL_PATH"
        echo "  Labels: $LABELS_PATH"
        echo "  Template: $TEMPLATE_PATH"
        echo "  Model key: $MODEL_KEY"
        ;;
    peoplesemsegnet_shuffle|peoplesemsegnet)
        MODEL_NAME="peoplesemsegnet_shuffle"
        MODEL_INPUT_WIDTH="960"
        MODEL_INPUT_HEIGHT="544"
        MODEL_FILE_PATH="${OUTPUT_DIR}/${MODEL_NAME}_${MODEL_INPUT_WIDTH}x${MODEL_INPUT_HEIGHT}.onnx"
        LEGACY_MODEL_PATH="${OUTPUT_DIR}/${MODEL_NAME}.onnx"
        LABELS_PATH="${OUTPUT_DIR}/labels_${MODEL_NAME}.txt"
        MODEL_URL="https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/peoplesemsegnet/deployable_shuffleseg_unet_onnx_v1.0.1/files?redirect=true&path=peoplesemsegnet_shuffleseg.onnx"
        LABELS_URL="https://api.ngc.nvidia.com/v2/models/org/nvidia/team/tao/peoplesemsegnet/deployable_shuffleseg_unet_onnx_v1.0.1/files?redirect=true&path=labels.txt"

        if [ ! -f "$MODEL_FILE_PATH" ]; then
            echo "Downloading peoplesemsegnet ONNX model from NGC..."
            download_file "$MODEL_URL" "$MODEL_FILE_PATH"
            echo "✓ Downloaded model: $MODEL_FILE_PATH"
        else
            echo "Model already exists, skipping: $MODEL_FILE_PATH"
        fi

        if [ ! -f "$LABELS_PATH" ]; then
            echo "Downloading peoplesemsegnet labels from NGC..."
            download_file "$LABELS_URL" "$LABELS_PATH"
            echo "✓ Downloaded labels: $LABELS_PATH"
        else
            echo "Labels file already exists, skipping: $LABELS_PATH"
        fi

        if [ ! -f "$LEGACY_MODEL_PATH" ]; then
            ln -s "$(basename "$MODEL_FILE_PATH")" "$LEGACY_MODEL_PATH"
        fi

        echo "SemSeg model setup complete."
        echo "  Model (ONNX): $MODEL_FILE_PATH"
        echo "  Legacy alias: $LEGACY_MODEL_PATH"
        echo "  Labels: $LABELS_PATH"
        ;;
    *)
        echo "Error: Unsupported semseg model name: $MODEL_NAME"
        echo "Supported values: citysemsegformer, peoplesemsegnet_shuffle"
        exit 1
        ;;
esac
