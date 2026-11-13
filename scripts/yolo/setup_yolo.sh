#!/bin/bash

# Shared setup script for DeepStream-Yolo integration.
# - Ensures yolo/DeepStream-Yolo exists and is up to date
# - Applies local patches (if present)
# - Builds libnvdsinfer_custom_impl_Yolo.so
#
# IMPORTANT:
# - This script does NOT run sudo. If a root action is required, it will print instructions.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/common_paths.sh"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/tensorrt_headers.sh"

VCAI_ROOT="$(get_vcai_root "$SCRIPT_DIR")"
YOLO_DIR="$VCAI_ROOT/yolo"
DEEPSTREAM_YOLO_DIR="$YOLO_DIR/DeepStream-Yolo"
CUSTOM_IMPL_DIR="$DEEPSTREAM_YOLO_DIR/nvdsinfer_custom_impl_Yolo"
PATCHES_DIR="$VCAI_ROOT/patches"

echo "Setting up DeepStream-Yolo (shared scripts)"
echo "VCAI root: $VCAI_ROOT"
echo "YOLO directory: $YOLO_DIR"

mkdir -p "$YOLO_DIR"

if [ ! -d "$DEEPSTREAM_YOLO_DIR" ]; then
    echo "DeepStream-Yolo repository not found. Cloning..."
    (cd "$YOLO_DIR" && git clone https://github.com/marcoslucianops/DeepStream-Yolo.git)
else
    echo "DeepStream-Yolo repository found. Updating..."
    (cd "$DEEPSTREAM_YOLO_DIR" && git pull)
fi

if [ -d "$PATCHES_DIR" ]; then
    if [ -f "$PATCHES_DIR/nvdsparsebbox_Yolo.cpp" ]; then
        echo "Applying patch: nvdsparsebbox_Yolo.cpp"
        cp "$PATCHES_DIR/nvdsparsebbox_Yolo.cpp" "$CUSTOM_IMPL_DIR/"
    fi
fi

detect_and_export_cuda_vars
echo "Using CUDA_VER=$CUDA_VER (CUDA_ARCH_BIN=$CUDA_ARCH_BIN)"

# DeepStream headers live under <install>/sources/includes (full SDK, not runtime-only).
DEEPSTREAM_SYMLINK="/opt/nvidia/deepstream/deepstream"
DEEPSTREAM_PATH="$(detect_deepstream_path)"
DS_INCLUDES="$(find_deepstream_includes_dir)"

if [ -z "$DS_INCLUDES" ]; then
    DEEPSTREAM_PATH="$(detect_deepstream_path)"
    echo "ERROR: Could not find DeepStream header nvdsinfer_custom_impl.h"
    echo ""
    echo "The DeepStream-Yolo build needs the full DeepStream SDK (sources/includes/ with dev headers)."
    echo ""
    echo "Run diagnostics:"
    echo "  ./scripts/setup/bin/diagnose_deepstream.sh"
    echo ""
    if [ -n "$DEEPSTREAM_PATH" ]; then
        echo "Runtime detected at: $DEEPSTREAM_PATH"
        echo "Missing: $DEEPSTREAM_PATH/sources/includes/nvdsinfer_custom_impl.h"
        echo ""
        echo "If you installed only the .deb without sources, add the SDK tarball from NGC:"
        echo "  sudo DEEPSTREAM_TARBALL=\"\$HOME/Downloads/deepstream_sdk_v7.1.0_x86_64.tbz2\" \\"
        echo "    ./scripts/setup/bin/install_deepstream_sdk_sources.sh"
    else
        echo "No DeepStream tree found under /opt/nvidia."
        echo "Install the .deb (see INSTALL.md Step 2) or extract the SDK .tbz2 with"
        echo "  install_deepstream_sdk_sources.sh"
    fi
    if [ -n "$DEEPSTREAM_PATH" ] && [ ! -e "$DEEPSTREAM_SYMLINK" ]; then
        echo ""
        echo "Optional symlink (some Makefiles expect it):"
        echo "  sudo ln -sfn \"$DEEPSTREAM_PATH\" \"$DEEPSTREAM_SYMLINK\""
    fi
    exit 1
fi

echo "Using DeepStream includes: $DS_INCLUDES"
if [ ! -e "$DEEPSTREAM_SYMLINK" ] && [ -n "$DEEPSTREAM_PATH" ]; then
    echo "Note: symlink $DEEPSTREAM_SYMLINK is missing (makefile expects it for default -I paths)."
    echo "If the build still fails, run: sudo ln -sfn \"$DEEPSTREAM_PATH\" \"$DEEPSTREAM_SYMLINK\""
fi

echo "Building DeepStream-Yolo custom implementation..."
MAKEFILE="$CUSTOM_IMPL_DIR/Makefile"
OLD_DS_INCLUDE="/opt/nvidia/deepstream/deepstream/sources/includes"
if grep -q "$OLD_DS_INCLUDE" "$MAKEFILE"; then
    sed -i "s|-I${OLD_DS_INCLUDE}|-I${DS_INCLUDES}|g" "$MAKEFILE"
fi

ensure_tensorrt_headers_for_yolo "$VCAI_ROOT"
apply_yolo_tensorrt_makefile_pins "$MAKEFILE"

(cd "$CUSTOM_IMPL_DIR" && make clean && make CUDA_VER="$CUDA_VER")

YOLO_LIB="$CUSTOM_IMPL_DIR/libnvdsinfer_custom_impl_Yolo.so"
if [ ! -f "$YOLO_LIB" ]; then
    echo "ERROR: Expected library was not built: $YOLO_LIB"
    exit 1
fi

echo "✓ Built: $YOLO_LIB"

