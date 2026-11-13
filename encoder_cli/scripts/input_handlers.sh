#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Input-specific handlers for encoder_cli run scripts
# This library contains functions for parsing different input types

# Global variables for input handling
VIDEO_FILE=""
VIDEO_WIDTH=""
VIDEO_HEIGHT=""
VIDEO_FRAMERATE=""
CAMERA_DEVICE=""
CAMERA_WIDTH=""
CAMERA_HEIGHT=""
CAMERA_FRAMERATE=""
UDP_BIND_ADDRESS=""
UDP_PORT=""
UDP_WIDTH=""
UDP_HEIGHT=""
UDP_FRAMERATE=""
RTP_HOST=""
RTP_PORT=""
ADDITIONAL_ARGS=()
HELP_MODE=false
NO_BUILD=false

# Function to parse common flags like --no-build
parse_common_flags() {
    local args=("$@")
    local filtered_args=()

    for arg in "${args[@]}"; do
        case "$arg" in
            --no-build)
                NO_BUILD=true
                echo "Build step will be skipped (--no-build flag detected)"
                ;;
            *)
                filtered_args+=("$arg")
                ;;
        esac
    done

    # Set the filtered arguments as a global variable
    FILTERED_ARGS=("${filtered_args[@]}")
}

# Function to parse video file input
parse_video_file() {
    local args=("$@")

    # Parse common flags first
    parse_common_flags "${args[@]}"
    local filtered_args=("${FILTERED_ARGS[@]}")
    local arg_count=${#filtered_args[@]}

    # Handle help option
    if [ "$arg_count" -eq 1 ] && [[ "${filtered_args[0]}" == "--help" || "${filtered_args[0]}" == "-h" ]]; then
        echo "Usage: $0 <video_file> [-- <additional_args...>]"
        echo "Video properties will be parsed from filename"
        echo "Expected format: NAME_WIDTHxHEIGHT_FPS.extension"
        echo ""
        echo "Arguments:"
        echo "  video_file    Path to the video file (required)"
        echo "  additional_args Additional arguments passed to encoder_cli (optional)"
        echo ""
        echo "Examples:"
        echo "  $0 /path/to/video_832x480_50.yuv"
        echo "  $0 /path/to/video_1920x1080_30.mp4"
        echo "  $0 /path/to/video_832x480_50.yuv -- --logging-roi --debug-all"
        echo ""
        show_common_help "$0" "<video_file> [-- <additional_args...>]"
        exit 0
    fi

    # Check if we're being called with -- --help (no video file needed)
    if [ "$arg_count" -eq 2 ] && [ "${filtered_args[0]}" = "--" ] && [ "${filtered_args[1]}" = "--help" ]; then
        echo "Passing --help to encoder_cli application..."
        echo ""
        VIDEO_FILE=""
        ADDITIONAL_ARGS=("--help")
        HELP_MODE=true
        return
    fi

    # Check if video file parameter is provided
    if [ "$arg_count" -lt 1 ]; then
        echo "Usage: $0 <video_file> [-- <additional_args...>]"
        echo "Video properties will be parsed from filename"
        echo "Expected format: NAME_WIDTHxHEIGHT_FPS.extension"
        echo ""
        echo "Arguments:"
        echo "  video_file    Path to the video file (required)"
        echo "  additional_args Additional arguments passed to encoder_cli (optional)"
        echo ""
        echo "Examples:"
        echo "  $0 /path/to/video_832x480_50.yuv"
        echo "  $0 /path/to/video_1920x1080_30.mp4"
        echo "  $0 /path/to/video_832x480_50.yuv -- --logging-roi --debug-all"
        echo ""
        show_common_help "$0" "<video_file> [-- <additional_args...>]"
        echo ""
        echo "To see encoder_cli help without a video file:"
        echo "Example: $0 -- --help"
        exit 1
    fi

    VIDEO_FILE="${filtered_args[0]}"
    # Parse arguments after the video file
    local remaining_args=("${filtered_args[@]:1}")  # Skip the first argument (video file)
    ADDITIONAL_ARGS=()

    # Look for '--' separator
    local i=0
    while [ $i -lt ${#remaining_args[@]} ]; do
        if [ "${remaining_args[$i]}" = "--" ]; then
            # Everything after '--' goes to the application
            ADDITIONAL_ARGS+=("${remaining_args[@]:$((i+1))}")
            break
        else
            echo "Error: Unexpected argument '${remaining_args[$i]}'. Use '--' to separate script parameters from application parameters."
            echo "Example: $0 $VIDEO_FILE -- --logging-roi --debug-all"
            echo ""
            echo "If you want to pass --help to the encoder_cli application, use:"
            echo "Example: $0 <video_file> -- --help"
            echo "Or without a video file:"
            echo "Example: $0 -- --help"
            exit 1
        fi
        i=$((i+1))
    done

    # Check if video file exists (skip if we're in help mode)
    if [ -n "$VIDEO_FILE" ] && [ ! -f "$VIDEO_FILE" ]; then
        echo "Error: Video file '$VIDEO_FILE' not found"
        exit 1
    fi

    if [ -n "$VIDEO_FILE" ]; then
        echo "Video file: $VIDEO_FILE"

        # Extract video properties from filename
        echo "Parsing video properties from filename..."

        # Extract filename from full path
        local filename=$(basename "$VIDEO_FILE")

        # Parse video properties using regex
        # Expected format: NAME_WIDTHxHEIGHT_FPS.extension or NAME_WIDTHxHEIGHT.extension
        if [[ "$filename" =~ _([0-9]+)x([0-9]+)_([0-9]+)\. ]]; then
            VIDEO_WIDTH="${BASH_REMATCH[1]}"
            VIDEO_HEIGHT="${BASH_REMATCH[2]}"
            VIDEO_FRAMERATE="${BASH_REMATCH[3]}"
        elif [[ "$filename" =~ _([0-9]+)x([0-9]+)\. ]]; then
            VIDEO_WIDTH="${BASH_REMATCH[1]}"
            VIDEO_HEIGHT="${BASH_REMATCH[2]}"
            VIDEO_FRAMERATE="30" # Default framerate if not specified
        else
            echo "Error: Filename '$filename' does not match expected pattern (NAME_WIDTHxHEIGHT_FPS.extension or NAME_WIDTHxHEIGHT.extension)"
            exit 1
        fi

        echo "Parsed video properties from filename:"
        echo "  Resolution: ${VIDEO_WIDTH}x${VIDEO_HEIGHT}"
        [ -n "$VIDEO_FRAMERATE" ] && echo "  Framerate: ${VIDEO_FRAMERATE} fps" || true
    else
        echo "Running in help mode (no video file required)"
        # No default values - will use config file or application defaults
    fi
}

# Function to parse camera input parameters
parse_camera_params() {
    local args=("$@")

    # Parse common flags first
    parse_common_flags "${args[@]}"
    local filtered_args=("${FILTERED_ARGS[@]}")
    local arg_count=${#filtered_args[@]}

    # Initialize variables as empty (no defaults)
    CAMERA_DEVICE=""
    CAMERA_WIDTH=""
    CAMERA_HEIGHT=""
    CAMERA_FRAMERATE=""

    # Handle help option
    if [ "$arg_count" -eq 1 ] && [[ "${filtered_args[0]}" == "--help" || "${filtered_args[0]}" == "-h" ]]; then
        echo "Usage: $0 [camera_device] [width] [height] [framerate] [-- <additional_args...>]"
        echo ""
        echo "Arguments:"
        echo "  camera_device  Camera device path (optional, uses config file or application defaults)"
        echo "  width          Camera width in pixels (optional, uses config file or application defaults)"
        echo "  height         Camera height in pixels (optional, uses config file or application defaults)"
        echo "  framerate      Camera framerate in fps (optional, uses config file or application defaults)"
        echo "  additional_args Additional arguments passed to encoder_cli (optional)"
        echo ""
        echo "Examples:"
        echo "  $0                                    # Use config file or application defaults"
        echo "  $0 /dev/video1                       # Use different camera device"
        echo "  $0 /dev/video0 1920 1080             # Use different resolution"
        echo "  $0 /dev/video0 1920 1080 30          # Use different resolution and framerate"
        echo "  $0 /dev/video0 1920 1080 30 -- --logging-roi --debug-all  # With additional args"
        echo "  $0 /dev/video2 -- --debug-enable-display  # Skip optional params, go straight to app args"
        echo ""
        echo "Common camera devices:"
        echo "  /dev/video0  - First camera device"
        echo "  /dev/video1  - Second camera device"
        echo "  /dev/video2  - Third camera device"
        echo ""
        echo "Common resolutions:"
        echo "  640x480   - VGA"
        echo "  1280x960  - SXGA"
        echo "  1920x1080 - Full HD"
        echo ""
        echo "Common framerates:"
        echo "  15, 25, 30, 60 fps"
        echo ""
        show_common_help "$0" "[camera_device] [width] [height] [framerate] [-- <additional_args...>]"
        exit 0
    fi

    # Check if we're being called with -- --help (no camera parameters needed)
    if [ "$arg_count" -eq 2 ] && [ "${filtered_args[0]}" = "--" ] && [ "${filtered_args[1]}" = "--help" ]; then
        echo "Passing --help to encoder_cli application..."
        echo ""
        CAMERA_DEVICE=""
        CAMERA_WIDTH=""
        CAMERA_HEIGHT=""
        CAMERA_FRAMERATE=""
        ADDITIONAL_ARGS=("--help")
        HELP_MODE=true
        return
    fi

    # Check if arguments start with '--' (flag-style arguments) - pass them directly to encoder_cli
    if [ "$arg_count" -gt 0 ] && [[ "${filtered_args[0]}" == --* ]]; then
        # All arguments are flag-style, pass them directly to encoder_cli
        ADDITIONAL_ARGS=("${filtered_args[@]}")
        return
    fi

    # Parse camera-specific arguments (positional style)
    ADDITIONAL_ARGS=()
    local arg_index=0

    # Parse arguments until we hit '--' or run out of arguments
    while [ "$arg_index" -lt "$arg_count" ]; do
        if [ "${filtered_args[$arg_index]}" = "--" ]; then
            # Found '--' separator, everything after goes to the application
            ADDITIONAL_ARGS+=("${filtered_args[@]:$((arg_index+1))}")
            break
        else
            # This is a script parameter
            case $arg_index in
                0) CAMERA_DEVICE="${filtered_args[$arg_index]}" ;;
                1) CAMERA_WIDTH="${filtered_args[$arg_index]}" ;;
                2) CAMERA_HEIGHT="${filtered_args[$arg_index]}" ;;
                3) CAMERA_FRAMERATE="${filtered_args[$arg_index]}" ;;
                *) echo "Error: Too many script parameters. Use '--' to separate script parameters from application parameters."
                   echo "Example: $0 $CAMERA_DEVICE $CAMERA_WIDTH $CAMERA_HEIGHT $CAMERA_FRAMERATE -- --logging-roi --debug-all"
                   exit 1 ;;
            esac
            arg_index=$((arg_index + 1))
        fi
    done

    # Validate camera device only if provided
    if [ -n "$CAMERA_DEVICE" ] && [ ! -e "$CAMERA_DEVICE" ]; then
        echo "Error: Camera device '$CAMERA_DEVICE' not found"
        echo "Available video devices:"
        ls -la /dev/video* 2>/dev/null || echo "  No video devices found"
        echo ""
        echo "Please check:"
        echo "  1. Camera is connected"
        echo "  2. Camera drivers are installed"
        echo "  3. User has permission to access the device"
        echo "  4. Device path is correct"
        exit 1
    fi

    # Validate numeric parameters only if provided
    if [ -n "$CAMERA_WIDTH" ] && (! [[ "$CAMERA_WIDTH" =~ ^[0-9]+$ ]] || [ "$CAMERA_WIDTH" -lt 1 ]); then
        echo "Error: Camera width must be a positive integer, got: $CAMERA_WIDTH"
        exit 1
    fi

    if [ -n "$CAMERA_HEIGHT" ] && (! [[ "$CAMERA_HEIGHT" =~ ^[0-9]+$ ]] || [ "$CAMERA_HEIGHT" -lt 1 ]); then
        echo "Error: Camera height must be a positive integer, got: $CAMERA_HEIGHT"
        exit 1
    fi

    if [ -n "$CAMERA_FRAMERATE" ] && (! [[ "$CAMERA_FRAMERATE" =~ ^[0-9]+$ ]] || [ "$CAMERA_FRAMERATE" -lt 1 ]); then
        echo "Error: Camera framerate must be a positive integer, got: $CAMERA_FRAMERATE"
        exit 1
    fi

    # Only print configuration for provided parameters
    if [ -n "$CAMERA_DEVICE" ] || [ -n "$CAMERA_WIDTH" ] || [ -n "$CAMERA_HEIGHT" ] || [ -n "$CAMERA_FRAMERATE" ]; then
        echo "Camera configuration:"
        [ -n "$CAMERA_DEVICE" ] && echo "  Device: $CAMERA_DEVICE" || true
        [ -n "$CAMERA_WIDTH" ] && [ -n "$CAMERA_HEIGHT" ] && echo "  Resolution: ${CAMERA_WIDTH}x${CAMERA_HEIGHT}" || true
        [ -n "$CAMERA_FRAMERATE" ] && echo "  Framerate: ${CAMERA_FRAMERATE} fps" || true
    fi
}

