#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Common encoder setup functions for encoder_cli run scripts
# This library contains shared functionality used across all run scripts

# Exit immediately if a command exits with a non-zero status.
set -e

# Global variables for common setup
MODEL_NAME="unknown"
ENCODER_OUTPUT_FILE=""
VCAI_PATH=""
HELP_MODE=false
ENCODER_CONFIG_FILE=""
COMMON_YOLO_SETUP_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/scripts/setup/lib/yolo_setup_lib.sh"

if [ -f "$COMMON_YOLO_SETUP_SCRIPT" ]; then
    # shellcheck source=/dev/null
    source "$COMMON_YOLO_SETUP_SCRIPT"
else
    echo "Warning: Shared YOLO helper script not found: $COMMON_YOLO_SETUP_SCRIPT" >&2
fi

# Function to detect which config file to use
# Checks ENCODER_CONFIG_FILE environment variable first, then command line args, then defaults to encoder_config.ini
detect_config_file() {
    # If ENCODER_CONFIG_FILE is already set, use it
    if [ -n "$ENCODER_CONFIG_FILE" ] && [ -f "$ENCODER_CONFIG_FILE" ]; then
        echo "$ENCODER_CONFIG_FILE"
        return 0
    fi

    # Check command line arguments for --config
    local config_file=""
    for arg in "$@"; do
        if [[ "$arg" == "--config"* ]]; then
            if [[ "$arg" == "--config="* ]]; then
                config_file="${arg#--config=}"
            else
                # Next argument is the config file
                local found_config=false
                for next_arg in "$@"; do
                    if [ "$found_config" = true ]; then
                        config_file="$next_arg"
                        break
                    fi
                    if [ "$next_arg" = "$arg" ]; then
                        found_config=true
                    fi
                done
            fi
            break
        fi
    done

    # If found in command line, use it
    if [ -n "$config_file" ] && [ -f "$config_file" ]; then
        echo "$config_file"
        return 0
    fi

    # Default to encoder_config.ini
    if [ -f "encoder_config.ini" ]; then
        echo "encoder_config.ini"
        return 0
    fi

    # If nothing found, return default anyway
    echo "encoder_config.ini"
    return 0
}


# Function to get the VCAI directory path
get_vcai_path() {
    # Get the directory of the current script
    # Script is in encoder_cli/scripts/, so go up two levels
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local project_root="$(cd "$script_dir/../.." && pwd)"

    # Verify this is the VCAI directory by checking for key directories
    if [ -d "$project_root/encoder_cli" ] && [ -d "$project_root/encoder_cli/scripts" ]; then
        echo "$project_root"
        return 0
    fi

    # Fallback: Start from the current directory and work up to find the VCAI root
    local current_dir="$(pwd)"
    local search_dir="$current_dir"

    while [ "$search_dir" != "/" ]; do
        if [ -d "$search_dir/encoder_cli" ] && [ -d "$search_dir/encoder_cli/scripts" ]; then
            echo "$search_dir"
            return 0
        fi
        search_dir="$(dirname "$search_dir")"
    done

    # If we still can't find it, return the script-based path anyway (for backward compatibility)
    # but log a warning
    echo "Warning: Could not validate VCAI directory, using script-based path: $project_root" >&2
    echo "$project_root"
    return 0
}

# Function to create COCO labels file if it doesn't exist
create_coco_labels_file() {
    local labels_file="$1"
    if declare -F sf_create_coco_labels_file >/dev/null; then
        sf_create_coco_labels_file "$labels_file"
    else
        echo "Error: Shared YOLO label helper is unavailable."
        return 1
    fi
}

