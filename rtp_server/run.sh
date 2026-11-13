#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# rtp_server run script with transport mode selection.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/scripts/common_rtp_server_setup.sh"

transport=""
rtp_output_host=""
rtp_output_port=""
HELP_MODE=false

show_help() {
    cat << 'EOF'
Usage:
  ./run.sh <transport> [<host:port>] [-- <rtp_server_args...>]
  ./run.sh -- --help

Summary:
  Builds and runs the rtp_server application with the proper GStreamer
  environment configured. Choose an input transport mode for receiving
  client streams, and optionally configure RTP output forwarding.

Input transport modes (how clients send video to the server):
  rtp     Receive streams via RTP input transport with binary handshake protocol.
  webrtc  Receive streams via WebRTC with embedded signaling server.

Optional arguments:
  host:port   RTP output forwarding destination as host or host:port.
              Overrides [RTPOutput] host/port in config file.

Passing application arguments:
  Use '--' to pass any arguments directly to the rtp_server binary.
  Example: ./run.sh rtp -- --server-handshake-port 5004

Examples:
  ./run.sh rtp                                    # RTP input, config defaults
  ./run.sh rtp 192.168.1.100:5006                 # RTP input, forward output to host:port
  ./run.sh rtp 192.168.1.100                      # RTP input, forward output to host
  ./run.sh webrtc                                 # WebRTC input, config defaults
  ./run.sh webrtc -- --webrtc-signaling-port 8080  # WebRTC input, custom signaling port
  ./run.sh rtp -- --server-handshake-port 5004 --debug-draw-bitrate  # RTP input, custom handshake port
  ./run.sh rtp -- --debug-save-frames              # Save final reconstructed debug PNG frames
  ./run.sh -- --help                               # Show rtp_server binary help
  ./run.sh rtp -- --help                           # Show rtp_server binary help

WebRTC Options (pass after '--'):
  --webrtc-signaling-port PORT     Set signaling port (default: 8080)
  --webrtc-signaling-ssl           Enable SSL for signaling (WSS)
  --webrtc-stun-server URL         Set STUN server
  --webrtc-turn-server URL         Set TURN server
  --webrtc-turn-username USER      Set TURN username
  --webrtc-turn-password PASS      Set TURN password
  --webrtc-ice-timeout-ms MS       Set ICE timeout in ms (default: 10000)

Debug Options (pass after '--'):
  --debug-draw-bitrate             Enable bitrate display overlay
  --debug-draw-fps                 Enable FPS display overlay
  --debug-save-frames              Save final reconstructed frames as PNG files

TET Options (pass after '--'):
  --tet-enable=true|false          Enable or disable TET post-processing
  --tet-tile-file PATH             Path to TET tile file (.tet)
  --tet-qp VALUE                   Fallback QP value (0-51)

Configuration:
  Settings are read from rtp_server_config.ini by default.
  Override with: -- --config <path>

  Common group:
  | Config key | Purpose                         |
  |------------|---------------------------------|
  | width      | Video width for YOLO setup       |
  | height     | Video height for YOLO setup      |
  | (CLI)      | Use --common-width/--common-height to override |

  Output forwarding:
  | CLI argument   | Config fallback              |
  |----------------|------------------------------|
  | host:port host | [RTPOutput] host                   |
  | host:port port | [RTPOutput] port (must be 1-65535) |

  Input / server:
  | CLI argument   | Config fallback              |
  |----------------|------------------------------|
  | (transport)    | [Server] transport_type      |
  | -- --server-handshake-port PORT | [Server] handshake_port |

Output:
  All output is saved to rtp_server_output.txt
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

parse_host_port() {
    local value="$1"
    local host_candidate="${value%:*}"
    local port_candidate="${value##*:}"

    if [ "$host_candidate" = "$value" ]; then
        # No colon found — treat entire value as host
        rtp_output_host="$value"
        rtp_output_port=""
        return 0
    fi

    if [[ "$port_candidate" =~ ^[0-9]+$ ]]; then
        if [ "$port_candidate" -lt 1 ] || [ "$port_candidate" -gt 65535 ]; then
            exit_with_help "RTP output port must be between 1-65535, got: $port_candidate"
        fi
        rtp_output_host="$host_candidate"
        rtp_output_port="$port_candidate"
        return 0
    fi

    exit_with_help "Argument must be a host or host:port, got: $value"
}

normalize_rtp_output_port() {
    if [ -n "$rtp_output_port" ]; then
        if ! [[ "$rtp_output_port" =~ ^[0-9]+$ ]]; then
            exit_with_help "RTP output port must be a number, got: $rtp_output_port"
        fi
        if [ "$rtp_output_port" -lt 1 ] || [ "$rtp_output_port" -gt 65535 ]; then
            exit_with_help "RTP output port must be between 1-65535, got: $rtp_output_port"
        fi
    fi
}

# --- Argument parsing ---

if [ "$#" -ge 2 ] && [ "$1" = "--" ] && { [ "$2" = "--help" ] || [ "$2" = "-h" ]; }; then
    setup_environment
    execute_rtp_server "$2"
    exit 0
fi

if [ "$#" -eq 0 ] || [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    show_help
    exit 0
fi

transport="$1"
shift

case "$transport" in
    rtp|webrtc) ;;
    *) exit_with_help "Invalid transport: $transport (must be 'rtp' or 'webrtc')" ;;