# Function to parse RTP parameters
parse_rtp_params() {
    local args=("$@")

    # Parse common flags first
    parse_common_flags "${args[@]}"
    local filtered_args=("${FILTERED_ARGS[@]}")
    local arg_count=${#filtered_args[@]}

    # Initialize variables as empty (no defaults)
    RTP_HOST=""
    RTP_PORT=""

    # Parse RTP-specific arguments
    if [ "$arg_count" -ge 1 ]; then
        RTP_HOST="${filtered_args[0]}"
    fi

    if [ "$arg_count" -ge 2 ]; then
        RTP_PORT="${filtered_args[1]}"
    fi

    # Validate RTP port only if provided
    if [ -n "$RTP_PORT" ] && (! [[ "$RTP_PORT" =~ ^[0-9]+$ ]] || [ "$RTP_PORT" -lt 1 ] || [ "$RTP_PORT" -gt 65535 ]); then
        echo "Error: RTP port must be a positive integer between 1-65535, got: $RTP_PORT"
        exit 1
    fi

    # Only print configuration for provided parameters
    if [ -n "$RTP_HOST" ] || [ -n "$RTP_PORT" ]; then
        echo "RTP configuration:"
        [ -n "$RTP_HOST" ] && echo "  Host: $RTP_HOST" || true
        [ -n "$RTP_PORT" ] && echo "  Port: $RTP_PORT" || true
    fi
}

# Function to parse camera + RTP parameters
parse_camera_rtp_params() {
    local args=("$@")

    # Parse common flags first
    parse_common_flags "${args[@]}"
    local filtered_args=("${FILTERED_ARGS[@]}")
    local arg_count=${#filtered_args[@]}

    # Initialize variables as empty (no defaults)
    CAMERA_DEVICE=""
    CAMERA_WIDTH=""
    CAMERA_HEIGHT=""
    CAMERA_FRAMERATE=""
    RTP_HOST=""
    RTP_PORT=""

    # Handle help option
    if [ "$arg_count" -eq 1 ] && [[ "${filtered_args[0]}" == "--help" || "${filtered_args[0]}" == "-h" ]]; then
        echo "Usage: $0 [camera_device] [width] [height] [framerate] [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]"
        echo ""
        echo "Arguments:"
        echo "  camera_device  Camera device path (optional, uses config file or application defaults)"
        echo "  width          Camera width in pixels (optional, uses config file or application defaults)"
        echo "  height         Camera height in pixels (optional, uses config file or application defaults)"
        echo "  framerate      Camera framerate in fps (optional, uses config file or application defaults)"
        echo "  rtp_host       Communication server host address (optional, uses config file)"
        echo "  rtp_port       Communication server port (optional, uses config file)"
        echo "  --webrtc       Enable WebRTC transport mode (optional, overrides config file)"
        echo "  additional_args Additional arguments passed to encoder_cli (optional)"
        echo ""
        echo "Examples:"
        echo "  $0                                    # Use config file or application defaults"
        echo "  $0 /dev/video1                       # Use different camera device"
        echo "  $0 /dev/video0 1920 1080             # Use different resolution"
        echo "  $0 /dev/video0 1920 1080 30          # Use different resolution and framerate"
        echo "  $0 /dev/video0 1920 1080 30 127.0.0.1  # Use different communication host"
        echo "  $0 /dev/video0 1920 1080 30 127.0.0.1 6000  # Use different RTP host and port"
        echo "  $0 /dev/video0 1920 1080 30 127.0.0.1 6000 -- --logging-roi --debug-all  # With additional args"
        echo "  $0 /dev/video0 1920 1080 30 127.0.0.1 6000 --webrtc -- --webrtc-signaling-url ws://127.0.0.1:8080  # With WebRTC"
        echo "  $0 /dev/video2 -- --debug-enable-display  # Skip optional params, go straight to app args"
        echo ""
        echo "Common camera devices:"
        echo "  /dev/video0  - First camera device"
        echo "  /dev/video1  - Second camera device"
        echo "  /dev/video2  - Third camera device"
        echo ""
        echo "Common resolutions:"
        echo "  640x480   - VGA"
        echo "  1280x960  - SXGA"
        echo "  1920x1080 - Full HD"
        echo ""
        echo "Common framerates:"
        echo "  15, 25, 30, 60 fps"
        echo ""
        echo "Communication server examples:"
        echo "  127.0.0.1:5000  - Localhost"
        echo "  127.0.0.1:5000  - Localhost"
        echo "  0.0.0.0:5000  - All interfaces (server mode)"
        echo ""
        echo "Transport Types:"
        echo "  The encoder supports two transport types:"
        echo "    udp    - UDP transport with binary protocol (default)"
        echo "    webrtc - WebRTC transport with signaling server"
        echo ""
        echo "  For WebRTC:"
        echo "    - Use --webrtc flag or pass --communication-transport-type webrtc after '--'"
        echo "    - Configure via command line: --webrtc-signaling-url, --webrtc-stun-server, etc."
        echo "    - Or configure in encoder_config.ini [Communication] and [WebRTC] sections"
        echo "    - The rtp_host and rtp_port parameters are still used for handshake"
        echo ""
        show_common_help "$0" "[camera_device] [width] [height] [framerate] [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]"
        exit 0
    fi

    # Check if we're being called with -- --help (no camera/RTP parameters needed)
    if [ "$arg_count" -eq 2 ] && [ "${filtered_args[0]}" = "--" ] && [ "${filtered_args[1]}" = "--help" ]; then
        echo "Passing --help to encoder_cli application..."
        echo ""
        CAMERA_DEVICE=""
        CAMERA_WIDTH=""
        CAMERA_HEIGHT=""
        CAMERA_FRAMERATE=""
        RTP_HOST=""
        RTP_PORT=""
        ADDITIONAL_ARGS=("--help")
        HELP_MODE=true
        return
    fi

    # Check if arguments start with '--' (flag-style arguments) - pass them directly to encoder_cli
    if [ "$arg_count" -gt 0 ] && [[ "${filtered_args[0]}" == --* ]]; then
        # All arguments are flag-style, pass them directly to encoder_cli
        ADDITIONAL_ARGS=("${filtered_args[@]}")
        return
    fi

    # Parse camera and RTP-specific arguments (positional style)
    ADDITIONAL_ARGS=()
    local arg_index=0
    local positional_index=0  # Track actual positional argument index (excluding --webrtc)

    # Parse arguments until we hit '--' or run out of arguments
    while [ "$arg_index" -lt "$arg_count" ]; do
        if [ "${filtered_args[$arg_index]}" = "--" ]; then
            # Found '--' separator, everything after goes to the application
            # Append to ADDITIONAL_ARGS (don't replace, in case --webrtc was already added)
            ADDITIONAL_ARGS+=("${filtered_args[@]:$((arg_index+1))}")
            break
        elif [ "${filtered_args[$arg_index]}" = "--webrtc" ]; then
            # --webrtc flag is handled by run script, just pass it through
            ADDITIONAL_ARGS+=("${filtered_args[$arg_index]}")
            arg_index=$((arg_index + 1))
            # Don't increment positional_index, skip this argument
        else
            # This is a script parameter
            case $positional_index in
                0) CAMERA_DEVICE="${filtered_args[$arg_index]}" ;;
                1) CAMERA_WIDTH="${filtered_args[$arg_index]}" ;;
                2) CAMERA_HEIGHT="${filtered_args[$arg_index]}" ;;
                3) CAMERA_FRAMERATE="${filtered_args[$arg_index]}" ;;
                4) RTP_HOST="${filtered_args[$arg_index]}" ;;
                5) RTP_PORT="${filtered_args[$arg_index]}" ;;
                *) echo "Error: Too many script parameters. Use '--' to separate script parameters from application parameters."
                   echo "Example: $0 $CAMERA_DEVICE $CAMERA_WIDTH $CAMERA_HEIGHT $CAMERA_FRAMERATE $RTP_HOST $RTP_PORT -- --logging-roi --debug-all"
                   echo "Or use --webrtc flag: $0 $CAMERA_DEVICE $CAMERA_WIDTH $CAMERA_HEIGHT $CAMERA_FRAMERATE $RTP_HOST $RTP_PORT --webrtc -- --webrtc-signaling-url ws://127.0.0.1:8080"
                   exit 1 ;;
            esac
            arg_index=$((arg_index + 1))
            positional_index=$((positional_index + 1))
        fi
    done

    # Validate camera device only if provided
    if [ -n "$CAMERA_DEVICE" ] && [ ! -e "$CAMERA_DEVICE" ]; then
        echo "Error: Camera device '$CAMERA_DEVICE' not found"
        echo "Available video devices:"
        ls -la /dev/video* 2>/dev/null || echo "  No video devices found"
        echo ""
        echo "Please check:"
        echo "  1. Camera is connected"
        echo "  2. Camera drivers are installed"
        echo "  3. User has permission to access the device"
        echo "  4. Device path is correct"
        exit 1
    fi

    # Validate numeric parameters only if provided
    if [ -n "$CAMERA_WIDTH" ] && (! [[ "$CAMERA_WIDTH" =~ ^[0-9]+$ ]] || [ "$CAMERA_WIDTH" -lt 1 ]); then
        echo "Error: Camera width must be a positive integer, got: $CAMERA_WIDTH"
        exit 1
    fi

    if [ -n "$CAMERA_HEIGHT" ] && (! [[ "$CAMERA_HEIGHT" =~ ^[0-9]+$ ]] || [ "$CAMERA_HEIGHT" -lt 1 ]); then
        echo "Error: Camera height must be a positive integer, got: $CAMERA_HEIGHT"
        exit 1
    fi

    if [ -n "$CAMERA_FRAMERATE" ] && (! [[ "$CAMERA_FRAMERATE" =~ ^[0-9]+$ ]] || [ "$CAMERA_FRAMERATE" -lt 1 ]); then
        echo "Error: Camera framerate must be a positive integer, got: $CAMERA_FRAMERATE"
        exit 1
    fi

    if [ -n "$RTP_PORT" ] && (! [[ "$RTP_PORT" =~ ^[0-9]+$ ]] || [ "$RTP_PORT" -lt 1 ] || [ "$RTP_PORT" -gt 65535 ]); then
        echo "Error: RTP port must be a positive integer between 1-65535, got: $RTP_PORT"
        exit 1
    fi

    # Only print configuration for provided parameters
    if [ -n "$CAMERA_DEVICE" ] || [ -n "$CAMERA_WIDTH" ] || [ -n "$CAMERA_HEIGHT" ] || [ -n "$CAMERA_FRAMERATE" ]; then
        echo "Camera configuration:"
        [ -n "$CAMERA_DEVICE" ] && echo "  Device: $CAMERA_DEVICE"
        [ -n "$CAMERA_WIDTH" ] && [ -n "$CAMERA_HEIGHT" ] && echo "  Resolution: ${CAMERA_WIDTH}x${CAMERA_HEIGHT}"
        [ -n "$CAMERA_FRAMERATE" ] && echo "  Framerate: ${CAMERA_FRAMERATE} fps"
    fi
    if [ -n "$RTP_HOST" ] || [ -n "$RTP_PORT" ]; then
        echo "RTP configuration:"
        [ -n "$RTP_HOST" ] && echo "  Host: $RTP_HOST"
        [ -n "$RTP_PORT" ] && echo "  Port: $RTP_PORT"
    fi
}