# Function to check and setup YOLO model and engine readiness
check_and_setup_yolo() {
    local width="$1"
    local height="$2"

    # Skip in help mode
    if [ "$HELP_MODE" = true ]; then
        return 0
    fi

    # Detect config file
    local config_file="${ENCODER_CONFIG_FILE:-encoder_config.ini}"
    if [ ! -f "$config_file" ]; then
        config_file="encoder_config.ini"
    fi

    local detection_config=""
    if declare -F sf_detect_yolo_detection_config >/dev/null; then
        detection_config="$(sf_detect_yolo_detection_config "$config_file")"
    fi

    # If YOLO is not configured, skip this check
    if [[ "$detection_config" != *"yolo"* ]] && [[ "$detection_config" != *"YOLO"* ]]; then
        return 0
    fi

    # Read YOLO version from config file (default to "8n")
    local yolo_version="8n"
    if declare -F sf_resolve_yolo_version >/dev/null; then
        yolo_version="$(sf_resolve_yolo_version "$config_file" "$detection_config")"
    else
        local version_from_config
        version_from_config=$(grep "^yolo_version" "$config_file" | grep -v "^#" | cut -d'=' -f2 | tr -d ' ' || true)
        if [ -n "$version_from_config" ]; then
            yolo_version="$version_from_config"
        fi
    fi

    # YOLO is configured, but validate dimensions are provided and valid
    if [ -z "$width" ] || [ -z "$height" ] || ! [[ "$width" =~ ^[0-9]+$ ]] || ! [[ "$height" =~ ^[0-9]+$ ]]; then
        echo "Warning: YOLO is configured but resolution (width x height) is not provided."
        echo "Skipping YOLO model setup. Please provide resolution to use YOLO detection."
        return 0
    fi

    echo "YOLO model detected in configuration (version: $yolo_version). Checking YOLO readiness..."

    # Ensure VCAI_PATH is set
    if [ -z "$VCAI_PATH" ]; then
        VCAI_PATH=$(get_vcai_path)
    fi

    if declare -F sf_ensure_yolo_assets >/dev/null; then
        sf_ensure_yolo_assets "$width" "$height" "$yolo_version" "$VCAI_PATH"
    else
        echo "Error: Shared YOLO setup helpers are unavailable."
        exit 1
    fi
}