esac

# Split remaining args into script_args (before --) and app_args (after --)
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

# Detect help mode (-- --help)
if [ "${#app_args[@]}" -eq 1 ] && { [ "${app_args[0]}" = "--help" ] || [ "${app_args[0]}" = "-h" ]; }; then
    HELP_MODE=true
fi

# Parse optional host:port from script_args
if [ "${#script_args[@]}" -gt 1 ]; then
    exit_with_help "Too many script arguments. Use '--' to pass application args."
fi

if [ "${#script_args[@]}" -eq 1 ]; then
    parse_host_port "${script_args[0]}"
fi

normalize_rtp_output_port

# Print configuration summary
echo "Transport: $transport"
if [ -n "$rtp_output_host" ] || [ -n "$rtp_output_port" ]; then
    echo "RTP output forwarding:"
    [ -n "$rtp_output_host" ] && echo "  Host: $rtp_output_host"
    [ -n "$rtp_output_port" ] && echo "  Port: $rtp_output_port"
fi

# --- Config detection ---

RTP_SERVER_CONFIG_FILE="$(detect_config_file "${app_args[@]}")"

# --- Read Common width/height from config ---

read_common_config_value() {
    local key="$1"
    local file="$2"
    awk -F'=' -v target="$key" '
        BEGIN { in_common=0 }
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*;/ { next }
        /^[[:space:]]*\[Common\][[:space:]]*$/ { in_common=1; next }
        /^[[:space:]]*\[/ { in_common=0 }
        in_common && $1 ~ "^[[:space:]]*" target "[[:space:]]*$" {
            value=$2
            sub(/^[[:space:]]+/, "", value)
            sub(/[[:space:]]+$/, "", value)
            print value
            exit
        }
    ' "$file"
}

COMMON_WIDTH=""
COMMON_HEIGHT=""
if [ -n "$RTP_SERVER_CONFIG_FILE" ] && [ -f "$RTP_SERVER_CONFIG_FILE" ]; then
    COMMON_WIDTH="$(read_common_config_value "width" "$RTP_SERVER_CONFIG_FILE")"
    COMMON_HEIGHT="$(read_common_config_value "height" "$RTP_SERVER_CONFIG_FILE")"
fi

# Allow CLI overrides for width/height (passed after --).
APP_WIDTH=""
APP_HEIGHT=""
for ((i=0; i<${#app_args[@]}; i++)); do
    if [ "${app_args[$i]}" = "--common-width" ] && [ $((i+1)) -lt ${#app_args[@]} ]; then
        APP_WIDTH="${app_args[$((i+1))]}"
    fi
    if [ "${app_args[$i]}" = "--common-height" ] && [ $((i+1)) -lt ${#app_args[@]} ]; then
        APP_HEIGHT="${app_args[$((i+1))]}"
    fi
done

if [ -n "$APP_WIDTH" ]; then
    COMMON_WIDTH="$APP_WIDTH"
fi
if [ -n "$APP_HEIGHT" ]; then
    COMMON_HEIGHT="$APP_HEIGHT"
fi

# --- Setup ---

setup_environment

check_and_setup_yolo "$COMMON_WIDTH" "$COMMON_HEIGHT"
setup_model_config "$COMMON_WIDTH" "$COMMON_HEIGHT"
prepare_execution

# --- Build rtp_server arguments ---

if [ "$HELP_MODE" = true ]; then
    execute_rtp_server "${app_args[@]}"
    exit 0
fi

rtp_server_args=(--server-transport-type "$transport")

[ -n "$rtp_output_host" ] && rtp_server_args+=(--rtp-output-host "$rtp_output_host")
[ -n "$rtp_output_port" ] && rtp_server_args+=(--rtp-output-port "$rtp_output_port")

rtp_server_args+=("${app_args[@]}")

# --- Execute ---

execute_rtp_server "${rtp_server_args[@]}"