# Function to parse video file + RTP parameters
parse_video_rtp_params() {
    local args=("$@")

    # Parse common flags first
    parse_common_flags "${args[@]}"
    local filtered_args=("${FILTERED_ARGS[@]}")
    local arg_count=${#filtered_args[@]}

    # Default RTP parameters
    local DEFAULT_RTP_HOST="127.0.0.1"
    local DEFAULT_RTP_PORT="5000"

    RTP_HOST="$DEFAULT_RTP_HOST"
    RTP_PORT="$DEFAULT_RTP_PORT"
    VIDEO_FILE=""

    # Handle help option
    if [ "$arg_count" -eq 1 ] && [[ "${filtered_args[0]}" == "--help" || "${filtered_args[0]}" == "-h" ]]; then
        echo "Usage: $0 <video_file> [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]"
        echo ""
        echo "Arguments:"
        echo "  video_file    Path to the video file (required)"
        echo "  rtp_host      Communication server host address (optional, uses config file)"
        echo "  rtp_port      Communication server port (optional, uses config file)"
        echo "  --webrtc      Enable WebRTC transport mode (optional, overrides config file)"
        echo "  additional_args Additional arguments passed to encoder_cli (optional)"
        echo ""
        echo "Examples:"
        echo "  $0 /path/to/video_832x480_50.yuv"
        echo "  $0 /path/to/video_832x480_50.yuv 127.0.0.1"
        echo "  $0 /path/to/video_832x480_50.yuv 127.0.0.1 6000"
        echo "  $0 /path/to/video_832x480_50.yuv 127.0.0.1 6000 -- --logging-roi --debug-all"
        echo "  $0 /path/to/video_832x480_50.yuv 127.0.0.1 6000 --webrtc -- --webrtc-signaling-url ws://127.0.0.1:8080"
        echo ""
        echo "Video properties will be parsed from filename"
        echo "Expected format: NAME_WIDTHxHEIGHT_FPS.extension"
        echo "Example: $0 /path/to/video_832x480_50.yuv"
        echo "Example: $0 /path/to/video_1920x1080_30.mp4"
        echo ""
        echo "Transport Types:"
        echo "  The encoder supports two transport types:"
        echo "    udp    - UDP transport with binary protocol (default)"
        echo "    webrtc - WebRTC transport with signaling server"
        echo ""
        echo "  For WebRTC:"
        echo "    - Use --webrtc flag or pass --communication-transport-type webrtc after '--'"
        echo "    - Configure via command line: --webrtc-signaling-url, --webrtc-stun-server, etc."
        echo "    - Or configure in encoder_config.ini [Communication] and [WebRTC] sections"
        echo "    - The rtp_host and rtp_port parameters are still used for handshake"
        echo ""
        show_common_help "$0" "<video_file> [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]"
        exit 0
    fi

    # Check if we're being called with -- --help (no video file needed)
    if [ "$arg_count" -eq 2 ] && [ "${filtered_args[0]}" = "--" ] && [ "${filtered_args[1]}" = "--help" ]; then
        echo "Passing --help to encoder_cli application..."
        echo ""
        VIDEO_FILE=""
        RTP_HOST=""
        RTP_PORT=""
        ADDITIONAL_ARGS=("--help")
        HELP_MODE=true
        return
    fi

    # Parse arguments
    if [ "$arg_count" -lt 1 ]; then
        echo "Usage: $0 <video_file> [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]"
        echo ""
        echo "Arguments:"
        echo "  video_file    Path to the video file (required)"
        echo "  rtp_host      Communication server host address (optional, uses config file)"
        echo "  rtp_port      Communication server port (optional, uses config file)"
        echo "  --webrtc      Enable WebRTC transport mode"
        echo "  additional_args Additional arguments passed to encoder_cli (optional)"
        echo ""
        echo "Examples:"
        echo "  $0 /path/to/video_832x480_50.yuv"
        echo "  $0 /path/to/video_832x480_50.yuv 127.0.0.1"
        echo "  $0 /path/to/video_832x480_50.yuv 127.0.0.1 6000"
        echo "  $0 /path/to/video_832x480_50.yuv --webrtc -- --webrtc-signaling-url ws://127.0.0.1:8080"
        echo "  $0 /path/to/video_832x480_50.yuv 127.0.0.1 6000 -- --logging-roi --debug-all"
        echo ""
        echo "Transport Types:"
        echo "  The encoder supports two transport types:"
        echo "    udp    - UDP transport with binary protocol (default)"
        echo "    webrtc - WebRTC transport with signaling server"
        echo ""
        echo "  For WebRTC:"
        echo "    - Use --webrtc flag or pass --communication-transport-type webrtc after '--'"
        echo "    - Configure via command line: --webrtc-signaling-url, --webrtc-stun-server, etc."
        echo "    - Or configure in encoder_config.ini [Communication] and [WebRTC] sections"
        echo "    - The rtp_host and rtp_port parameters are still used for handshake"
        echo ""
        echo "WebRTC Options (pass after '--'):"
        echo "  --communication-transport-type webrtc  Enable WebRTC transport"
        echo "  --webrtc-signaling-url URL              Set signaling URL (ws://host:port)"
        echo "  --webrtc-stun-server URL                Set STUN server (e.g., stun:stun.l.google.com:19302)"
        echo "  --webrtc-turn-server URL                Set TURN server"
        echo "  --webrtc-turn-username USER            Set TURN username"
        echo "  --webrtc-turn-password PASS            Set TURN password"
        echo "  --webrtc-ice-timeout-ms MS             Set ICE timeout in ms (default: 10000)"
        echo ""
        echo "Video properties will be parsed from filename"
        echo "Expected format: NAME_WIDTHxHEIGHT_FPS.extension"
        echo "Example: $0 /path/to/video_832x480_50.yuv"
        echo "Example: $0 /path/to/video_1920x1080_30.mp4"
        echo ""
        show_common_help "$0" "<video_file> [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]"
        exit 1
    fi

    ADDITIONAL_ARGS=()
    local arg_index=0
    local positional_index=0  # Track actual positional argument index (excluding --webrtc)

    # Parse arguments until we hit '--' or run out of arguments
    while [ "$arg_index" -lt "$arg_count" ]; do
        if [ "${filtered_args[$arg_index]}" = "--" ]; then
            # Found '--' separator, everything after goes to the application
            # Append to ADDITIONAL_ARGS (don't replace, in case --webrtc was already added)
            ADDITIONAL_ARGS+=("${filtered_args[@]:$((arg_index+1))}")
            break
        elif [ "${filtered_args[$arg_index]}" = "--webrtc" ]; then
            # --webrtc flag is handled by run.sh, just pass it through
            ADDITIONAL_ARGS+=("${filtered_args[$arg_index]}")
            arg_index=$((arg_index + 1))
            # Don't increment positional_index, skip this argument
        else
            # This is a script parameter
            case $positional_index in
                0) VIDEO_FILE="${filtered_args[$arg_index]}" ;;
                1) RTP_HOST="${filtered_args[$arg_index]}" ;;
                2) RTP_PORT="${filtered_args[$arg_index]}" ;;
                *) echo "Error: Too many script parameters. Use '--' to separate script parameters from application parameters."
                   echo "Example: $0 $VIDEO_FILE $RTP_HOST $RTP_PORT -- --logging-roi --debug-all"
                   echo "Or use --webrtc flag: $0 $VIDEO_FILE --webrtc -- --webrtc-signaling-url ws://127.0.0.1:8080"
                   exit 1 ;;
            esac
            arg_index=$((arg_index + 1))
            positional_index=$((positional_index + 1))
        fi
    done

    # Check if video file exists
    if [ ! -f "$VIDEO_FILE" ]; then
        echo "Error: Video file '$VIDEO_FILE' not found"
        exit 1
    fi

    echo "Video file: $VIDEO_FILE"
    echo "Communication server: $RTP_HOST:$RTP_PORT"

    # Extract video properties from filename
    echo "Parsing video properties from filename..."

    # Extract filename from full path
    local filename=$(basename "$VIDEO_FILE")

    # Parse video properties using regex
    # Expected format: NAME_WIDTHxHEIGHT_FPS.extension or NAME_WIDTHxHEIGHT.extension
    if [[ "$filename" =~ _([0-9]+)x([0-9]+)_([0-9]+)\. ]]; then
        VIDEO_WIDTH="${BASH_REMATCH[1]}"
        VIDEO_HEIGHT="${BASH_REMATCH[2]}"
        VIDEO_FRAMERATE="${BASH_REMATCH[3]}"
    elif [[ "$filename" =~ _([0-9]+)x([0-9]+)\. ]]; then
        VIDEO_WIDTH="${BASH_REMATCH[1]}"
        VIDEO_HEIGHT="${BASH_REMATCH[2]}"
        # No default framerate - will use config file or application defaults
    else
        echo "Error: Filename '$filename' does not match expected pattern (NAME_WIDTHxHEIGHT_FPS.extension or NAME_WIDTHxHEIGHT.extension)"
        exit 1
    fi

    echo "Parsed video properties from filename:"
    echo "  Resolution: ${VIDEO_WIDTH}x${VIDEO_HEIGHT}"
    echo "  Framerate: ${VIDEO_FRAMERATE} fps"

    # Validate RTP port only if provided
    if [ -n "$RTP_PORT" ] && (! [[ "$RTP_PORT" =~ ^[0-9]+$ ]] || [ "$RTP_PORT" -lt 1 ] || [ "$RTP_PORT" -gt 65535 ]); then
        echo "Error: RTP port must be a positive integer between 1-65535, got: $RTP_PORT"
        exit 1
    fi
}