# Function to setup common environment variables
setup_environment() {
    echo "Setting up environment..."

    # Get VCAI path
    VCAI_PATH=$(get_vcai_path)

    export GST_DEBUG_NO_COLOR=1

    # Detect system architecture dynamically
    local ARCH=$(uname -m)
    local LIB_ARCH=""
    case $ARCH in
        x86_64)
            LIB_ARCH="x86_64-linux-gnu"
            ;;
        aarch64)
            LIB_ARCH="aarch64-linux-gnu"
            ;;
        arm64)
            LIB_ARCH="aarch64-linux-gnu"
            ;;
        *)
            echo "Warning: Unknown architecture $ARCH, defaulting to x86_64-linux-gnu"
            LIB_ARCH="x86_64-linux-gnu"
            ;;
    esac

    echo "Detected architecture: $ARCH, using library path: $LIB_ARCH"

    # Prioritize system runtime libraries
    export LD_LIBRARY_PATH="/usr/lib/$LIB_ARCH:/lib/$LIB_ARCH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

    # Find DeepStream installation dynamically
    local DEEPSTREAM_PATHS=(
        "/opt/nvidia/deepstream/deepstream-7.1"
        "/opt/nvidia/deepstream/deepstream-7.0"
        "/opt/nvidia/deepstream/deepstream-6.4"
        "/opt/nvidia/deepstream/deepstream-6.3"
        "/opt/nvidia/deepstream/deepstream-6.2"
        "/opt/nvidia/deepstream/deepstream-6.1"
        "/opt/nvidia/deepstream/deepstream-6.0"
    )

    local DEEPSTREAM_PATH=""
    for path in "${DEEPSTREAM_PATHS[@]}"; do
        if [ -d "$path" ]; then
            DEEPSTREAM_PATH="$path"
            break
        fi
    done

    if [ -n "$DEEPSTREAM_PATH" ]; then
        export GST_PLUGIN_PATH="$DEEPSTREAM_PATH/lib/gst-plugins:/usr/lib/$LIB_ARCH/gstreamer-1.0"
        export PYTHONPATH="$DEEPSTREAM_PATH/lib/python3.8/site-packages${PYTHONPATH:+:$PYTHONPATH}"
        echo "✓ DeepStream found: $DEEPSTREAM_PATH"
    else
        echo "⚠ DeepStream not found in standard locations. Using GStreamer plugins."
        export GST_PLUGIN_PATH="/usr/lib/$LIB_ARCH/gstreamer-1.0"
        export PYTHONPATH=""
    fi

    # Kvazaar: use only the standalone lib/kvazaar build. Do not use plugins from lib/install.
    local KVAAZAERNC_SO=""
    for dir in "$VCAI_PATH/lib/kvazaar/lib/$LIB_ARCH/gstreamer-1.0" "$VCAI_PATH/lib/kvazaar/lib/gstreamer-1.0"; do
        if [ -f "${dir}/libgstkvazaarenc.so" ]; then
            KVAAZAERNC_SO="${dir}/libgstkvazaarenc.so"
            break
        fi
    done
    if [ -n "$KVAAZAERNC_SO" ]; then
        local KVAAZAERNC_PLUGIN_DIR="$VCAI_PATH/lib/kvazaar/gst-plugins-kvazaar-only"
        mkdir -p "$KVAAZAERNC_PLUGIN_DIR"
        ln -sf "$KVAAZAERNC_SO" "$KVAAZAERNC_PLUGIN_DIR/libgstkvazaarenc.so"
        export GST_PLUGIN_PATH="${GST_PLUGIN_PATH:+$GST_PLUGIN_PATH:}$KVAAZAERNC_PLUGIN_DIR"
        echo "Kvazaar encoder plugin path added (isolated dir): $KVAAZAERNC_PLUGIN_DIR"
        export LD_LIBRARY_PATH="$VCAI_PATH/lib/kvazaar/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        if [ -d "$VCAI_PATH/lib/kvazaar/lib/$LIB_ARCH" ]; then
            export LD_LIBRARY_PATH="$VCAI_PATH/lib/kvazaar/lib/$LIB_ARCH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        fi
    fi

    # gst-openhevc: use only the standalone lib/openhevc build. Do not use plugins from lib/install.
    local OPENHEVC_SO=""
    for dir in "$VCAI_PATH/lib/openhevc/lib/$LIB_ARCH/gstreamer-1.0" "$VCAI_PATH/lib/openhevc/lib/gstreamer-1.0"; do
        if [ -f "${dir}/libgstopenhevc.so" ]; then
            OPENHEVC_SO="${dir}/libgstopenhevc.so"
            break
        fi
    done
    if [ -n "$OPENHEVC_SO" ]; then
        local OPENHEVC_PLUGIN_DIR="$VCAI_PATH/lib/openhevc/gst-plugins-openhevc-only"
        mkdir -p "$OPENHEVC_PLUGIN_DIR"
        ln -sf "$OPENHEVC_SO" "$OPENHEVC_PLUGIN_DIR/libgstopenhevc.so"
        export GST_PLUGIN_PATH="${GST_PLUGIN_PATH:+$GST_PLUGIN_PATH:}$OPENHEVC_PLUGIN_DIR"
        echo "gst-openhevc plugin path added (isolated dir): $OPENHEVC_PLUGIN_DIR"
        export LD_LIBRARY_PATH="$VCAI_PATH/lib/openhevc/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        if [ -d "$VCAI_PATH/lib/openhevc/lib/$LIB_ARCH" ]; then
            export LD_LIBRARY_PATH="$VCAI_PATH/lib/openhevc/lib/$LIB_ARCH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        fi
    fi

    # Set GStreamer debug level
    # Only set if GST_DEBUG is not already set (e.g., from Docker environment for full debug)
    if [ -z "${GST_DEBUG:-}" ]; then
        export GST_DEBUG=2,encoder-cli:3,encoded-frame-processing:3,frame-processing:3,frame-processing-utils:3,pipeline-message-processing:2,roi-detection-processing:2,roi-frame-processing:3,gst-callbacks:2,gst-pipeline:2,simple-status-reporter:4,performance-monitor:4,roi-parser:3,roi-manager:3,rectangle-splitter:2,border-drawer:3,nal-utils:3,area-copy:2,video-info:1,nvinfer:2,nvstreammux:2,nvtracker:2,nvvideoconvert:2,gpu-elements:2,low-delay-packer:3,random-access-packer:3,packer:3,frame-saver:2,encoder-config:5,configuration-manager:5,config-parser:3,bitrate-calculation:3,output-pipeline:3,input-pipeline:3,input-analyzer:3,pipeline-utils:3,common-input:4,file-input:4,camera-input:3,roi-input:3,udp-input:3,frame-drop-manager:3,rtp-utils:3,rtp-timestamp-overlay:3,fps-overlay:3,static-roi:2,groundtruth-roi:2,videodecoder:1,communication-manager:3,transport:3,transport-webrtc:3,signaling:3,rtp-heartbeat:3,branch-fps-tracker:3,frame-merger:3,bypass-path:3,appsink-polling:2,frame-router:3,last-inference-roi:3,frame-path-meta:3
    else
        echo "Preserving GST_DEBUG from environment: ${GST_DEBUG}"
    fi

    # Source CUDA version detection script
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    source "$SCRIPT_DIR/set_cuda_ver.sh"

    # Detect and set CUDA version dynamically
    detect_and_set_cuda_version

    # DeepStream-Yolo environment setup
    export DEEPSTREAM_YOLO_PATH="$VCAI_PATH/yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo"

    if [ -d "$DEEPSTREAM_YOLO_PATH" ]; then
        export LD_LIBRARY_PATH="$DEEPSTREAM_YOLO_PATH${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
        echo "DeepStream-Yolo library path added: $DEEPSTREAM_YOLO_PATH"
    else
        echo "Warning: DeepStream-Yolo path not found: $DEEPSTREAM_YOLO_PATH"
        echo "Please ensure DeepStream-Yolo is compiled and the path is correct"
    fi


    # Clear GStreamer registry cache to force a rescan
    # with the correct environment settings.
    echo "Clearing GStreamer cache..."
    rm -rf ~/.cache/gstreamer-1.0
}

