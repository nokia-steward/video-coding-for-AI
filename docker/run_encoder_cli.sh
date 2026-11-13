#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Encoder CLI Docker wrapper script for consolidated encoder_cli/run.sh.

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Configuration
IMAGE_NAME="vcai-encoder-cli:latest"
CONTAINER_NAME="vcai-encoder-cli"
VCAI_HOME="${HOME}/.VCAI"

print_usage() {
    echo "Usage: $0 [OPTIONS] <input_mode> <output_mode> [script_args] [-- <encoder_cli_args...>]"
    echo "       $0 [OPTIONS] -- --help"
    echo ""
    echo "Docker wrapper options:"
    echo "  --build                    Build the Docker image before running"
    echo "  --force-rebuild            Force rebuild of application image only"
    echo "  --force-rebuild-base       Force rebuild of base image (also rebuilds app)"
    echo "  --background, -d           Run container in background"
    echo "  --env VAR=value, -e        Pass environment variable to container (repeatable)"
    echo "  --help, -h                 Show this help message"
    echo ""
    echo "Forwarded arguments (to encoder_cli/run.sh):"
    echo "  <input_mode>               file | camera | udp"
    echo "  <output_mode>              rtp | webrtc"
    echo "  [script_args]              Optional wrapper script args for encoder_cli/run.sh"
    echo "  -- <encoder_cli_args...>   Pass-through args for encoder_cli binary"
    echo ""
    echo "Examples:"
    echo "  $0 udp rtp"
    echo "  $0 file rtp /path/to/video_1920x1080_30.yuv"
    echo "  $0 camera webrtc /dev/video0 -o ws://127.0.0.1:8080"
    echo "  $0 udp webrtc -- --debug-all --communication-timeout-ms 2000"
    echo "  $0 -- --help"
    echo "  $0 --background udp rtp"
}

BUILD_IMAGE=false
FORCE_REBUILD=false
FORCE_REBUILD_BASE=false
BACKGROUND=false
ENV_VARS=()
ENCODER_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build)
            BUILD_IMAGE=true
            shift
            ;;
        --force-rebuild)
            FORCE_REBUILD=true
            BUILD_IMAGE=true
            shift
            ;;
        --force-rebuild-base)
            FORCE_REBUILD_BASE=true
            FORCE_REBUILD=true
            BUILD_IMAGE=true
            shift
            ;;
        --background|-d)
            BACKGROUND=true
            shift
            ;;
        --env|-e)
            if [ -z "$2" ]; then
                echo -e "${RED}Error: --env/-e requires VAR=value${NC}"
                exit 1
            fi
            ENV_VARS+=("$2")
            shift 2
            ;;
        --help|-h)
            print_usage
            exit 0
            ;;
        --)
            ENCODER_ARGS+=("$1")
            shift
            ENCODER_ARGS+=("$@")
            break
            ;;
        *)
            ENCODER_ARGS+=("$1")
            shift
            ;;
    esac
done

if [ "${#ENCODER_ARGS[@]}" -eq 0 ]; then
    echo -e "${RED}Error: missing encoder run arguments.${NC}"
    echo ""
    print_usage
    exit 1
fi

HELP_PASSTHROUGH=false
if [ "${#ENCODER_ARGS[@]}" -ge 2 ] && [ "${ENCODER_ARGS[0]}" = "--" ] && { [ "${ENCODER_ARGS[1]}" = "--help" ] || [ "${ENCODER_ARGS[1]}" = "-h" ]; }; then
    HELP_PASSTHROUGH=true
fi

INPUT_MODE=""
if [ "$HELP_PASSTHROUGH" = false ]; then
    if [ "${#ENCODER_ARGS[@]}" -lt 2 ]; then
        echo -e "${RED}Error: expected <input_mode> <output_mode> for encoder_cli/run.sh.${NC}"
        echo ""
        print_usage
        exit 1
    fi

    INPUT_MODE="${ENCODER_ARGS[0]}"
    case "${INPUT_MODE}" in
        file|camera|udp) ;;
        *)
            echo -e "${RED}Error: invalid input mode '${INPUT_MODE}' (expected file|camera|udp).${NC}"
            exit 1
            ;;
    esac
