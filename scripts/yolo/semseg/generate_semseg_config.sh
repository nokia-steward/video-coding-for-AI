#!/bin/bash

# Generate DeepStream semantic-segmentation config files under yolo/semseg.
#
# Outputs:
# - yolo/semseg/semseg_infer_config_<profile>.yml
# - yolo/semseg/semseg_config_<profile>.yml
#
# Usage:
#   ./scripts/yolo/semseg/generate_semseg_config.sh \
#       --video-width 1920 --video-height 1080 \
#       --profile pandaset \
#       --engine-file /abs/path/model.engine
#
#   ./scripts/yolo/semseg/generate_semseg_config.sh \
#       --video-width 1920 --video-height 1080 \
#       --profile pandaset \
#       --onnx-file /abs/path/model.onnx

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/../common_paths.sh"

VIDEO_WIDTH="640"
VIDEO_HEIGHT="480"
INFER_WIDTH=""
INFER_HEIGHT=""
PROFILE="default"
ONNX_FILE=""
TLT_ENCODED_MODEL=""
TLT_MODEL_KEY="tlt_encode"
ENGINE_FILE=""
LABELS_FILE=""
NETWORK_MODE="1"
NETWORK_MODE_EXPLICIT=false
BATCH_SIZE="1"
GPU_ID="0"
SEGMENTATION_THRESHOLD="0.0"
SEGMENTATION_OUTPUT_ORDER="0"
NET_SCALE_FACTOR=""
OFFSETS=""
MODEL_COLOR_FORMAT=""
OUTPUT_TENSOR_META=""
CLUSTER_MODE=""
NUM_CLASSES=""

show_help() {
    cat <<'EOF'
Usage:
  ./scripts/yolo/semseg/generate_semseg_config.sh [options]

Options:
  --video-width VALUE             Stream width used by streammux (default: 640)
  --video-height VALUE            Stream height used by streammux (default: 480)
  --infer-width VALUE             Inference input width (default: video width)
  --infer-height VALUE            Inference input height (default: video height)
  --profile NAME                  Config profile suffix (default: default)
  --onnx-file PATH                Path to ONNX model (optional if engine-file is set)
  --tlt-encoded-model PATH        Path to TAO ETLT model (optional if onnx/engine is set)
  --tlt-model-key VALUE           TAO ETLT model key (default: tlt_encode)
  --engine-file PATH              Path to TensorRT engine (optional if onnx/tlt model is set)
  --labels-file PATH              Path to labels file (default: auto-generated VCM labels)
  --network-mode VALUE            DeepStream network mode (0=FP32,1=FP16,2=INT8; default: 1)
  --batch-size VALUE              Batch size (default: 1)
  --gpu-id VALUE                  GPU id (default: 0)
  --segmentation-threshold VALUE  Segmentation threshold (default: 0.0)
  --segmentation-output-order V   Segmentation output order (default: 0)
  --net-scale-factor VALUE        net-scale-factor passthrough (optional)
  --offsets VALUE                 offsets passthrough, e.g. 123.675;116.28;103.53 (optional)
  --model-color-format VALUE      model-color-format passthrough (optional)
  --output-tensor-meta VALUE      output-tensor-meta passthrough (optional)
  --cluster-mode VALUE            cluster-mode passthrough (optional)
  --num-classes VALUE             Explicit class count (default: labels line count or 10)
  -h, --help                      Show help
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --video-width) VIDEO_WIDTH="$2"; shift 2 ;;
        --video-height) VIDEO_HEIGHT="$2"; shift 2 ;;
        --infer-width) INFER_WIDTH="$2"; shift 2 ;;
        --infer-height) INFER_HEIGHT="$2"; shift 2 ;;
        --profile) PROFILE="$2"; shift 2 ;;
        --onnx-file) ONNX_FILE="$2"; shift 2 ;;
        --tlt-encoded-model) TLT_ENCODED_MODEL="$2"; shift 2 ;;
        --tlt-model-key) TLT_MODEL_KEY="$2"; shift 2 ;;
        --engine-file) ENGINE_FILE="$2"; shift 2 ;;
        --labels-file) LABELS_FILE="$2"; shift 2 ;;
        --network-mode) NETWORK_MODE="$2"; NETWORK_MODE_EXPLICIT=true; shift 2 ;;
        --batch-size) BATCH_SIZE="$2"; shift 2 ;;
        --gpu-id) GPU_ID="$2"; shift 2 ;;
        --segmentation-threshold) SEGMENTATION_THRESHOLD="$2"; shift 2 ;;
        --segmentation-output-order) SEGMENTATION_OUTPUT_ORDER="$2"; shift 2 ;;
        --net-scale-factor) NET_SCALE_FACTOR="$2"; shift 2 ;;
        --offsets) OFFSETS="$2"; shift 2 ;;
        --model-color-format) MODEL_COLOR_FORMAT="$2"; shift 2 ;;
        --output-tensor-meta) OUTPUT_TENSOR_META="$2"; shift 2 ;;
        --cluster-mode) CLUSTER_MODE="$2"; shift 2 ;;
        --num-classes) NUM_CLASSES="$2"; shift 2 ;;
        -h|--help) show_help; exit 0 ;;
        *) echo "Unknown argument: $1"; show_help; exit 1 ;;
    esac
