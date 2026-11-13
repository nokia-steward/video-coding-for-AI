#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Consolidated encoder_cli run script with input/output modes.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/scripts/common_encoder_setup.sh"

NO_BUILD=false

input_mode=""
output_mode=""
input_arg=""
output_arg=""
video_file=""
video_width=""
video_height=""
video_framerate=""
camera_device=""
output_host=""
output_port=""
output_signaling_url=""
HELP_MODE=false

show_help() {
    cat << 'EOF'
Usage:
  ./run.sh <input_mode> <output_mode> [<input_arg>] [<output_arg>] [-- <encoder_cli_args...>]
  ./run.sh <input_mode> <output_mode> -o <output_arg> [-- <encoder_cli_args...>]
  ./run.sh -- --help

Summary:
  This script consolidates all encoder_cli run scripts into a single entry point.
  The expected, common usage is to choose input/output modes only and keep all
  configuration in encoder_config.ini. Any overrides can be passed after '--'.

Input modes:
  file    Optional input_arg: path to a video file.
          The filename should include WIDTHxHEIGHT and optionally FPS:
            name_WIDTHxHEIGHT_FPS.ext
            name_WIDTHxHEIGHT.ext (defaults to 30 fps)
  camera  Optional input_arg: camera device (e.g., /dev/video0).
  udp     No input_arg. Uses [UDP] and [Common] sections in encoder_config.ini.

Output modes:
  rtp     Optional output_arg: communication host or host:port.
  webrtc  Optional output_arg:
            - communication host or host:port, or
            - signaling URL (e.g., ws://host:port)

Passing application arguments:
  Use '--' to pass any arguments directly to encoder_cli.
  Example: ./run.sh file rtp /path/to/file -- --debug-all

Disambiguating output arguments:
  If both the input and output modes can take arguments and you only want to pass
  the output argument, insert '-o' before the output argument:
    ./run.sh file rtp -o 192.168.1.100

Examples:
  ./run.sh file rtp /path/to/video_1920x1080_30.yuv
  ./run.sh udp webrtc
  ./run.sh camera rtp /dev/video1
  ./run.sh file webrtc -o ws://127.0.0.1:8080
  ./run.sh file rtp /path/to/video_1920x1080_30.yuv -o 192.168.1.100:5000
  ./run.sh udp webrtc -o 127.0.0.1:5000 -- --webrtc-signaling-url ws://127.0.0.1:8080
  ./run.sh -- --help

Notes:
  - The log file is always: encoder_output.txt
  - If you omit output_arg, the application will use encoder_config.ini.
    Missing Communication settings will cause a startup error.
  - All other legacy run script arguments are not supported.
  - For full encoder_cli options, use:
      ./build/encoder_cli/encoder_cli --help
EOF
}

exit_with_help() {
    local message="$1"
    if [ -n "$message" ]; then
        echo "Error: $message"
        echo ""
    fi
    show_help
    exit 1
}

read_config_value() {
    local key="$1"
    local config_file="$2"
    if [ ! -f "$config_file" ]; then
        return 0
    fi
    awk -F= -v key="$key" '
        /^[[:space:]]*#/ { next }
        $0 ~ "^[[:space:]]*" key "[[:space:]]*=" {
            value=$0
            sub(/^[^=]*=/, "", value)
            gsub(/[[:space:]]+/, "", value)
            print value
            exit
        }
    ' "$config_file"
}

normalize_output_port() {
    if [ -n "$output_port" ]; then
        if ! [[ "$output_port" =~ ^[0-9]+$ ]]; then
            exit_with_help "Communication port must be a number, got: $output_port"
        fi
        if [ "$output_port" -lt 1 ] || [ "$output_port" -gt 65535 ]; then
            exit_with_help "Communication port must be between 1-65535, got: $output_port"
        fi
    fi
}

load_common_dimensions() {
    local config_file="$1"
    if [ -z "$video_width" ]; then
        video_width="$(read_config_value "width" "$config_file")"
    fi
    if [ -z "$video_height" ]; then
        video_height="$(read_config_value "height" "$config_file")"
    fi
    if [ -z "$video_framerate" ]; then
        video_framerate="$(read_config_value "framerate" "$config_file")"
    fi
}

parse_video_filename() {
    local filename="$1"
    if [[ "$filename" =~ _([0-9]+)x([0-9]+)_([0-9]+)\. ]]; then
        video_width="${BASH_REMATCH[1]}"
        video_height="${BASH_REMATCH[2]}"
        video_framerate="${BASH_REMATCH[3]}"
        return 0
    fi
    if [[ "$filename" =~ _([0-9]+)x([0-9]+)\. ]]; then
        video_width="${BASH_REMATCH[1]}"
        video_height="${BASH_REMATCH[2]}"
        video_framerate="30"
        return 0
    fi
    return 1
}

parse_host_port() {
    local value="$1"
    local host_candidate="${value%:*}"
    local port_candidate="${value##*:}"

    if [ "$host_candidate" = "$value" ]; then
        output_host="$value"
        output_port=""
        return 0
    fi

    if [[ "$port_candidate" =~ ^[0-9]+$ ]]; then
        if [ "$port_candidate" -lt 1 ] || [ "$port_candidate" -gt 65535 ]; then
            exit_with_help "Output port must be between 1-65535, got: $port_candidate"
        fi
        output_host="$host_candidate"
        output_port="$port_candidate"
        return 0
    fi

    exit_with_help "Output argument must be a host or host:port"
}

if [ "$#" -ge 2 ] && [ "$1" = "--" ] && { [ "$2" = "--help" ] || [ "$2" = "-h" ]; }; then
    setup_environment
    execute_encoder_cli "$2"
    exit 0
fi

if [ "$#" -eq 0 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    show_help
    exit 0
fi

input_mode="$1"
output_mode="$2"
shift 2

if [ -z "$input_mode" ] || [ -z "$output_mode" ]; then
    exit_with_help "Both input_mode and output_mode are required."
fi

case "$input_mode" in
    file|camera|udp) ;;
    *) exit_with_help "Invalid input_mode: $input_mode" ;;
