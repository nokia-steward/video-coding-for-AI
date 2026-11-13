#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# RTP server Docker wrapper script for consolidated rtp_server/run.sh.

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
IMAGE_NAME="vcai-rtp-server:latest"
CONTAINER_NAME="vcai-rtp-server"
VCAI_HOME="${HOME}/.VCAI"

print_usage() {
    echo "Usage: $0 [OPTIONS] <transport> [host|host:port] [-- <rtp_server_args...>]"
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
    echo "Forwarded arguments (to rtp_server/run.sh):"
    echo "  <transport>                rtp | webrtc"
    echo "  [host|host:port]           Optional RTP output destination override"
    echo "  -- <rtp_server_args...>    Pass-through args for rtp_server binary"
    echo ""
    echo "Examples:"
    echo "  $0 rtp"
    echo "  $0 rtp 192.168.1.100:5006"
    echo "  $0 webrtc -- --webrtc-signaling-port 8080"
    echo "  $0 rtp -- --server-handshake-port 5004"
    echo "  $0 --background rtp"
    echo "  $0 -- --help"
}

BUILD_IMAGE=false
FORCE_REBUILD=false
FORCE_REBUILD_BASE=false
BACKGROUND=false
ENV_VARS=()
RTP_ARGS=()

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
            RTP_ARGS+=("$1")
            shift
            RTP_ARGS+=("$@")
            break
            ;;
        *)
            RTP_ARGS+=("$1")
            shift
            ;;
    esac
done

if [ "${#RTP_ARGS[@]}" -eq 0 ]; then
    echo -e "${RED}Error: missing rtp_server run arguments.${NC}"
    echo ""
    print_usage
    exit 1
fi

HELP_PASSTHROUGH=false
if [ "${#RTP_ARGS[@]}" -ge 2 ] && [ "${RTP_ARGS[0]}" = "--" ] && { [ "${RTP_ARGS[1]}" = "--help" ] || [ "${RTP_ARGS[1]}" = "-h" ]; }; then
    HELP_PASSTHROUGH=true
fi

TRANSPORT=""
if [ "$HELP_PASSTHROUGH" = false ]; then
    TRANSPORT="${RTP_ARGS[0]}"
    case "$TRANSPORT" in
        rtp|webrtc) ;;
        *)
            echo -e "${RED}Error: invalid transport '${TRANSPORT}' (expected rtp|webrtc).${NC}"
            exit 1
            ;;
    esac
fi

# Initialize directories if needed
if [ ! -d "${VCAI_HOME}/rtp_server" ]; then
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
        docker build --no-cache -f "${SCRIPT_DIR}/Dockerfile_rtp_server" -t "${IMAGE_NAME}" .
    else
        docker build -f "${SCRIPT_DIR}/Dockerfile_rtp_server" -t "${IMAGE_NAME}" .
    fi
    echo -e "${GREEN}Image built successfully${NC}"
fi

# Set up environment variables
export DISPLAY="${DISPLAY:-:0}"
export USER_ID="${USER_ID:-$(id -u)}"
export GROUP_ID="${GROUP_ID:-$(id -g)}"
export USERNAME="${USERNAME:-${USER}}"

XAUTH_MOUNT_ARGS=()
if [ -n "${XAUTHORITY}" ] && [ -f "${XAUTHORITY}" ]; then
    XAUTH_MOUNT_ARGS=(-v "${XAUTHORITY}:${XAUTHORITY}:ro" -e "XAUTHORITY=${XAUTHORITY}")
elif [ -f "${HOME}/.Xauthority" ]; then
    export XAUTHORITY="${HOME}/.Xauthority"
    XAUTH_MOUNT_ARGS=(-v "${HOME}/.Xauthority:${HOME}/.Xauthority:ro" -e "XAUTHORITY=${XAUTHORITY}")
fi