# Function to parse UDP input parameters
parse_udp_params() {
    local args=("$@")

    # Parse common flags first
    parse_common_flags "${args[@]}"
    local filtered_args=("${FILTERED_ARGS[@]}")
    local arg_count=${#filtered_args[@]}

    # Initialize variables as empty (no defaults)
    UDP_BIND_ADDRESS=""
    UDP_PORT=""
    UDP_WIDTH=""
    UDP_HEIGHT=""
    UDP_FRAMERATE=""

    # Handle help option
    if [ "$arg_count" -eq 1 ] && [[ "${filtered_args[0]}" == "--help" || "${filtered_args[0]}" == "-h" ]]; then
        echo "Usage: $0 [bind_address] [port] [width] [height] [framerate] [-- <additional_args...>]"
        echo ""
        echo "Arguments:"
        echo "  bind_address  UDP bind address (optional, uses config file or application defaults)"
        echo "  port          UDP port to listen on (optional, uses config file or application defaults)"
        echo "  width         Stream width in pixels (optional, uses config file or application defaults)"
        echo "  height        Stream height in pixels (optional, uses config file or application defaults)"
        echo "  framerate     Stream framerate in fps (optional, uses config file or application defaults)"
        echo "  additional_args Additional arguments passed to encoder_cli (optional)"
        echo ""
        echo "Note: encoding, payload_type, clock_rate, and latency_ms are configured in encoder_config.ini"
        echo ""
        echo "Examples:"
        echo "  $0                                    # Use config file or application defaults"
        echo "  $0 0.0.0.0                            # Use different bind address"
        echo "  $0 0.0.0.0 12300                      # Use different port"
        echo "  $0 0.0.0.0 12300 1920 1080 30        # Full configuration"
        echo "  $0 0.0.0.0 12300 1920 1080 30 -- --logging-roi --debug-all  # With additional args"
        echo "  $0 -- --debug-enable-display  # Skip optional params, go straight to app args"
        echo ""
        echo "Common bind addresses:"
        echo "  0.0.0.0  - Listen on all interfaces (default)"
        echo "  127.0.0.1 - Listen on localhost only"
        echo "  <ip>     - Listen on specific interface"
        echo ""
        echo "Common resolutions:"
        echo "  864x480   - Default"
        echo "  1920x1080 - Full HD"
        echo "  1280x720  - HD"
        echo ""
        show_common_help "$0" "[bind_address] [port] [width] [height] [framerate] [-- <additional_args...>]"
        exit 0
    fi

    # Check if we're being called with -- --help (no UDP parameters needed)
    if [ "$arg_count" -eq 2 ] && [ "${filtered_args[0]}" = "--" ] && [ "${filtered_args[1]}" = "--help" ]; then
        echo "Passing --help to encoder_cli application..."
        echo ""
        UDP_BIND_ADDRESS=""
        UDP_PORT=""
        UDP_WIDTH=""
        UDP_HEIGHT=""
        UDP_FRAMERATE=""
        ADDITIONAL_ARGS=("--help")
        HELP_MODE=true
        return
    fi

    # Parse UDP-specific arguments
    ADDITIONAL_ARGS=()
    local arg_index=0

    # Parse arguments until we hit '--' or run out of arguments
    while [ "$arg_index" -lt "$arg_count" ]; do
        if [ "${filtered_args[$arg_index]}" = "--" ]; then
            # Found '--' separator, everything after goes to the application
            ADDITIONAL_ARGS+=("${filtered_args[@]:$((arg_index+1))}")
            break
        else
            # This is a script parameter
            case $arg_index in
                0) UDP_BIND_ADDRESS="${filtered_args[$arg_index]}" ;;
                1) UDP_PORT="${filtered_args[$arg_index]}" ;;
                2) UDP_WIDTH="${filtered_args[$arg_index]}" ;;
                3) UDP_HEIGHT="${filtered_args[$arg_index]}" ;;
                4) UDP_FRAMERATE="${filtered_args[$arg_index]}" ;;
                *) echo "Error: Too many script parameters. Use '--' to separate script parameters from application parameters."
                   echo "Example: $0 $UDP_BIND_ADDRESS $UDP_PORT $UDP_WIDTH $UDP_HEIGHT $UDP_FRAMERATE -- --logging-roi --debug-all"
                   echo ""
                   echo "Note: encoding, payload_type, clock_rate, and latency_ms are configured in encoder_config.ini"
                   exit 1 ;;
            esac
            arg_index=$((arg_index + 1))
        fi
    done

    # Validate UDP port only if provided
    if [ -n "$UDP_PORT" ] && (! [[ "$UDP_PORT" =~ ^[0-9]+$ ]] || [ "$UDP_PORT" -lt 1 ] || [ "$UDP_PORT" -gt 65535 ]); then
        echo "Error: UDP port must be a positive integer between 1-65535, got: $UDP_PORT"
        exit 1
    fi

    # Note: encoding, payload_type, clock_rate, and latency_ms are now read from config file

    if [ -n "$UDP_WIDTH" ] && (! [[ "$UDP_WIDTH" =~ ^[0-9]+$ ]] || [ "$UDP_WIDTH" -lt 1 ]); then
        echo "Error: UDP width must be a positive integer, got: $UDP_WIDTH"
        exit 1
    fi

    if [ -n "$UDP_HEIGHT" ] && (! [[ "$UDP_HEIGHT" =~ ^[0-9]+$ ]] || [ "$UDP_HEIGHT" -lt 1 ]); then
        echo "Error: UDP height must be a positive integer, got: $UDP_HEIGHT"
        exit 1
    fi

    if [ -n "$UDP_FRAMERATE" ] && (! [[ "$UDP_FRAMERATE" =~ ^[0-9]+$ ]] || [ "$UDP_FRAMERATE" -lt 1 ]); then
        echo "Error: UDP framerate must be a positive integer, got: $UDP_FRAMERATE"
        exit 1
    fi

    # Only print configuration for provided parameters
    if [ -n "$UDP_BIND_ADDRESS" ] || [ -n "$UDP_PORT" ] || [ -n "$UDP_WIDTH" ] || [ -n "$UDP_HEIGHT" ] || [ -n "$UDP_FRAMERATE" ]; then
        echo "UDP configuration:"
        [ -n "$UDP_BIND_ADDRESS" ] && echo "  Bind address: $UDP_BIND_ADDRESS" || true
        [ -n "$UDP_PORT" ] && echo "  Port: $UDP_PORT" || true
        [ -n "$UDP_WIDTH" ] && [ -n "$UDP_HEIGHT" ] && echo "  Resolution: ${UDP_WIDTH}x${UDP_HEIGHT}" || true
        [ -n "$UDP_FRAMERATE" ] && echo "  Framerate: ${UDP_FRAMERATE} fps" || true
    fi
}

