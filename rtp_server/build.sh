#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Exit immediately if a command exits with a non-zero status.
set -e

# --- Dynamically Set Project Root ---
# This allows the script to be run from any directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
if [[ "$(basename "${SCRIPT_DIR}")" == "rtp_server" ]]; then
    PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"
else
    PROJECT_ROOT=$(pwd)
fi

clean_requested=false
debug_requested=false
asan_requested=false

for arg in "$@"; do
    case "$arg" in
        clean)
            clean_requested=true
            ;;
        debug)
            debug_requested=true
            ;;
        asan)
            asan_requested=true
            ;;
    esac
done

# Check for clean parameter
if [ "$clean_requested" = true ]; then
    echo "Cleaning rtp_server build files..."
    rm -rf "${PROJECT_ROOT}/build/rtp_server"
    echo "✅ Clean completed!"
    echo ""
fi

echo "Building rtp_server..."

# Use GStreamer
echo "Using GStreamer..."

# Prevent stale environment overrides from prior sessions.
unset PKG_CONFIG_PATH
unset CMAKE_PREFIX_PATH
unset CMAKE_LIBRARY_PATH

# Check if GStreamer is available in the system installation
if ! pkg-config --exists gstreamer-1.0; then
    echo "Error: GStreamer 1.0 not found in system installation."
    echo "Please install the required GStreamer development packages."
    exit 1
fi

echo "System GStreamer found: $(pkg-config --modversion gstreamer-1.0)"

echo "Configuring and building rtp_server..."

# Check for ASAN parameter
CMAKE_ARGS="-DBUILD_RTP_SERVER=ON -DBUILD_VCAI_DECODER=ON -DCMAKE_PREFIX_PATH= -DCMAKE_LIBRARY_PATH="
if [ "$debug_requested" = true ]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    echo "Building with debug symbols enabled..."
fi
if [ "$asan_requested" = true ]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DENABLE_ASAN=ON"
    echo "Building with AddressSanitizer enabled..."
fi

cmake -S "${PROJECT_ROOT}" -B "${PROJECT_ROOT}/build/rtp_server" ${CMAKE_ARGS}
cmake --build "${PROJECT_ROOT}/build/rtp_server" --target gstvcai_decoder rtp_server

echo "✅ rtp_server build completed successfully!"