fi

HOST_VIDEO_FILE=""
if [ "$HELP_PASSTHROUGH" = false ] && [ "$INPUT_MODE" = "file" ]; then
    SCRIPT_ARGS_AFTER_MODES=("${ENCODER_ARGS[@]:2}")
    if [ "${#SCRIPT_ARGS_AFTER_MODES[@]}" -gt 0 ] && [ "${SCRIPT_ARGS_AFTER_MODES[0]}" != "-o" ] && [ "${SCRIPT_ARGS_AFTER_MODES[0]}" != "--" ]; then
        INPUT_VIDEO_CANDIDATE="${SCRIPT_ARGS_AFTER_MODES[0]}"
        if [ -f "${INPUT_VIDEO_CANDIDATE}" ]; then
            HOST_VIDEO_FILE="$(readlink -f "${INPUT_VIDEO_CANDIDATE}" 2>/dev/null || realpath "${INPUT_VIDEO_CANDIDATE}" 2>/dev/null || echo "${INPUT_VIDEO_CANDIDATE}")"
        fi
    fi
fi

# Initialize directories if needed
if [ ! -d "${VCAI_HOME}/encoder_cli" ]; then
    echo -e "${YELLOW}Initializing directories...${NC}"
    "${SCRIPT_DIR}/init_directories.sh"
fi

# Check and build base image if needed
BASE_IMAGE="vcai-base:latest"
if [ "$FORCE_REBUILD_BASE" = true ] || ! docker image inspect "${BASE_IMAGE}" >/dev/null 2>&1; then
    if [ "$FORCE_REBUILD_BASE" = true ]; then
        echo -e "${YELLOW}Force rebuilding base image: ${BASE_IMAGE}${NC}"
    else
        echo -e "${YELLOW}Base image ${BASE_IMAGE} not found${NC}"
    fi
    echo -e "${BLUE}Building base image: ${BASE_IMAGE}${NC}"
    cd "${PROJECT_ROOT}"
    if [ "$FORCE_REBUILD_BASE" = true ]; then
        docker build --no-cache -f "${SCRIPT_DIR}/Dockerfile_base" -t "${BASE_IMAGE}" .
    else
        docker build -f "${SCRIPT_DIR}/Dockerfile_base" -t "${BASE_IMAGE}" .
    fi
    echo -e "${GREEN}Base image built successfully${NC}"
fi

# Build image if requested or if it doesn't exist
if [ "$BUILD_IMAGE" = true ] || ! docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1; then
    if [ "$FORCE_REBUILD" = true ]; then
        echo -e "${YELLOW}Force rebuilding Docker image: ${IMAGE_NAME}${NC}"
    fi
    echo -e "${BLUE}Building Docker image: ${IMAGE_NAME}${NC}"
    cd "${PROJECT_ROOT}"
    if [ "$FORCE_REBUILD" = true ]; then
        docker build --no-cache -f "${SCRIPT_DIR}/Dockerfile_encoder_cli" -t "${IMAGE_NAME}" .
    else
        docker build -f "${SCRIPT_DIR}/Dockerfile_encoder_cli" -t "${IMAGE_NAME}" .
    fi
    echo -e "${GREEN}Image built successfully${NC}"
fi

# Set up environment variables
export DISPLAY="${DISPLAY:-:0}"
export USER_ID="${USER_ID:-$(id -u)}"
export GROUP_ID="${GROUP_ID:-$(id -g)}"
export USERNAME="${USERNAME:-${USER}}"

# Get group IDs for render and video groups (needed for DRM device access)
RENDER_GID=$(getent group render 2>/dev/null | cut -d: -f3 || echo "")
VIDEO_GID=$(getent group video 2>/dev/null | cut -d: -f3 || echo "")

XAUTH_MOUNT_ARGS=()
if [ -n "${XAUTHORITY}" ] && [ -f "${XAUTHORITY}" ]; then
    XAUTH_MOUNT_ARGS=(-v "${XAUTHORITY}:${XAUTHORITY}:ro" -e "XAUTHORITY=${XAUTHORITY}")