esac

case "$output_mode" in
    rtp|webrtc) ;;
    *) exit_with_help "Invalid output_mode: $output_mode" ;;
esac

script_args=()
app_args=()
in_app_args=false
for arg in "$@"; do
    if [ "$in_app_args" = false ] && [ "$arg" = "--" ]; then
        in_app_args=true
        continue
    fi
    if [ "$in_app_args" = true ]; then
        app_args+=("$arg")
    else
        script_args+=("$arg")
    fi
done

if [ "${#app_args[@]}" -eq 1 ] && { [ "${app_args[0]}" = "--help" ] || [ "${app_args[0]}" = "-h" ]; }; then
    HELP_MODE=true
fi

if [ "${#script_args[@]}" -gt 0 ] && [ "${script_args[0]}" = "-o" ]; then
    if [ "${#script_args[@]}" -lt 2 ]; then
        exit_with_help "Missing output argument after -o."
    fi
    if [ "${#script_args[@]}" -gt 2 ]; then
        exit_with_help "Too many script arguments. Use '--' to pass application args."
    fi
    output_arg="${script_args[1]}"
else
    if [ "$input_mode" = "file" ] || [ "$input_mode" = "camera" ]; then
        if [ "${#script_args[@]}" -gt 0 ]; then
            input_arg="${script_args[0]}"
        fi
        if [ "${#script_args[@]}" -gt 1 ]; then
            output_arg="${script_args[1]}"
        fi
        if [ "${#script_args[@]}" -gt 2 ]; then
            exit_with_help "Too many script arguments. Use '--' to pass application args."
        fi
    else
        if [ "${#script_args[@]}" -gt 0 ]; then
            output_arg="${script_args[0]}"
        fi
        if [ "${#script_args[@]}" -gt 1 ]; then
            exit_with_help "Too many script arguments. Use '--' to pass application args."
        fi
    fi
fi

ENCODER_CONFIG_FILE="$(detect_config_file "${app_args[@]}")"

if [ "$input_mode" = "file" ]; then
    if [ -n "$input_arg" ]; then
        video_file="$input_arg"
        if [ "$HELP_MODE" = false ] && [ ! -f "$video_file" ]; then
            exit_with_help "Video file '$video_file' not found."
        fi

        if [ -n "$video_file" ]; then
            filename="$(basename "$video_file")"
            if ! parse_video_filename "$filename"; then
                exit_with_help "Filename '$filename' does not match NAME_WIDTHxHEIGHT[_FPS].ext"
            fi
        fi
    fi
elif [ "$input_mode" = "camera" ]; then
    if [ -n "$input_arg" ]; then
        camera_device="$input_arg"
        if [ "$HELP_MODE" = false ] && [ ! -e "$camera_device" ]; then
            exit_with_help "Camera device '$camera_device' not found."
        fi
    fi
fi

if [ -n "$ENCODER_CONFIG_FILE" ]; then
    load_common_dimensions "$ENCODER_CONFIG_FILE"
fi

if [ -n "$output_arg" ]; then
    if [ "$output_mode" = "webrtc" ] && [[ "$output_arg" == *"://"* ]]; then
        output_signaling_url="$output_arg"
    else
        if [[ "$output_arg" == *"://"* ]]; then
            exit_with_help "Output argument must be a host or host:port for rtp mode."
        fi
        parse_host_port "$output_arg"
    fi
fi

normalize_output_port

echo "Input mode: $input_mode"
[ -n "$input_arg" ] && echo "Input argument: $input_arg"
echo "Output mode: $output_mode"
[ -n "$output_arg" ] && echo "Output argument: $output_arg"

setup_environment

check_and_setup_yolo "$video_width" "$video_height"
setup_model_config "$video_width" "$video_height"
prepare_execution
ensure_adaptive_luma_onnx || exit 1

ENCODER_OUTPUT_FILE="encoder_output.txt"

if [ "$HELP_MODE" = true ]; then
    execute_encoder_cli "${app_args[@]}"
    exit 0
fi

encoder_args=()
encoder_args+=(--common-input-type "$input_mode")

if [ "$input_mode" = "file" ]; then
    [ -n "$video_file" ] && encoder_args+=(--video-file-name "$video_file")
    [ -n "$video_width" ] && encoder_args+=(--width "$video_width")
    [ -n "$video_height" ] && encoder_args+=(--height "$video_height")
    [ -n "$video_framerate" ] && encoder_args+=(--framerate "$video_framerate")
elif [ "$input_mode" = "camera" ]; then
    [ -n "$camera_device" ] && encoder_args+=(--camera-device "$camera_device")
fi

if [ "$output_mode" = "rtp" ]; then
    encoder_args+=(--communication-transport-type "rtp")
else
    encoder_args+=(--communication-transport-type "webrtc")
fi

if [ -n "$output_signaling_url" ]; then
    encoder_args+=(--webrtc-signaling-url "$output_signaling_url")
fi

if [ -n "$output_host" ]; then
    encoder_args+=(--communication-host "$output_host")
fi

if [ -n "$output_port" ]; then
    encoder_args+=(--communication-port "$output_port")
fi

if [ -n "$RESOLVED_INPUT_ANALYZER_MODEL_PATH" ]; then
    encoder_args+=(--input-analyzer-model-path "$RESOLVED_INPUT_ANALYZER_MODEL_PATH")
fi

encoder_args+=("${app_args[@]}")
execute_encoder_cli "${encoder_args[@]}"