# Function to setup model configuration
setup_model_config() {
    echo "Generating configuration files..."

    # Skip model configuration in help mode
    if [ "$HELP_MODE" = true ]; then
        echo "Skipping model configuration in help mode"
        return
    fi

    # Detect config file
    local config_file="${ENCODER_CONFIG_FILE:-encoder_config.ini}"
    if [ ! -f "$config_file" ]; then
        config_file="encoder_config.ini"
    fi

    # Detect which model is being used and generate appropriate configuration
    local DETECTION_CONFIG=""
    if [ -f "$config_file" ]; then
        DETECTION_CONFIG=$(grep "^deepstream_config_file" "$config_file" | grep -v "^#" | cut -d'=' -f2 | tr -d ' ' || true)
    fi
    # Also check for detection_config_file (older config format)
    if [ -z "$DETECTION_CONFIG" ] && [ -f "$config_file" ]; then
        DETECTION_CONFIG=$(grep "^detection_config_file" "$config_file" | grep -v "^#" | cut -d'=' -f2 | tr -d ' ' || true)
    fi
    echo "Detection config file: '$DETECTION_CONFIG'"

    if [[ "$DETECTION_CONFIG" == *"peoplenet"* ]]; then
        echo "PeopleNet model detected. Generating PeopleNet configuration..."
        if [ -f "./scripts/generate_peoplenet_config.sh" ]; then
            ./scripts/generate_peoplenet_config.sh "$1" "$2"
        else
            echo "Warning: generate_peoplenet_config.sh not found. Using static peoplenet config if it exists."
        fi
    elif [[ "$DETECTION_CONFIG" == *"yolo"* ]]; then
        echo "YOLO model detected. Setting up YOLO model and configuration..."

        # Read YOLO version from config file (default to "8n")
        local yolo_version="8n"
        if [ -f "$config_file" ]; then
            local version_from_config=$(grep "^yolo_version" "$config_file" | grep -v "^#" | cut -d'=' -f2 | tr -d ' ' || true)
            if [ -n "$version_from_config" ]; then
                yolo_version="$version_from_config"
            fi
        fi

        # Validate dimensions are provided and valid before running YOLO scripts
        if [ -z "$1" ] || [ -z "$2" ] || ! [[ "$1" =~ ^[0-9]+$ ]] || ! [[ "$2" =~ ^[0-9]+$ ]]; then
            echo "Warning: YOLO is configured but resolution (width x height) is not provided."
            echo "Skipping YOLO model setup. Please provide resolution to use YOLO detection."
            return 0
        fi

        # Calculate YOLO-compatible dimensions (multiples of 32)
        local yolo_width=$(( ($1 + 31) / 32 * 32 ))
        local yolo_height=$(( ($2 + 31) / 32 * 32 ))

        # First, ensure the YOLO model exists for the given dimensions
        local YOLO_MODEL_PATH="../yolo/yolov${yolo_version}_${yolo_width}x${yolo_height}.onnx"
        if [ ! -f "$YOLO_MODEL_PATH" ]; then
            echo "YOLO model not found for dimensions ${1}x${2} (YOLO-compatible: ${yolo_width}x${yolo_height}, version: ${yolo_version}). Creating model..."
            if [ -f "./scripts/convert_yolo_model.sh" ]; then
                ./scripts/convert_yolo_model.sh "$1" "$2" "../yolo" "$yolo_version"
                if [ $? -ne 0 ]; then
                    echo "Error: Failed to create YOLO model. Please check the convert_yolo_model.sh script."
                    exit 1
                fi
            else
                echo "Error: convert_yolo_model.sh not found. Cannot create YOLO model."
                echo "Please ensure the convert_yolo_model.sh script exists in the scripts directory."
                exit 1
            fi
        else
            echo "YOLO model already exists for dimensions ${1}x${2} (YOLO-compatible: ${yolo_width}x${yolo_height}, version: ${yolo_version})."
        fi

        # Always regenerate YOLO configuration to match current resolution
        # Ensure VCAI_PATH is set
        if [ -z "$VCAI_PATH" ]; then
            VCAI_PATH=$(get_vcai_path)
        fi

        echo "Generating YOLO configuration for ${1}x${2} (version: ${yolo_version})..."
        if [ -f "./scripts/generate_yolo_config.sh" ]; then
            ./scripts/generate_yolo_config.sh "$1" "$2" "$yolo_version"

            # Update encoder_config.ini to use the model-specific config file
            local config_file="${ENCODER_CONFIG_FILE:-encoder_config.ini}"
            if [ ! -f "$config_file" ]; then
                config_file="encoder_config.ini"
            fi

            if [ -f "$config_file" ]; then
                local model_specific_config="../yolo/yolo_config_${yolo_version}.yml"
                # Check if the config file already has the correct path
                if grep -q "^deepstream_config_file.*=.*yolo_config" "$config_file"; then
                    # Update existing deepstream_config_file line
                    if command -v sed >/dev/null 2>&1; then
                        # Use sed to update the line (works on both Linux and macOS)
                        if [[ "$OSTYPE" == "darwin"* ]]; then
                            sed -i '' "s|^deepstream_config_file.*=.*|deepstream_config_file = ${model_specific_config}|" "$config_file"
                        else
                            sed -i "s|^deepstream_config_file.*=.*|deepstream_config_file = ${model_specific_config}|" "$config_file"
                        fi
                        echo "✓ Updated encoder_config.ini to use model-specific config: $model_specific_config"
                    else
                        echo "Warning: sed not found, cannot automatically update encoder_config.ini"
                        echo "Please manually set deepstream_config_file = $model_specific_config in $config_file"
                    fi
                else
                    # Add the config file path if it doesn't exist
                    echo "Note: Please set deepstream_config_file = $model_specific_config in $config_file"
                fi
            fi
        else
            echo "Warning: generate_yolo_config.sh not found. Using static model-specific YAML config files if they exist."
        fi
    else
        echo "Warning: Unknown detection model. Using static configuration files if they exist."
    fi
}