done

if ! [[ "$VIDEO_WIDTH" =~ ^[0-9]+$ ]] || [ "$VIDEO_WIDTH" -le 0 ]; then
    echo "Invalid --video-width: ${VIDEO_WIDTH}" >&2
    exit 1
fi
if ! [[ "$VIDEO_HEIGHT" =~ ^[0-9]+$ ]] || [ "$VIDEO_HEIGHT" -le 0 ]; then
    echo "Invalid --video-height: ${VIDEO_HEIGHT}" >&2
    exit 1
fi
if [ -z "$INFER_WIDTH" ]; then
    INFER_WIDTH="$VIDEO_WIDTH"
fi
if [ -z "$INFER_HEIGHT" ]; then
    INFER_HEIGHT="$VIDEO_HEIGHT"
fi
if ! [[ "$INFER_WIDTH" =~ ^[0-9]+$ ]] || [ "$INFER_WIDTH" -le 0 ]; then
    echo "Invalid --infer-width: ${INFER_WIDTH}" >&2
    exit 1
fi
if ! [[ "$INFER_HEIGHT" =~ ^[0-9]+$ ]] || [ "$INFER_HEIGHT" -le 0 ]; then
    echo "Invalid --infer-height: ${INFER_HEIGHT}" >&2
    exit 1
fi
if [ -z "$PROFILE" ]; then
    echo "Invalid --profile: empty value" >&2
    exit 1
fi
if [ -z "$ONNX_FILE" ] && [ -z "$TLT_ENCODED_MODEL" ] && [ -z "$ENGINE_FILE" ]; then
    echo "At least one of --onnx-file, --tlt-encoded-model, or --engine-file must be provided." >&2
    exit 1
fi

if [ -n "$ONNX_FILE" ] && [ ! -f "$ONNX_FILE" ]; then
    echo "ONNX model not found: $ONNX_FILE" >&2
    exit 1
fi
if [ -n "$TLT_ENCODED_MODEL" ] && [ ! -f "$TLT_ENCODED_MODEL" ]; then
    echo "TLT encoded model not found: $TLT_ENCODED_MODEL" >&2
    exit 1
fi

if [ -n "$TLT_ENCODED_MODEL" ] && [ "$NETWORK_MODE_EXPLICIT" = false ]; then
    tlt_model_name_lower="$(basename "$TLT_ENCODED_MODEL" | tr '[:upper:]' '[:lower:]')"
    if [[ "$tlt_model_name_lower" == *citysemsegformer* ]]; then
        NETWORK_MODE="2"
        echo "Info: Auto-selecting --network-mode=2 for citysemsegformer ETLT to align with observed engine output."
    fi
fi

# If ONNX is provided but no explicit engine path is given, derive the same
# default engine filename pattern DeepStream nvinfer uses when serializing:
#   <onnx-file>_b<batch>_gpu<gpu>_<precision>.engine
# This keeps rtp_server's startup prebuild check aligned with nvinfer output.
if [ -z "$ENGINE_FILE" ] && { [ -n "$ONNX_FILE" ] || [ -n "$TLT_ENCODED_MODEL" ]; }; then
    model_source_path="$ONNX_FILE"
    if [ -z "$model_source_path" ]; then
        model_source_path="$TLT_ENCODED_MODEL"
    fi

    shopt -s nullglob
    sibling_engines=( "${model_source_path}_b${BATCH_SIZE}_gpu${GPU_ID}_"*.engine )
    shopt -u nullglob
    if [ "${#sibling_engines[@]}" -gt 0 ]; then
        ENGINE_FILE="${sibling_engines[0]}"
        echo "Info: Reusing existing sibling engine: $ENGINE_FILE"
    fi
fi

if [ -z "$ENGINE_FILE" ] && { [ -n "$ONNX_FILE" ] || [ -n "$TLT_ENCODED_MODEL" ]; }; then
    local_precision_suffix="fp16"
    if [ "$NETWORK_MODE" = "0" ]; then
        local_precision_suffix="fp32"
    elif [ "$NETWORK_MODE" = "2" ]; then
        local_precision_suffix="int8"
    fi
    if [ -n "$ONNX_FILE" ]; then
        ENGINE_FILE="${ONNX_FILE}_b${BATCH_SIZE}_gpu${GPU_ID}_${local_precision_suffix}.engine"
    else
        ENGINE_FILE="${TLT_ENCODED_MODEL}_b${BATCH_SIZE}_gpu${GPU_ID}_${local_precision_suffix}.engine"
    fi
fi

