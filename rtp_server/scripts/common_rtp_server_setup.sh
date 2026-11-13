#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Common rtp_server setup functions for run scripts.
# This library contains shared functionality used by rtp_server run scripts.

# Exit immediately if a command exits with a non-zero status.
set -e

# Global variables for common setup
MODEL_NAME="unknown"
VCAI_PATH=""
HELP_MODE=false
RTP_SERVER_CONFIG_FILE=""
COMMON_YOLO_SETUP_SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/scripts/setup/lib/yolo_setup_lib.sh"

if [ -f "$COMMON_YOLO_SETUP_SCRIPT" ]; then
    # shellcheck source=/dev/null
    source "$COMMON_YOLO_SETUP_SCRIPT"
else
    echo "Warning: Shared YOLO helper script not found: $COMMON_YOLO_SETUP_SCRIPT" >&2
fi

# Function to detect which config file to use.
# Checks RTP_SERVER_CONFIG_FILE environment variable first, then command line args,
# then defaults to rtp_server_config.ini.
detect_config_file() {
    # If RTP_SERVER_CONFIG_FILE is already set, use it
    if [ -n "$RTP_SERVER_CONFIG_FILE" ] && [ -f "$RTP_SERVER_CONFIG_FILE" ]; then
        echo "$RTP_SERVER_CONFIG_FILE"
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

    # Default to rtp_server_config.ini
    if [ -f "rtp_server_config.ini" ]; then
        echo "rtp_server_config.ini"
        return 0
    fi

    # If nothing found, return default anyway
    echo "rtp_server_config.ini"
    return 0
}