# Function to determine model type and prepare execution
prepare_execution() {
    echo "Preparing execution..."

    # Build encoder_cli (unless --no-build flag is set)
    if [ "$NO_BUILD" = true ]; then
        echo "Skipping build step (--no-build flag detected)"
    else
        ./build.sh
    fi

    # Determine model type from config file
    MODEL_NAME="unknown"
    local config_file="${ENCODER_CONFIG_FILE:-encoder_config.ini}"
    if [ ! -f "$config_file" ]; then
        config_file="encoder_config.ini"
    fi
    if [ -f "$config_file" ]; then
        local DETECTION_CONFIG=$(grep "^deepstream_config_file" "$config_file" | grep -v "^#" | cut -d'=' -f2 | tr -d ' ' || true)
        # Also check for detection_config_file (older config format)
        if [ -z "$DETECTION_CONFIG" ]; then
            DETECTION_CONFIG=$(grep "^detection_config_file" "$config_file" | grep -v "^#" | cut -d'=' -f2 | tr -d ' ' || true)
        fi
        echo "Model detection - config file: '$DETECTION_CONFIG'"
        if [[ "$DETECTION_CONFIG" == *"yolo"* ]] || [[ "$DETECTION_CONFIG" == *"YOLO"* ]]; then
            MODEL_NAME="yolo"
            echo "Model type determined: YOLO"
        elif [[ "$DETECTION_CONFIG" == *"peoplenet"* ]] || [[ "$DETECTION_CONFIG" == *"nvinfer_config.yml"* ]]; then
            MODEL_NAME="peoplenet"
            echo "Model type determined: PeopleNet"
        else
            echo "Model type could not be determined from config file"
        fi
    fi

    # Create model-specific output directory and filename in VCAI root
    mkdir -p "../$MODEL_NAME"

    echo "Using model: $MODEL_NAME"
}