# Function to parse UDP + RTP parameters
parse_udp_rtp_params() {
    local args=("$@")

    # Parse common flags first
    parse_common_flags "${args[@]}"
    local filtered_args=("${FILTERED_ARGS[@]}")
    local arg_count=${#filtered_args[@]}

    # Initialize variables as empty (no defaults)
    UDP_BIND_ADDRESS=""
    UDP_PORT=""
    UDP_WIDTH=""
    UDP_HEIGHT=""
    UDP_FRAMERATE=""
    RTP_HOST=""
    RTP_PORT=""

    # Handle help option
    if [ "$arg_count" -eq 1 ] && [[ "${filtered_args[0]}" == "--help" || "${filtered_args[0]}" == "-h" ]]; then
        echo "Usage: $0 [bind_address] [port] [width] [height] [framerate] [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]"
        echo ""
        echo "Arguments:"
        echo "  bind_address  UDP bind address (optional, uses config file or application defaults)"
        echo "  port          UDP port to listen on (optional, uses config file or application defaults)"
        echo "  width         Stream width in pixels (optional, uses config file or application defaults)"
        echo "  height        Stream height in pixels (optional, uses config file or application defaults)"
        echo "  framerate     Stream framerate in fps (optional, uses config file or application defaults)"
        echo "  rtp_host      Communication server host address (optional, uses config file)"
        echo "  rtp_port      Communication server port (optional, uses config file)"
        echo "  --webrtc      Enable WebRTC transport mode (optional, overrides config file)"
        echo "  additional_args Additional arguments passed to encoder_cli (optional)"
        echo ""
        echo "Note: encoding, payload_type, clock_rate, and latency_ms are configured in encoder_config.ini"
        echo ""
        echo "Examples:"
        echo "  $0                                    # Use config file or application defaults"
        echo "  $0 0.0.0.0                            # Use different bind address"
        echo "  $0 0.0.0.0 12300                      # Use different UDP port"
        echo "  $0 0.0.0.0 12300 1920 1080 30        # Full UDP configuration"
        echo "  $0 0.0.0.0 12300 1920 1080 30 127.0.0.1  # With RTP host"
        echo "  $0 0.0.0.0 12300 1920 1080 30 127.0.0.1 5000  # Full configuration"
        echo "  $0 0.0.0.0 12300 1920 1080 30 127.0.0.1 5000 -- --logging-roi --debug-all  # With additional args"
        echo "  $0 0.0.0.0 12300 1920 1080 30 127.0.0.1 5000 --webrtc -- --webrtc-signaling-url ws://127.0.0.1:8080  # With WebRTC"
        echo "  $0 -- --debug-enable-display  # Skip optional params, go straight to app args"
        echo ""
        echo "Common bind addresses:"
        echo "  0.0.0.0  - Listen on all interfaces (default)"
        echo "  127.0.0.1 - Listen on localhost only"
        echo "  <ip>     - Listen on specific interface"
        echo ""
        echo "Common resolutions:"
        echo "  864x480   - Default"
        echo "  1920x1080 - Full HD"
        echo "  1280x720  - HD"
        echo ""
        echo "Communication server examples:"
        echo "  127.0.0.1:5000  - Localhost"
        echo "  127.0.0.1:5000  - Localhost"
        echo "  0.0.0.0:5000  - All interfaces (server mode)"
        echo ""
        echo "Transport Types:"
        echo "  The encoder supports two transport types:"
        echo "    udp    - UDP transport with binary protocol (default)"
        echo "    webrtc - WebRTC transport with signaling server"
        echo ""
        echo "  For WebRTC:"
        echo "    - Use --webrtc flag or pass --communication-transport-type webrtc after '--'"
        echo "    - Configure via command line: --webrtc-signaling-url, --webrtc-stun-server, etc."
        echo "    - Or configure in encoder_config.ini [Communication] and [WebRTC] sections"
        echo "    - The rtp_host and rtp_port parameters are still used for handshake"
        echo ""
        show_common_help "$0" "[bind_address] [port] [width] [height] [framerate] [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]"
        exit 0
    fi

    # Check if we're being called with -- --help (no UDP/RTP parameters needed)
    if [ "$arg_count" -eq 2 ] && [ "${filtered_args[0]}" = "--" ] && [ "${filtered_args[1]}" = "--help" ]; then
        echo "Passing --help to encoder_cli application..."
        echo ""
        UDP_BIND_ADDRESS=""
        UDP_PORT=""
        UDP_WIDTH=""
        UDP_HEIGHT=""
        UDP_FRAMERATE=""
        RTP_HOST=""
        RTP_PORT=""
        ADDITIONAL_ARGS=("--help")
        HELP_MODE=true
        return
    fi

    # Parse UDP and RTP-specific arguments
    ADDITIONAL_ARGS=()
    local arg_index=0
    local positional_index=0  # Track actual positional argument index (excluding --webrtc)

    # Parse arguments until we hit '--' or run out of arguments
    while [ "$arg_index" -lt "$arg_count" ]; do
        if [ "${filtered_args[$arg_index]}" = "--" ]; then
            # Found '--' separator, everything after goes to the application
            # Append to ADDITIONAL_ARGS (don't replace, in case --webrtc was already added)
            ADDITIONAL_ARGS+=("${filtered_args[@]:$((arg_index+1))}")
            break
        elif [ "${filtered_args[$arg_index]}" = "--webrtc" ]; then
            # --webrtc flag is handled by run script, just pass it through
            ADDITIONAL_ARGS+=("${filtered_args[$arg_index]}")
            arg_index=$((arg_index + 1))
            # Don't increment positional_index, skip this argument
        else
            # This is a script parameter
            case $positional_index in
                0) UDP_BIND_ADDRESS="${filtered_args[$arg_index]}" ;;
                1) UDP_PORT="${filtered_args[$arg_index]}" ;;
                2) UDP_WIDTH="${filtered_args[$arg_index]}" ;;
                3) UDP_HEIGHT="${filtered_args[$arg_index]}" ;;
                4) UDP_FRAMERATE="${filtered_args[$arg_index]}" ;;
                5) RTP_HOST="${filtered_args[$arg_index]}" ;;
                6) RTP_PORT="${filtered_args[$arg_index]}" ;;
                *) echo "Error: Too many script parameters. Use '--' to separate script parameters from application parameters."
                   echo "Example: $0 $UDP_BIND_ADDRESS $UDP_PORT $UDP_WIDTH $UDP_HEIGHT $UDP_FRAMERATE $RTP_HOST $RTP_PORT -- --logging-roi --debug-all"
                   echo "Or use --webrtc flag: $0 $UDP_BIND_ADDRESS $UDP_PORT $UDP_WIDTH $UDP_HEIGHT $UDP_FRAMERATE $RTP_HOST $RTP_PORT --webrtc -- --webrtc-signaling-url ws://127.0.0.1:8080"
                   echo ""
                   echo "Note: encoding, payload_type, clock_rate, and latency_ms are configured in encoder_config.ini"
                   exit 1 ;;
            esac
            arg_index=$((arg_index + 1))
            positional_index=$((positional_index + 1))
        fi
    done

    # Validate UDP port only if provided
    if [ -n "$UDP_PORT" ] && (! [[ "$UDP_PORT" =~ ^[0-9]+$ ]] || [ "$UDP_PORT" -lt 1 ] || [ "$UDP_PORT" -gt 65535 ]); then
        echo "Error: UDP port must be a positive integer between 1-65535, got: $UDP_PORT"
        exit 1
    fi

    # Note: encoding, payload_type, clock_rate, and latency_ms are now read from config file

    if [ -n "$UDP_WIDTH" ] && (! [[ "$UDP_WIDTH" =~ ^[0-9]+$ ]] || [ "$UDP_WIDTH" -lt 1 ]); then
        echo "Error: UDP width must be a positive integer, got: $UDP_WIDTH"
        exit 1
    fi

    if [ -n "$UDP_HEIGHT" ] && (! [[ "$UDP_HEIGHT" =~ ^[0-9]+$ ]] || [ "$UDP_HEIGHT" -lt 1 ]); then
        echo "Error: UDP height must be a positive integer, got: $UDP_HEIGHT"
        exit 1
    fi

    if [ -n "$UDP_FRAMERATE" ] && (! [[ "$UDP_FRAMERATE" =~ ^[0-9]+$ ]] || [ "$UDP_FRAMERATE" -lt 1 ]); then
        echo "Error: UDP framerate must be a positive integer, got: $UDP_FRAMERATE"
        exit 1
    fi

    if [ -n "$RTP_PORT" ] && (! [[ "$RTP_PORT" =~ ^[0-9]+$ ]] || [ "$RTP_PORT" -lt 1 ] || [ "$RTP_PORT" -gt 65535 ]); then
        echo "Error: RTP port must be a positive integer between 1-65535, got: $RTP_PORT"
        exit 1
    fi

    # Only print configuration for provided parameters
    if [ -n "$UDP_BIND_ADDRESS" ] || [ -n "$UDP_PORT" ] || [ -n "$UDP_WIDTH" ] || [ -n "$UDP_HEIGHT" ] || [ -n "$UDP_FRAMERATE" ]; then
        echo "UDP configuration:"
        [ -n "$UDP_BIND_ADDRESS" ] && echo "  Bind address: $UDP_BIND_ADDRESS" || true
        [ -n "$UDP_PORT" ] && echo "  Port: $UDP_PORT" || true
        [ -n "$UDP_WIDTH" ] && [ -n "$UDP_HEIGHT" ] && echo "  Resolution: ${UDP_WIDTH}x${UDP_HEIGHT}" || true
        [ -n "$UDP_FRAMERATE" ] && echo "  Framerate: ${UDP_FRAMERATE} fps" || true
    fi
    if [ -n "$RTP_HOST" ] || [ -n "$RTP_PORT" ]; then
        echo "RTP configuration:"
        [ -n "$RTP_HOST" ] && echo "  Host: $RTP_HOST" || true
        [ -n "$RTP_PORT" ] && echo "  Port: $RTP_PORT" || true
    fi
}

