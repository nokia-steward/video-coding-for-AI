#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
PROJECT_ROOT="$(dirname "${SCRIPT_DIR}")"

clean_requested=false
debug_requested=false
minimal_app_requested=true

for arg in "$@"; do
    case "$arg" in
        clean)
            clean_requested=true
            ;;
        debug)
            debug_requested=true
            ;;
        noapp)
            minimal_app_requested=false
            ;;
    esac
done

if [ "${clean_requested}" = true ]; then
    echo "Cleaning vcai_decoder build files..."
    rm -rf "${PROJECT_ROOT}/build/vcai_decoder"
    echo "Clean completed."
fi

echo "Building vcai_decoder against GStreamer..."

if ! pkg-config --exists gstreamer-1.0; then
    echo "Error: gstreamer-1.0 was not found by pkg-config."
    exit 1
fi

system_gst_version="$(pkg-config --modversion gstreamer-1.0)"
echo "Found GStreamer version: ${system_gst_version}"

CMAKE_ARGS="-DBUILD_VCAI_DECODER=ON"
if [ "${debug_requested}" = true ]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    echo "Building with debug symbols enabled..."
fi
if [ "${minimal_app_requested}" = true ]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DBUILD_VCAI_DECODER_MINIMAL_APP=ON"
else
    CMAKE_ARGS="${CMAKE_ARGS} -DBUILD_VCAI_DECODER_MINIMAL_APP=OFF"
fi

cmake -S "${PROJECT_ROOT}" -B "${PROJECT_ROOT}/build/vcai_decoder" ${CMAKE_ARGS}
if [ "${minimal_app_requested}" = true ]; then
    cmake --build "${PROJECT_ROOT}/build/vcai_decoder" --target gstvcai_decoder sf_decoder_minimal_test_app
else
    cmake --build "${PROJECT_ROOT}/build/vcai_decoder" --target gstvcai_decoder
fi

echo "vcai_decoder build completed successfully."
echo "Plugin library:"
echo "  ${PROJECT_ROOT}/build/vcai_decoder/vcai_decoder/libgstvcai_decoder.so"
if [ "${minimal_app_requested}" = true ]; then
    echo "Minimal test app:"
    echo "  ${PROJECT_ROOT}/build/vcai_decoder/vcai_decoder/sf_decoder_minimal_test_app"
fi