# Function to get the VCAI directory path.
get_vcai_path() {
    # Get the directory of the current script.
    # Script is in rtp_server/scripts/, so go up two levels.
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local project_root="$(cd "$script_dir/../.." && pwd)"

    # Verify this is the VCAI directory by checking for key directories
    if [ -d "$project_root/rtp_server" ] && [ -d "$project_root/rtp_server/scripts" ]; then
        echo "$project_root"
        return 0
    fi

    # Fallback: Start from the current directory and work up to find the VCAI root
    local current_dir="$(pwd)"
    local search_dir="$current_dir"

    while [ "$search_dir" != "/" ]; do
        if [ -d "$search_dir/rtp_server" ] && [ -d "$search_dir/rtp_server/scripts" ]; then
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

# Function to create COCO labels file if it doesn't exist.
create_coco_labels_file() {
    local labels_file="$1"
    if declare -F sf_create_coco_labels_file >/dev/null; then
        sf_create_coco_labels_file "$labels_file"
    else
        echo "Error: Shared YOLO label helper is unavailable."
        return 1
    fi
}

read_inference_config_value() {
    local config_file="$1"
    local key_name="$2"
    if [ ! -f "$config_file" ]; then
        echo ""
        return 0
    fi
    awk -F'=' -v key_name="$key_name" '
        BEGIN { in_inference = 0 }
        /^[[:space:]]*[#;]/ { next }
        /^[[:space:]]*\[/ {
            in_inference = ($0 ~ /^[[:space:]]*\[Inference\][[:space:]]*$/)
            next
        }
        in_inference && $1 ~ "^[[:space:]]*" key_name "[[:space:]]*$" {
            value = $2
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
            print value
            exit
        }
    ' "$config_file" || true
}

resolve_config_relative_path() {
    local value="$1"
    local config_file="$2"
    if [ -z "$value" ]; then
        echo ""
        return 0
    fi
    if [[ "$value" == /* ]]; then
        echo "$value"
        return 0
    fi
    local config_dir
    config_dir="$(cd "$(dirname "$config_file")" && pwd)"
    if [ -f "$config_dir/$value" ] || [ -d "$config_dir/$value" ]; then
        echo "$config_dir/$value"
        return 0
    fi
    if [ -n "$VCAI_PATH" ] && [ -f "$VCAI_PATH/$value" ]; then
        echo "$VCAI_PATH/$value"
        return 0
    fi
    echo "$config_dir/$value"
}

extract_dimensions_from_filename() {
    local name="$1"
    local parsed=""
    if [[ "$name" =~ _([0-9]+)x([0-9]+)(_([0-9]+))?\. ]]; then
        parsed="${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
    elif [[ "$name" =~ _([0-9]+)_([0-9]+)(_([0-9]+))?\. ]]; then
        parsed="${BASH_REMATCH[1]} ${BASH_REMATCH[2]}"
    fi
    echo "$parsed"
}

discover_semseg_asset() {
    local semseg_dir="$1"
    shift
    local pattern
    for pattern in "$@"; do
        # shellcheck disable=SC2086
        for candidate in $pattern; do
            if [ -f "$candidate" ]; then
                echo "$candidate"
                return 0
            fi
        done
    done
    echo ""
}

setup_semseg_model_config() {
    local width="$1"
    local height="$2"
    local config_file="$3"
    : "$width"
    : "$height"

    local semseg_deepstream_config_file=""
    local semseg_model_file=""
    local semseg_engine_file=""
    semseg_deepstream_config_file="$(read_inference_config_value "$config_file" "semseg_deepstream_config_file")"
    semseg_model_file="$(read_inference_config_value "$config_file" "semseg_model_file")"
    semseg_engine_file="$(read_inference_config_value "$config_file" "semseg_engine_file")"

    if [ -n "$semseg_deepstream_config_file" ] || [ -n "$semseg_model_file" ] || [ -n "$semseg_engine_file" ]; then
        echo "Warning: semseg config/model auto-setup in rtp_server/run.sh is deprecated."
        echo "Use scripts/run_tet_comparison.sh --mode semseg for AH0187 semseg evaluation."
    fi
}

# Function to check and setup YOLO model and engine readiness.
check_and_setup_yolo() {
    local width="$1"
    local height="$2"

    # Skip in help mode
    if [ "$HELP_MODE" = true ]; then
        return 0
    fi

    # Detect config file
    local config_file="${RTP_SERVER_CONFIG_FILE:-rtp_server_config.ini}"
    if [ ! -f "$config_file" ]; then
        config_file="rtp_server_config.ini"
    fi

    local detection_config=""
    if declare -F sf_detect_yolo_detection_config >/dev/null; then
        detection_config="$(sf_detect_yolo_detection_config "$config_file")"
    fi

    # If YOLO is not configured, skip this check
    if [[ "$detection_config" != *"yolo"* ]] && [[ "$detection_config" != *"YOLO"* ]]; then
        return 0
    fi

    local yolo_version="8n"
    if declare -F sf_resolve_yolo_version >/dev/null; then
        yolo_version="$(sf_resolve_yolo_version "$config_file" "$detection_config")"
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

# Function to setup rtp_server-specific environment variables.
# Sets up GStreamer plugin/library paths for VCAI decoder plugin,
# optional local gst-openhevc, DeepStream paths, YOLO lib, CUDA, GST_DEBUG,
# and clears the GStreamer cache.
setup_environment() {
    echo "Setting up environment for GStreamer..."

    # Get VCAI path
    VCAI_PATH=$(get_vcai_path)
    local PROJECT_ROOT="$VCAI_PATH"

    local ARCH_TRIPLET="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo "x86_64-linux-gnu")"
    local VCAI_DECODER_PLUGIN_PATH_1="${PROJECT_ROOT}/build/rtp_server/vcai_decoder"
    local VCAI_DECODER_PLUGIN_PATH_2="${PROJECT_ROOT}/build/vcai_decoder/vcai_decoder"
    local OPENHEVC_PLUGIN_PATH="${PROJECT_ROOT}/lib/openhevc/lib/gstreamer-1.0"
    local OPENHEVC_LIB_PATH="${PROJECT_ROOT}/lib/openhevc/lib"

    # DeepStream plugins are typically installed in the system plugin directory under a 'deepstream' subfolder.
    local DEEPSTREAM_SYSTEM_PLUGIN_PATH="/usr/lib/${ARCH_TRIPLET}/gstreamer-1.0/deepstream"

    # DeepStream library locations (use whichever exists).
    local DEEPSTREAM_SYMLINK_DIR="/opt/nvidia/deepstream/deepstream"
    local DEEPSTREAM_VERSIONED_DIR="/opt/nvidia/deepstream/deepstream-7.1"

    # DeepStream-Yolo custom parser library directory.
    local DEEPSTREAM_YOLO_LIB_DIR="${PROJECT_ROOT}/yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo"

    local gst_plugin_paths=()
    if [ -d "${VCAI_DECODER_PLUGIN_PATH_1}" ]; then
        gst_plugin_paths+=("${VCAI_DECODER_PLUGIN_PATH_1}")
    fi
    if [ -d "${VCAI_DECODER_PLUGIN_PATH_2}" ]; then
        gst_plugin_paths+=("${VCAI_DECODER_PLUGIN_PATH_2}")
    fi
    if [ -d "${OPENHEVC_PLUGIN_PATH}" ]; then
        gst_plugin_paths+=("${OPENHEVC_PLUGIN_PATH}")
    fi
    if [ -d "${DEEPSTREAM_SYSTEM_PLUGIN_PATH}" ]; then
        gst_plugin_paths+=("${DEEPSTREAM_SYSTEM_PLUGIN_PATH}")
    fi
    if [ -d "${DEEPSTREAM_SYMLINK_DIR}/lib/gstreamer-1.0" ]; then
        gst_plugin_paths+=("${DEEPSTREAM_SYMLINK_DIR}/lib/gstreamer-1.0")
    fi
    if [ -d "${DEEPSTREAM_VERSIONED_DIR}/lib/gstreamer-1.0" ]; then
        gst_plugin_paths+=("${DEEPSTREAM_VERSIONED_DIR}/lib/gstreamer-1.0")
    fi

    local ld_library_paths=()
    if [ -d "${OPENHEVC_LIB_PATH}" ]; then
        ld_library_paths+=("${OPENHEVC_LIB_PATH}")
    fi
    if [ -d "${DEEPSTREAM_SYMLINK_DIR}/lib" ]; then
        ld_library_paths+=("${DEEPSTREAM_SYMLINK_DIR}/lib")
    fi
    if [ -d "${DEEPSTREAM_VERSIONED_DIR}/lib" ]; then
        ld_library_paths+=("${DEEPSTREAM_VERSIONED_DIR}/lib")
    fi
    if [ -d "${DEEPSTREAM_YOLO_LIB_DIR}" ]; then
        ld_library_paths+=("${DEEPSTREAM_YOLO_LIB_DIR}")
    fi

    # Export environment (keep local plugin paths first).
    if [ ${#gst_plugin_paths[@]} -gt 0 ]; then
        export GST_PLUGIN_PATH="$(IFS=:; echo "${gst_plugin_paths[*]}")"
    fi
    if [ ${#ld_library_paths[@]} -gt 0 ]; then
        export LD_LIBRARY_PATH="$(IFS=:; echo "${ld_library_paths[*]}")"
    fi
    unset GST_PLUGIN_SCANNER
    echo "Using GStreamer plugins from: ${GST_PLUGIN_PATH}"
    echo "Using runtime libraries from: ${LD_LIBRARY_PATH}"

    export GST_DEBUG_NO_COLOR=1
    export GST_DEBUG=2,rtp_server:5,frame-processing:5,border-drawer:5,nal-utils:5,video-info:0,decoded-frame-processor:5,pipeline:5,server:5,rtp-statistics:5,command-line:5,configuration-manager:5,initialization:5,rtp:5,udp:5,rtpmanager:5,rtpbin:5,session-monitor:5,client-session:5,client-manager:5,area-copy:5,communication-manager:5,transport:5,transport-webrtc:5,signaling:5,gpu-elements:5,display-branch:5

    # Source CUDA version detection script
    local SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    source "$SCRIPT_DIR/set_cuda_ver.sh"

    # Detect and set CUDA version dynamically
    detect_and_set_cuda_version

    # Clear GStreamer registry cache to force a rescan
    # with the correct environment settings.
    echo "Clearing GStreamer cache..."
    rm -rf ~/.cache/gstreamer-1.0
}

# Function to setup model configuration.
setup_model_config() {
    echo "Generating configuration files..."

    # Skip model configuration in help mode
    if [ "$HELP_MODE" = true ]; then
        echo "Skipping model configuration in help mode"
        return
    fi

    # Detect config file
    local config_file="${RTP_SERVER_CONFIG_FILE:-rtp_server_config.ini}"
    if [ ! -f "$config_file" ]; then
        config_file="rtp_server_config.ini"
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
            else
                local version_from_path=""
                version_from_path=$(basename "$DETECTION_CONFIG" | sed -n 's/^yolo_config_\([^\.]*\)\.yml$/\1/p' || true)
                if [ -n "$version_from_path" ]; then
                    yolo_version="$version_from_path"
                fi
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

        # Generate YOLO configuration only if it doesn't exist
        # Ensure VCAI_PATH is set
        if [ -z "$VCAI_PATH" ]; then
            VCAI_PATH=$(get_vcai_path)
        fi

        local yolo_config_file="$VCAI_PATH/yolo/yolo_config_${yolo_version}.yml"

        local regenerate_config=false
        if [ -f "$yolo_config_file" ]; then
            local current_width=""
            local current_height=""
            current_width=$(awk -F: '/^[[:space:]]*width[[:space:]]*:/ {print $2; exit}' "$yolo_config_file" | tr -d ' ')
            current_height=$(awk -F: '/^[[:space:]]*height[[:space:]]*:/ {print $2; exit}' "$yolo_config_file" | tr -d ' ')
            if [ "$current_width" != "$1" ] || [ "$current_height" != "$2" ]; then
                echo "YOLO configuration resolution mismatch (${current_width}x${current_height} vs ${1}x${2}); regenerating."
                regenerate_config=true
            else
                echo "YOLO configuration file already exists: $yolo_config_file"
                echo "  Resolution matches ${1}x${2}; skipping regeneration."
            fi
        else
            echo "YOLO configuration file not found: $yolo_config_file"
            regenerate_config=true
        fi

        if [ "$regenerate_config" = true ]; then
            echo "Generating YOLO configuration..."
            if [ -f "./scripts/generate_yolo_config.sh" ]; then
                ./scripts/generate_yolo_config.sh "$1" "$2" "$yolo_version"
            else
                echo "Warning: generate_yolo_config.sh not found. Using static model-specific YAML config files if they exist."
            fi
        fi

        # Update config to use the model-specific config file regardless of generation
        if [ -f "$config_file" ]; then
            local model_specific_config="../yolo/yolo_config_${yolo_version}.yml"
            if grep -q "^deepstream_config_file.*=.*yolo_config" "$config_file"; then
                if command -v sed >/dev/null 2>&1; then
                    if [[ "$OSTYPE" == "darwin"* ]]; then
                        sed -i '' "s|^deepstream_config_file.*=.*|deepstream_config_file = ${model_specific_config}|" "$config_file"
                    else
                        sed -i "s|^deepstream_config_file.*=.*|deepstream_config_file = ${model_specific_config}|" "$config_file"
                    fi
                    echo "Updated config to use model-specific config: $model_specific_config"
                else
                    echo "Warning: sed not found, cannot automatically update config"
                    echo "Please manually set deepstream_config_file = $model_specific_config in $config_file"
                fi
            else
                echo "Note: Please set deepstream_config_file = $model_specific_config in $config_file"
            fi
        fi
    else
        echo "Warning: Unknown detection model. Using static configuration files if they exist."
    fi
}

# Function to determine model type, build, and prepare execution.
prepare_execution() {
    echo "Preparing execution..."

    # Build rtp_server (unless NO_BUILD=true is set in the environment)
    if [ "${NO_BUILD:-false}" = true ]; then
        echo "Skipping build step (NO_BUILD=true)"
    else
        ./build.sh
    fi

    # Determine model type from config file
    MODEL_NAME="unknown"
    local config_file="${RTP_SERVER_CONFIG_FILE:-rtp_server_config.ini}"
    if [ ! -f "$config_file" ]; then
        config_file="rtp_server_config.ini"
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

    # Create model-specific output directory in VCAI root
    mkdir -p "../$MODEL_NAME"

    echo "Using model: $MODEL_NAME"
}

# Function to find rtp_server binary dynamically.
find_rtp_server_binary() {
    # Get the VCAI path if not already set
    if [ -z "$VCAI_PATH" ]; then
        VCAI_PATH=$(get_vcai_path)
    fi

    # Common locations where the binary might be
    local possible_paths=(
        "$VCAI_PATH/build/rtp_server/rtp_server/rtp_server"
        "$VCAI_PATH/build/rtp_server/bin/rtp_server"
        "$VCAI_PATH/build/rtp_server/rtp_server"
        "../build/rtp_server/rtp_server/rtp_server"
        "../build/rtp_server/bin/rtp_server"
        "../build/rtp_server/rtp_server"
    )

    # Try each path
    for path in "${possible_paths[@]}"; do
        if [ -f "$path" ] && [ -x "$path" ]; then
            echo "$path"
            return 0
        fi
    done

    # If not found, return error
    echo "Error: rtp_server binary not found in any expected location" >&2
    echo "Searched in:" >&2
    for path in "${possible_paths[@]}"; do
        echo "  - $path" >&2
    done
    return 1
}

# Function to execute rtp_server with signal-safe logging.
# Uses a named pipe so that tee does not interfere with signal delivery.
execute_rtp_server() {
    local rtp_server_args=("$@")

    echo "Running rtp_server..."

    # Find the binary dynamically
    local rtp_server_binary
    rtp_server_binary=$(find_rtp_server_binary)
    if [ $? -ne 0 ]; then
        echo "$rtp_server_binary"
        exit 1
    fi

    echo "Using binary: $rtp_server_binary"

    # Clean up previous output file
    rm -f rtp_server_output.txt

    # --- Signal Handling ---
    #
    # IMPORTANT: The pipe (| tee) causes signal handling issues!
    #
    # When using `app | tee log.txt`, both processes are in the same process group.
    # When Ctrl+C (SIGINT) is pressed:
    # 1. Both app and tee receive SIGINT simultaneously
    # 2. tee terminates immediately by default
    # 3. When app tries to write to the broken pipe, it receives SIGPIPE
    # 4. SIGPIPE terminates app immediately, preventing proper cleanup
    #
    # SOLUTION: Use a named pipe.
    #
    # - Runs app in the foreground process group
    # - Runs tee as a separate background process
    # - Only app receives SIGINT/SIGTERM signals
    # - tee continues running, keeping the output stream open
    # - app can handle signals gracefully and perform cleanup
    # - tee exits naturally after app closes its output stream

    echo "Starting rtp_server..."

    local fifo="/tmp/rtp_server_output"

    # Cleanup function for trap
    cleanup() {
        kill "$TEE_PID" 2>/dev/null || true
        rm -f "$fifo"
    }
    trap cleanup EXIT

    # Use named pipe approach to avoid signal interference with logging
    mkfifo "$fifo" 2>/dev/null || true

    # Start tee in background to copy output to file
    tee rtp_server_output.txt < "$fifo" &
    TEE_PID=$!

    # Run the server and redirect output to the named pipe
    "$rtp_server_binary" "${rtp_server_args[@]}" > "$fifo" 2>&1
    SERVER_EXIT_CODE=$?

    # Clean up (trap will also run, but explicit cleanup is fine)
    cleanup
    trap - EXIT

    exit $SERVER_EXIT_CODE
}