# Check if container is already running
if docker ps -q -f name="${CONTAINER_NAME}" | grep -q .; then
    echo -e "${YELLOW}Container ${CONTAINER_NAME} is already running${NC}"
    echo -e "${YELLOW}Stopping existing container...${NC}"
    docker stop "${CONTAINER_NAME}" >/dev/null 2>&1 || true
    docker rm "${CONTAINER_NAME}" >/dev/null 2>&1 || true
fi

RTP_CMD=("/opt/vcai/rtp_server/run.sh" "${RTP_ARGS[@]}")

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
    -e USER_ID="${USER_ID}"
    -e GROUP_ID="${GROUP_ID}"
    -e USERNAME="${USERNAME}"
    -v "${VCAI_HOME}/rtp_server:/opt/vcai/data/rtp_server"
    -v "${PROJECT_ROOT}/yolo:/opt/vcai/yolo"
    -v /tmp/.X11-unix:/tmp/.X11-unix
    --device /dev/dri
)

if [ "${#XAUTH_MOUNT_ARGS[@]}" -gt 0 ]; then
    DOCKER_RUN_ARGS+=("${XAUTH_MOUNT_ARGS[@]}")
fi

for env_var in "${ENV_VARS[@]}"; do
    DOCKER_RUN_ARGS+=(-e "$env_var")
done

if [ -f "${PROJECT_ROOT}/rtp_server/rtp_server_config.ini" ]; then
    DOCKER_RUN_ARGS+=(-v "${PROJECT_ROOT}/rtp_server/rtp_server_config.ini:/opt/vcai/rtp_server/rtp_server_config.ini:ro")
fi

# Check X11 permissions - warn if xhost might be needed
if ! xhost | grep -q "LOCAL:"; then
    echo -e "${YELLOW}Warning: X11 access might be restricted. If display doesn't work, run:${NC}"
    echo -e "${YELLOW}  xhost +local:docker${NC}"
    echo ""
fi

echo -e "${BLUE}Starting RTP server container...${NC}"
echo -e "${BLUE}  Image: ${IMAGE_NAME}${NC}"
echo -e "${BLUE}  Container: ${CONTAINER_NAME}${NC}"
echo -e "${BLUE}  Entrypoint: /opt/vcai/rtp_server/run.sh${NC}"
echo -e "${BLUE}  Network: host (accessible from outside)${NC}"
if [ -n "${TRANSPORT}" ]; then
    echo -e "${BLUE}  Transport: ${TRANSPORT}${NC}"
fi
echo -e "${BLUE}  Forwarded args: ${RTP_ARGS[*]}${NC}"
echo ""

cd "${PROJECT_ROOT}"

cleanup() {
    echo ""
    echo -e "${YELLOW}Shutting down RTP server...${NC}"
    docker stop "${CONTAINER_NAME}" >/dev/null 2>&1 || true
    docker rm "${CONTAINER_NAME}" >/dev/null 2>&1 || true
    echo -e "${GREEN}Cleanup complete${NC}"
    exit 0
}

trap cleanup SIGINT SIGTERM

if [ "$BACKGROUND" = true ]; then
    DOCKER_RUN_ARGS+=(-d)
    if docker run "${DOCKER_RUN_ARGS[@]}" "${IMAGE_NAME}" "${RTP_CMD[@]}"; then
        echo ""
        echo -e "${GREEN}RTP server started in background${NC}"
        echo -e "${BLUE}To view logs: docker logs -f ${CONTAINER_NAME}${NC}"
        echo -e "${BLUE}To stop: docker stop ${CONTAINER_NAME}${NC}"
    else
        echo -e "${RED}Failed to start RTP server${NC}"
        exit 1
    fi
else
    DOCKER_RUN_ARGS+=(-it)
    if docker run "${DOCKER_RUN_ARGS[@]}" "${IMAGE_NAME}" "${RTP_CMD[@]}"; then
        echo ""
        echo -e "${GREEN}RTP server stopped${NC}"
    else
        echo -e "${RED}Failed to start RTP server${NC}"
        exit 1
    fi
fi
