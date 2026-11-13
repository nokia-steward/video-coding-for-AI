#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Download and extract ONNX Runtime Linux x64 or aarch64 CPU prebuilt into lib/onnxruntime.
# Idempotent: skips if lib/onnxruntime already has include/ and lib/.
# Run from repo root (bootstrap.sh core or all). Other platforms: exit 0 with message.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$(cd "$SCRIPT_DIR/../../.." && pwd)"
PROJECT_ROOT="$(pwd)"

ONNX_VERSION="1.24.3"
LIB_DIR="${PROJECT_ROOT}/lib"
ONNX_DIR="${LIB_DIR}/onnxruntime"
ARCH="$(uname -m)"

print_info() {
    echo -e "\033[1;34m[INFO]\033[0m $1"
}

print_success() {
    echo -e "\033[1;32m[SUCCESS]\033[0m $1"
}

# Support Linux x86_64 and aarch64 only
if [ "$(uname -s)" != "Linux" ]; then
    print_info "ONNX Runtime automated install is only for Linux. Skip. See docs/part03/install-onnx-runtime.md for manual install."
    exit 0
fi
case "${ARCH}" in
    x86_64)  TARBALL_NAME="onnxruntime-linux-x64-${ONNX_VERSION}.tgz" ;;
    aarch64) TARBALL_NAME="onnxruntime-linux-aarch64-${ONNX_VERSION}.tgz" ;;
    *)
        print_info "ONNX Runtime automated install is only for Linux x86_64 and aarch64. Skip. See docs/part03/install-onnx-runtime.md for manual install."
        exit 0
        ;;
esac
TARBALL_PATH="${LIB_DIR}/${TARBALL_NAME}"
DOWNLOAD_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ONNX_VERSION}/${TARBALL_NAME}"

# Skip if already present
if [ -d "${ONNX_DIR}/include" ] && [ -d "${ONNX_DIR}/lib" ]; then
    print_info "ONNX Runtime already present in lib/onnxruntime; skipping."
    exit 0
fi

mkdir -p "${LIB_DIR}"

# Download (prefer wget, fallback to curl)
if command -v wget >/dev/null 2>&1; then
    print_info "Downloading ONNX Runtime ${ONNX_VERSION}..."
    wget -q --show-progress -O "${TARBALL_PATH}" "${DOWNLOAD_URL}" || {
        echo "wget failed" >&2
        exit 1
    }
elif command -v curl >/dev/null 2>&1; then
    print_info "Downloading ONNX Runtime ${ONNX_VERSION}..."
    curl -sSL -o "${TARBALL_PATH}" "${DOWNLOAD_URL}" || {
        echo "curl failed" >&2
        exit 1
    }
else
    echo "[ERROR] Neither wget nor curl found. Install one to download ONNX Runtime." >&2
    exit 1
fi

print_info "Extracting to lib/onnxruntime..."
mkdir -p "${ONNX_DIR}"
tar -xzf "${TARBALL_PATH}" -C "${ONNX_DIR}" --strip-components=1
rm -f "${TARBALL_PATH}"

# CMake config references lib64/ but the tarball puts .so in lib/; symlink so find_package finds them
if [ -d "${ONNX_DIR}/lib" ] && [ ! -d "${ONNX_DIR}/lib64" ]; then
    ln -s lib "${ONNX_DIR}/lib64"
fi
# CMake config expects include/onnxruntime but the tarball has headers directly under include/; symlink
if [ -d "${ONNX_DIR}/include" ] && [ ! -d "${ONNX_DIR}/include/onnxruntime" ]; then
    ln -s . "${ONNX_DIR}/include/onnxruntime"
fi

if [ -d "${ONNX_DIR}/include" ] && [ -d "${ONNX_DIR}/lib" ]; then
    print_success "ONNX Runtime ${ONNX_VERSION} installed in lib/onnxruntime"
else
    echo "[ERROR] Extraction did not produce include/ and lib/ under lib/onnxruntime" >&2
    exit 1
fi
