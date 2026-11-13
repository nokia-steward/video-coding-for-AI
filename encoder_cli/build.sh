#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Exit immediately if a command exits with a non-zero status.
set -e

clean_requested=false
debug_requested=false

for arg in "$@"; do
    case "$arg" in
        clean)
            clean_requested=true
            ;;
        debug)
            debug_requested=true
            ;;
    esac
done

# Check for clean parameter
if [ "$clean_requested" = true ]; then
    echo "Cleaning encoder_cli build files..."
    rm -rf ../build/encoder_cli
    echo "✅ Clean completed!"
    echo ""
fi

echo "Building encoder_cli..."

# Check if the DeepStream YOLO export environment exists
VCAI_PATH="$(cd "$(dirname "$0")/.." && pwd)"
ENV_DIR="$VCAI_PATH/.env/ds_export"

if [ ! -d "$ENV_DIR" ]; then
    echo ""
    echo "❌ ERROR: DeepStream YOLO export environment not found!"
    echo ""
    echo "The YOLO model conversion requires a dedicated Python environment with"
    echo "compatible versions of PyTorch and ultralytics for DeepStream 7.1."
    echo ""
    echo "To set up the environment, run:"
    echo "  cd $VCAI_PATH"
    echo "  ./scripts/setup/bootstrap.sh ds-env"
    echo ""
    exit 1
fi

# Check if the DeepStream-Yolo library is compiled
DEEPSTREAM_YOLO_LIB="$VCAI_PATH/yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo/libnvdsinfer_custom_impl_Yolo.so"

if [ ! -f "$DEEPSTREAM_YOLO_LIB" ]; then
    echo ""
    echo "❌ ERROR: DeepStream-Yolo library not found!"
    echo ""
    echo "The YOLO object detection requires the DeepStream-Yolo custom implementation"
    echo "library to be compiled. This library provides the TensorRT integration"
    echo "needed for YOLO model inference in DeepStream."
    echo ""
    echo "To compile the library, run:"
    echo "  cd $VCAI_PATH/encoder_cli"
    echo "  ./scripts/setup_yolo.sh"
    echo ""
    exit 1
fi

# Use lib/onnxruntime if present (e.g. after bootstrap.sh core)
if [ -d "${VCAI_PATH}/lib/onnxruntime/include" ]; then
    export CMAKE_PREFIX_PATH="${VCAI_PATH}/lib/onnxruntime:${CMAKE_PREFIX_PATH:-}"
fi

echo "Configuring and building encoder_cli..."

CMAKE_ARGS="-DBUILD_ENCODER_CLI=ON"
if [ "$debug_requested" = true ]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DCMAKE_BUILD_TYPE=Debug"
    echo "Building with debug symbols enabled..."
fi

# Temporarily exclude custom pkg-config path to use GStreamer libraries
export PKG_CONFIG_PATH_OLD="$PKG_CONFIG_PATH"
export PKG_CONFIG_PATH=""
cmake -S .. -B ../build/encoder_cli ${CMAKE_ARGS}
cmake --build ../build/encoder_cli --target encoder_cli
# Restore original PKG_CONFIG_PATH
export PKG_CONFIG_PATH="$PKG_CONFIG_PATH_OLD"

echo "✅ encoder_cli build completed successfully!"