# Ensure a repo-level venv exists with torch/torchvision for .pt -> .onnx export.
# Prints the path to the venv's python3; returns 0 on success.
ensure_onnx_export_venv() {
    local encoder_cli_dir="$1"
    local venv_dir="${encoder_cli_dir}/../.env/onnx"
    if [ ! -d "$venv_dir" ]; then
        echo "Creating Python venv for ONNX export at ${venv_dir}..." >&2
        if ! python3 -m venv "$venv_dir"; then
            echo "Error: failed to create venv at $venv_dir" >&2
            return 1
        fi
        if ! "$venv_dir/bin/pip" install --quiet torch torchvision onnxscript; then
            echo "Error: failed to install torch/torchvision/onnxscript in $venv_dir" >&2
            return 1
        fi
    else
        if ! "$venv_dir/bin/python3" -c "import torch" 2>/dev/null; then
            echo "Installing torch/torchvision in existing venv ${venv_dir}..." >&2
            if ! "$venv_dir/bin/pip" install --quiet torch torchvision onnxscript; then
                echo "Error: failed to install torch/torchvision/onnxscript in $venv_dir" >&2
                return 1
            fi
        else
            if ! "$venv_dir/bin/python3" -c "import onnxscript" 2>/dev/null; then
                echo "Installing onnxscript in existing venv ${venv_dir} (required by torch.onnx.export)..." >&2
                if ! "$venv_dir/bin/pip" install --quiet onnxscript; then
                    echo "Error: failed to install onnxscript in $venv_dir" >&2
                    return 1
                fi
            fi
        fi
    fi
    echo "$venv_dir/bin/python3"
}

# Ensure adaptive luma ONNX exists when enable_adaptive_luma_adaptation is true.
# Config accepts either a .pt or .onnx path. If .pt, we export to .onnx (same base path) when missing or older.
# If .onnx, we use it as-is. Sets RESOLVED_INPUT_ANALYZER_MODEL_PATH to the .onnx path the encoder should load.
# Uses .env/onnx for the export so torch/torchvision/numpy are consistent (avoids pip/apt mix).
ensure_adaptive_luma_onnx() {
    RESOLVED_INPUT_ANALYZER_MODEL_PATH=""
    local config_file="${ENCODER_CONFIG_FILE:-encoder_config.ini}"
    if [ ! -f "$config_file" ]; then
        return 0
    fi
    local adaptive_enabled
    adaptive_enabled=$(awk -F= '/^[[:space:]]*enable_adaptive_luma_adaptation[[:space:]]*=/ {
        gsub(/[[:space:]]+/, "", $2); if ($2 == "true" || $2 == "1") print "true"; exit
    }' "$config_file" 2>/dev/null || true)
    if [ "$adaptive_enabled" != "true" ]; then
        return 0
    fi
    local model_path
    model_path=$(awk -F= '/^[[:space:]]*input_analyzer_model_path[[:space:]]*=/ {
        sub(/^[^=]*=[[:space:]]*/, ""); gsub(/^[[:space:]]+|[[:space:]]+$/, ""); print; exit
    }' "$config_file" 2>/dev/null || true)
    if [ -z "$model_path" ]; then
        return 0
    fi
    local onnx_path pt_path
    case "$model_path" in
        *.pt)  pt_path="$model_path"; onnx_path="${model_path%.pt}.onnx" ;;
        *.onnx) onnx_path="$model_path"; pt_path="${model_path%.onnx}.pt" ;;
        *)     onnx_path="$model_path"; pt_path="${model_path}.pt" ;;
    esac
    local need_export=false
    if [ ! -f "$onnx_path" ]; then
        need_export=true
    elif [ -f "$pt_path" ] && [ "$pt_path" -nt "$onnx_path" ]; then
        need_export=true
    fi
    if [ "$need_export" = true ]; then
        if [ ! -f "$pt_path" ]; then
            echo "Error: enable_adaptive_luma_adaptation is true but $pt_path not found." >&2
            echo "  Place model_weights.pt in yolo/ or set input_analyzer_model_path in [ROI] to an existing .pt or .onnx path." >&2
            return 1
        fi
        local script_dir
        script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
        local encoder_cli_dir
        encoder_cli_dir="$(cd "$script_dir/.." && pwd)"
        local export_script="${script_dir}/export_input_analyzer_onnx.py"
        if [ ! -f "$export_script" ]; then
            echo "Error: export script not found: $export_script" >&2
            return 1
        fi
        local onnx_python
        onnx_python=$(ensure_onnx_export_venv "$encoder_cli_dir") || return 1
        echo "Exporting adaptive luma model: $pt_path -> $onnx_path"
        if ! "$onnx_python" "$export_script" "$pt_path" "$onnx_path"; then
            echo "Error: ONNX export failed. Adaptive luma requires a .onnx model." >&2
            echo "  Ensure $pt_path contains 'model_state_dict' (see encoder_cli/scripts/export_input_analyzer_onnx.py)." >&2
            echo "  Or export manually: $onnx_python $export_script $pt_path $onnx_path" >&2
            return 1
        fi
    fi
    if [ ! -f "$onnx_path" ]; then
        echo "Error: ONNX model not found: $onnx_path (expected when using .pt config; export step may have been skipped or failed)." >&2
        return 1
    fi
    RESOLVED_INPUT_ANALYZER_MODEL_PATH="$onnx_path"
}