elif [ -f "${HOME}/.Xauthority" ]; then
    export XAUTHORITY="${HOME}/.Xauthority"
    XAUTH_MOUNT_ARGS=(-v "${HOME}/.Xauthority:${HOME}/.Xauthority:ro" -e "XAUTHORITY=${XAUTHORITY}")
fi

# Set XDG_RUNTIME_DIR for applications that need it (e.g., GStreamer, Wayland)
if [ -z "${XDG_RUNTIME_DIR}" ]; then
    export XDG_RUNTIME_DIR="/tmp/runtime-${USER_ID}"
fi
mkdir -p "${XDG_RUNTIME_DIR}" >/dev/null 2>&1 || true

# Logging defaults for concise runtime output
export GST_DEBUG="${GST_DEBUG:-encoder-cli:4,*:1}"
export GST_DEBUG_NO_COLOR=1
export GST_DEBUG_DUMP_DOT_DIR="/tmp/gst-debug-dump"

# Check if container is already running
if docker ps -q -f name="${CONTAINER_NAME}" | grep -q .; then
    echo -e "${YELLOW}Container ${CONTAINER_NAME} is already running${NC}"
    echo -e "${YELLOW}Stopping existing container...${NC}"
    docker stop "${CONTAINER_NAME}" >/dev/null 2>&1 || true
    docker rm "${CONTAINER_NAME}" >/dev/null 2>&1 || true
fi

DOCKER_RUN_ARGS=(
    --rm
    --name "${CONTAINER_NAME}"
    --network host
    --privileged
    --runtime=nvidia
    --gpus all
    -e DISPLAY="${DISPLAY}"
    -e NVIDIA_VISIBLE_DEVICES=all
    -e NVIDIA_DRIVER_CAPABILITIES=compute,utility,video
    -e VCAI_DATA_DIR=/opt/vcai/data
    -e WORKING_DIR=/opt/vcai
    -e USER_ID="${USER_ID}"
    -e GROUP_ID="${GROUP_ID}"
    -e USERNAME="${USERNAME}"
    -e GST_DEBUG="${GST_DEBUG}"
    -e GST_DEBUG_NO_COLOR="${GST_DEBUG_NO_COLOR}"
    -e GST_DEBUG_DUMP_DOT_DIR="${GST_DEBUG_DUMP_DOT_DIR}"
    -e XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR}"
    -v "${VCAI_HOME}/encoder_cli:/opt/vcai/data/encoder_cli"
    -v "${PROJECT_ROOT}/yolo:/opt/vcai/yolo"
    -v /tmp/.X11-unix:/tmp/.X11-unix
    -v "${XDG_RUNTIME_DIR}:${XDG_RUNTIME_DIR}"
    --device /dev/dri
)

if [ "${#XAUTH_MOUNT_ARGS[@]}" -gt 0 ]; then
    DOCKER_RUN_ARGS+=("${XAUTH_MOUNT_ARGS[@]}")
fi

if [ -n "${RENDER_GID}" ]; then
    DOCKER_RUN_ARGS+=(-e "RENDER_GID=${RENDER_GID}")
fi
if [ -n "${VIDEO_GID}" ]; then
    DOCKER_RUN_ARGS+=(-e "VIDEO_GID=${VIDEO_GID}")
fi

for env_var in "${ENV_VARS[@]}"; do
    DOCKER_RUN_ARGS+=(-e "$env_var")
done

# Add config file mounts if they exist
if [ -f "${PROJECT_ROOT}/encoder_cli/encoder_config.ini" ]; then
    DOCKER_RUN_ARGS+=(-v "${PROJECT_ROOT}/encoder_cli/encoder_config.ini:/opt/vcai/encoder_cli/encoder_config.ini:ro")
fi
if [ -f "${PROJECT_ROOT}/encoder_cli/encoder_config_prod.ini" ]; then
    DOCKER_RUN_ARGS+=(-v "${PROJECT_ROOT}/encoder_cli/encoder_config_prod.ini:/opt/vcai/encoder_cli/encoder_config_prod.ini:ro")