# Function to parse production parameters
parse_prod_params() {
    local args=("$@")

    # Parse common flags first
    parse_common_flags "${args[@]}"
    local filtered_args=("${FILTERED_ARGS[@]}")
    local arg_count=${#filtered_args[@]}

    # Initialize variables as empty (no defaults - use config file)
    PROD_WIDTH=""
    PROD_HEIGHT=""

    # Handle help option
    if [ "$arg_count" -eq 1 ] && [[ "${filtered_args[0]}" == "--help" || "${filtered_args[0]}" == "-h" ]]; then
        echo "Usage: $0 [width] [height]"
        echo ""
        echo "Arguments:"
        echo "  width         Stream width in pixels (optional, uses config file or application defaults)"
        echo "  height        Stream height in pixels (optional, uses config file or application defaults)"
        echo ""
        echo "Note: All other configuration comes from encoder_config_prod.ini"
        echo ""
        echo "Examples:"
        echo "  $0                                    # Use config file or application defaults"
        echo "  $0 640 480                            # Override resolution (needed for a new YOLO model)"
        echo "  $0 1920 1080                          # Use Full HD resolution"
        echo ""
        echo "Common resolutions:"
        echo "  640x480   - VGA"
        echo "  1920x1080 - Full HD"
        echo "  1280x720  - HD"
        echo ""
        show_common_help "$0" "[width] [height]"
        exit 0
    fi

    # Check if we're being called with -- --help (no parameters needed)
    if [ "$arg_count" -eq 2 ] && [ "${filtered_args[0]}" = "--" ] && [ "${filtered_args[1]}" = "--help" ]; then
        echo "Passing --help to encoder_cli application..."
        echo ""
        PROD_WIDTH=""
        PROD_HEIGHT=""
        ADDITIONAL_ARGS=("--help")
        HELP_MODE=true
        return
    fi

    # Initialize ADDITIONAL_ARGS as empty (no additional args allowed)
    ADDITIONAL_ARGS=()

    # Parse only width and height parameters
    # Everything else must come from encoder_config_prod.ini
    if [ "$arg_count" -gt 0 ]; then
        PROD_WIDTH="${filtered_args[0]}"
    fi

    if [ "$arg_count" -gt 1 ]; then
        PROD_HEIGHT="${filtered_args[1]}"
    fi

    # Validate that we don't have too many arguments
    if [ "$arg_count" -gt 2 ]; then
        echo "Error: Too many arguments. This script only accepts width and height."
        echo "All other configuration must come from encoder_config_prod.ini"
        echo ""
        echo "Usage: $0 [width] [height]"
        echo "Example: $0 640 480"
        exit 1
    fi

    # Validate numeric parameters only if provided
    if [ -n "$PROD_WIDTH" ] && (! [[ "$PROD_WIDTH" =~ ^[0-9]+$ ]] || [ "$PROD_WIDTH" -lt 1 ]); then
        echo "Error: Width must be a positive integer, got: $PROD_WIDTH"
        exit 1
    fi

    if [ -n "$PROD_HEIGHT" ] && (! [[ "$PROD_HEIGHT" =~ ^[0-9]+$ ]] || [ "$PROD_HEIGHT" -lt 1 ]); then
        echo "Error: Height must be a positive integer, got: $PROD_HEIGHT"
        exit 1
    fi

    # Only print configuration if provided
    if [ -n "$PROD_WIDTH" ] || [ -n "$PROD_HEIGHT" ]; then
        echo "Production configuration:"
        [ -n "$PROD_WIDTH" ] && [ -n "$PROD_HEIGHT" ] && echo "  Resolution: ${PROD_WIDTH}x${PROD_HEIGHT}" || true
        [ -n "$PROD_WIDTH" ] && [ -z "$PROD_HEIGHT" ] && echo "  Width: $PROD_WIDTH (height from config)" || true
        [ -z "$PROD_WIDTH" ] && [ -n "$PROD_HEIGHT" ] && echo "  Height: $PROD_HEIGHT (width from config)" || true
    fi
}