# Function to find encoder_cli binary dynamically
find_encoder_cli_binary() {
    # Get the VCAI path if not already set
    if [ -z "$VCAI_PATH" ]; then
        VCAI_PATH=$(get_vcai_path)
    fi

    # Common locations where the binary might be
    local possible_paths=(
        "$VCAI_PATH/build/encoder_cli/bin/encoder_cli"
        "$VCAI_PATH/build/encoder_cli/encoder_cli/encoder_cli"
        "$VCAI_PATH/build/encoder_cli/encoder_cli"
        "../build/encoder_cli/bin/encoder_cli"
        "../build/encoder_cli/encoder_cli/encoder_cli"
        "../build/encoder_cli/encoder_cli"
    )

    # Try each path
    for path in "${possible_paths[@]}"; do
        if [ -f "$path" ] && [ -x "$path" ]; then
            echo "$path"
            return 0
        fi
    done

    # If not found, return error
    echo "Error: encoder_cli binary not found in any expected location" >&2
    echo "Searched in:" >&2
    for path in "${possible_paths[@]}"; do
        echo "  - $path" >&2
    done
    return 1
}

# Function to execute encoder_cli with common arguments
execute_encoder_cli() {
    local additional_args=("$@")
    local has_config_arg=false

    echo "Running encoder_cli..."

    for arg in "${additional_args[@]}"; do
        if [[ "$arg" == "--config" || "$arg" == --config=* ]]; then
            has_config_arg=true
            break
        fi
    done

    # Find the binary dynamically
    local encoder_cli_binary
    encoder_cli_binary=$(find_encoder_cli_binary)
    if [ $? -ne 0 ]; then
        echo "$encoder_cli_binary"
        exit 1
    fi

    echo "Using binary: $encoder_cli_binary"

    # Clean up previous output file
    rm -f "$ENCODER_OUTPUT_FILE"

    # Execute the application, redirecting all output to a log file.
    if [ "$has_config_arg" = true ]; then
        "$encoder_cli_binary" \
          "${additional_args[@]}" \
          2>&1 | tee "$ENCODER_OUTPUT_FILE"
    else
        "$encoder_cli_binary" \
          --config encoder_config.ini \
          "${additional_args[@]}" \
          2>&1 | tee "$ENCODER_OUTPUT_FILE"
    fi
}

# Function to show common help information
show_common_help() {
    local script_name="$1"
    local description="$2"

    echo "Usage: $script_name $description"
    echo ""
    echo "Use '--' to separate script parameters from encoder_cli application parameters."
    echo "Additional arguments after '--' will be passed directly to the encoder_cli application."
    echo "Use --help to see all available options."
}