fi
if [ -f "${PROJECT_ROOT}/encoder_cli/static_roi_data.json" ]; then
    DOCKER_RUN_ARGS+=(-v "${PROJECT_ROOT}/encoder_cli/static_roi_data.json:/opt/vcai/encoder_cli/static_roi_data.json:ro")
fi

if [ -n "${HOST_VIDEO_FILE}" ]; then
    DOCKER_RUN_ARGS+=(-v "${HOST_VIDEO_FILE}:${HOST_VIDEO_FILE}:ro")
fi

# Add video device mounts for camera input mode
if [ "$INPUT_MODE" = "camera" ] && ls /dev/video* >/dev/null 2>&1; then
    for video_dev in /dev/video*; do
        DOCKER_RUN_ARGS+=(--device "$video_dev")
    done
fi

LOG_FILE="encoder_output.txt"
ENCODER_CMD=("/opt/vcai/encoder_cli/run.sh" "${ENCODER_ARGS[@]}")

# Check X11 permissions - warn if xhost might be needed
if ! xhost | grep -q "LOCAL:"; then
    echo -e "${YELLOW}Warning: X11 access might be restricted. If display doesn't work, run:${NC}"
    echo -e "${YELLOW}  xhost +local:docker${NC}"
    echo ""
fi

echo -e "${BLUE}Starting encoder_cli container...${NC}"
echo -e "${BLUE}  Image: ${IMAGE_NAME}${NC}"
echo -e "${BLUE}  Container: ${CONTAINER_NAME}${NC}"
echo -e "${BLUE}  Entrypoint: /opt/vcai/encoder_cli/run.sh${NC}"
echo -e "${BLUE}  Network: host${NC}"
echo -e "${GREEN}  Log file: ${VCAI_HOME}/encoder_cli/${LOG_FILE}${NC}"
if [ -n "${INPUT_MODE}" ]; then
    echo -e "${BLUE}  Input mode: ${INPUT_MODE}${NC}"
fi
if [ -n "${HOST_VIDEO_FILE}" ]; then
    echo -e "${BLUE}  Mounted input file: ${HOST_VIDEO_FILE}${NC}"
fi
echo -e "${BLUE}  Forwarded args: ${ENCODER_ARGS[*]}${NC}"
echo ""

cd "${PROJECT_ROOT}"

cleanup() {
    echo ""
    echo -e "${YELLOW}Shutting down encoder_cli...${NC}"
    docker stop "${CONTAINER_NAME}" >/dev/null 2>&1 || true
    docker rm "${CONTAINER_NAME}" >/dev/null 2>&1 || true
    echo -e "${GREEN}Cleanup complete${NC}"
    exit 0
}

trap cleanup SIGINT SIGTERM

if [ "$BACKGROUND" = true ]; then
    DOCKER_RUN_ARGS+=(-d)
    if docker run "${DOCKER_RUN_ARGS[@]}" "${IMAGE_NAME}" "${ENCODER_CMD[@]}"; then
        echo ""
        echo -e "${GREEN}Encoder CLI started in background${NC}"
        echo -e "${BLUE}To view logs: docker logs -f ${CONTAINER_NAME}${NC}"
        echo -e "${BLUE}To stop: docker stop ${CONTAINER_NAME}${NC}"
        echo -e "${GREEN}Log file location: ${VCAI_HOME}/encoder_cli/${LOG_FILE}${NC}"
    else
        echo -e "${RED}Failed to start encoder_cli${NC}"
        exit 1
    fi
else
    DOCKER_RUN_ARGS+=(-it)
    if docker run "${DOCKER_RUN_ARGS[@]}" "${IMAGE_NAME}" "${ENCODER_CMD[@]}"; then
        echo ""
        echo -e "${GREEN}Encoder CLI stopped${NC}"
        echo -e "${GREEN}Log file location: ${VCAI_HOME}/encoder_cli/${LOG_FILE}${NC}"
    else
        echo -e "${RED}Failed to start encoder_cli${NC}"
        exit 1
    fi
fi