# If only an engine is provided (no ONNX fallback), it must already exist.
if [ -n "$ENGINE_FILE" ] && [ -z "$ONNX_FILE" ] && [ -z "$TLT_ENCODED_MODEL" ] && [ ! -f "$ENGINE_FILE" ]; then
    echo "Engine file not found: $ENGINE_FILE (and no ONNX/TLT model provided to build it)" >&2
    exit 1
fi

VCAI_ROOT="$(get_vcai_root "$SCRIPT_DIR")"
SEMSEG_DIR="$VCAI_ROOT/yolo/semseg"
mkdir -p "$SEMSEG_DIR"

if [ -z "$LABELS_FILE" ]; then
    if [ -n "$TLT_ENCODED_MODEL" ]; then
        tlt_model_dir="$(cd "$(dirname "$TLT_ENCODED_MODEL")" && pwd)"
        tlt_model_name="$(basename "$TLT_ENCODED_MODEL" | tr '[:upper:]' '[:lower:]')"
        if [[ "$tlt_model_name" == *citysemsegformer* ]] && [ -f "$tlt_model_dir/labels_citysemsegformer.txt" ]; then
            LABELS_FILE="$tlt_model_dir/labels_citysemsegformer.txt"
        elif [ -f "$tlt_model_dir/labels.txt" ]; then
            LABELS_FILE="$tlt_model_dir/labels.txt"
        fi
    fi
fi

if [ -z "$LABELS_FILE" ]; then
    LABELS_FILE="$SEMSEG_DIR/labels_vcm_eval.txt"
    if [ ! -f "$LABELS_FILE" ]; then
        cat > "$LABELS_FILE" <<'EOF'
vegetation
road
sidewalk
car
truck
motorcycle
bicycle
bus
pedestrian
building
EOF
    fi
fi

if [ ! -f "$LABELS_FILE" ]; then
    echo "Labels file not found: $LABELS_FILE" >&2
    exit 1
fi

if [ -z "$NUM_CLASSES" ]; then
    NUM_CLASSES="$(wc -l < "$LABELS_FILE" | tr -d ' ')"
fi
if ! [[ "$NUM_CLASSES" =~ ^[0-9]+$ ]] || [ "$NUM_CLASSES" -le 0 ]; then
    echo "Invalid class count: $NUM_CLASSES" >&2
    exit 1
fi

INFER_CONFIG_FILE="$SEMSEG_DIR/semseg_infer_config_${PROFILE}.yml"
PIPELINE_CONFIG_FILE="$SEMSEG_DIR/semseg_config_${PROFILE}.yml"

{
    echo "property:"
    if [ -n "$ONNX_FILE" ]; then
        echo "  onnx-file: $ONNX_FILE"
    fi
    if [ -n "$TLT_ENCODED_MODEL" ]; then
        echo "  tlt-encoded-model: $TLT_ENCODED_MODEL"
        echo "  tlt-model-key: $TLT_MODEL_KEY"
    fi
    if [ -n "$ENGINE_FILE" ]; then
        echo "  model-engine-file: $ENGINE_FILE"
    fi
    if [ -n "$NET_SCALE_FACTOR" ]; then
        echo "  net-scale-factor: $NET_SCALE_FACTOR"
    fi
    if [ -n "$OFFSETS" ]; then
        echo "  offsets: $OFFSETS"
    fi
    if [ -n "$MODEL_COLOR_FORMAT" ]; then
        echo "  model-color-format: $MODEL_COLOR_FORMAT"
    fi
    echo "  labelfile-path: $LABELS_FILE"
    echo "  network-type: 2"
    echo "  network-mode: $NETWORK_MODE"
    echo "  num-detected-classes: $NUM_CLASSES"
    echo "  gpu-id: $GPU_ID"
    echo "  batch-size: $BATCH_SIZE"
    echo "  gie-unique-id: 1"
    echo "  process-mode: 1"
    echo "  infer-dims: 3;${INFER_HEIGHT};${INFER_WIDTH}"
    if [ -n "$OUTPUT_TENSOR_META" ]; then
        echo "  output-tensor-meta: $OUTPUT_TENSOR_META"
    fi
    if [ -n "$CLUSTER_MODE" ]; then
        echo "  cluster-mode: $CLUSTER_MODE"
    fi
    echo "  segmentation-threshold: $SEGMENTATION_THRESHOLD"
    echo "  segmentation-output-order: $SEGMENTATION_OUTPUT_ORDER"
} > "$INFER_CONFIG_FILE"

cat > "$PIPELINE_CONFIG_FILE" <<EOF
streammux:
  batch-size: ${BATCH_SIZE}
  batched-push-timeout: 40000
  width: ${VIDEO_WIDTH}
  height: ${VIDEO_HEIGHT}

primary-gie:
  plugin-type: 0
  config-file-path: ${INFER_CONFIG_FILE}
EOF

echo "Generated semseg infer config: $INFER_CONFIG_FILE"
echo "Generated semseg pipeline config: $PIPELINE_CONFIG_FILE"
if [ -n "$ENGINE_FILE" ]; then
    echo "Configured semseg engine target: $ENGINE_FILE"
fi
