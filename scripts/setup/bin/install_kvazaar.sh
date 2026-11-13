#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# ==============================================================================
# Kvazaar Installation Script (lib/kvazaar)
#
# Downloads, builds, and installs kvazaar and gst-kvazaar into lib/kvazaar.
# No root or sudo required. encoder_cli/run.sh will use this install when present.
#
# 1. Check for necessary build tools.
# 2. Clone kvazaar and gst-kvazaar if not already present.
# 3. Configure, build, and install kvazaar into lib/kvazaar.
# 4. Build and install gst-kvazaar into the same prefix.
#
# Location: scripts/setup/bin/ — run from repo root. Project root is resolved
# by finding the directory that contains patches/ (walk up from this script's directory).
# ==============================================================================

set -e

# --- Resolve project root ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "${PROJECT_ROOT:-}" ] && [ -d "${PROJECT_ROOT}/patches" ]; then
    cd "${PROJECT_ROOT}"
elif [ -d "${SCRIPT_DIR}/../../../patches" ]; then
    PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
    cd "${PROJECT_ROOT}"
else
    PROJECT_ROOT="${SCRIPT_DIR}"
    while [ "${PROJECT_ROOT}" != "/" ]; do
        if [ -d "${PROJECT_ROOT}/patches" ]; then
            cd "${PROJECT_ROOT}"
            break
        fi
        PROJECT_ROOT="$(dirname "${PROJECT_ROOT}")"
    done
    if [ ! -d "${PROJECT_ROOT}/patches" ]; then
        echo "[ERROR] Could not find project root (no patches/ directory)." >&2
        exit 1
    fi
fi

# --- Configuration ---
PROJECT_ROOT="$(pwd)"
KVAZAAR_INSTALL_PREFIX="${PROJECT_ROOT}/lib/kvazaar"
KVAZAAR_REPO="https://github.com/ultravideo/kvazaar.git"
KVAZAAR_VERSION="master"
GST_KVAZAAR_REPO="https://github.com/ahresse/gst-kvazaar.git"
GST_KVAZAAR_VERSION="master"
TEMP_DIR="${PROJECT_ROOT}/temp"
PATCHES_DIR="${PROJECT_ROOT}/patches"
KVAZAAR_SRC_DIR="${TEMP_DIR}/kvazaar"
GST_KVAZAAR_SRC_DIR="${TEMP_DIR}/gst-kvazaar"

# --- Helper Functions ---
print_info() {
    echo -e "\033[1;34m[INFO]\033[0m $1"
}
print_success() {
    echo -e "\033[1;32m[SUCCESS]\033[0m $1"
}
print_error() {
    echo -e "\033[1;31m[ERROR]\033[0m $1" >&2
    exit 1
}
print_warning() {
    echo -e "\033[1;33m[WARNING]\033[0m $1"
}

# --- 1. Dependency Check ---
print_info "Checking for required tools (git, autoconf, automake, libtoolize, make, gcc, meson, ninja)..."
command -v git >/dev/null 2>&1 || print_error "git is not installed. Please install it."
command -v autoconf >/dev/null 2>&1 || print_error "autoconf is not installed. Please install it (e.g., 'apt-get install autoconf')."
command -v automake >/dev/null 2>&1 || print_error "automake is not installed. Please install it (e.g., 'apt-get install automake')."
command -v libtoolize >/dev/null 2>&1 || print_error "libtool is not installed. Please install it (e.g., 'apt-get install libtool')."
command -v make >/dev/null 2>&1 || print_error "make is not installed. Please install it (e.g., 'apt-get install build-essential')."
command -v gcc >/dev/null 2>&1 || print_error "gcc is not installed. Please install it (e.g., 'apt-get install build-essential')."
command -v meson >/dev/null 2>&1 || print_error "meson is not installed. Please install it (e.g., 'pip3 install meson')."
command -v ninja >/dev/null 2>&1 || print_error "ninja is not installed. Please install it (e.g., 'apt-get install ninja-build')."
if command -v yasm >/dev/null 2>&1; then
    print_info "yasm found - will enable assembly optimizations."
else
    print_warning "yasm not found - assembly optimizations will be disabled. Consider installing yasm for better performance."
fi
if pkg-config --exists gstreamer-1.0; then
    print_info "GStreamer $(pkg-config --modversion gstreamer-1.0) development packages found."
else
    print_error "GStreamer development packages not found. Please install them (e.g., 'apt-get install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev')."
fi
print_info "All required tools are present."

# --- 2. Create directories ---
mkdir -p "${TEMP_DIR}"
mkdir -p "${KVAZAAR_INSTALL_PREFIX}"

# --- 3. Clone or Update Source ---
if [ -d "${KVAZAAR_SRC_DIR}" ]; then
    print_info "Kvazaar source directory already exists. Updating..."
    (cd "${KVAZAAR_SRC_DIR}" && git fetch origin && git reset --hard "origin/${KVAZAAR_VERSION}")
else
    print_info "Cloning kvazaar repository..."
    git clone --depth 1 --branch "${KVAZAAR_VERSION}" "${KVAZAAR_REPO}" "${KVAZAAR_SRC_DIR}"
fi
if [ -d "${GST_KVAZAAR_SRC_DIR}" ]; then
    print_info "gst-kvazaar source directory already exists. Updating..."
    (cd "${GST_KVAZAAR_SRC_DIR}" && git fetch origin && git reset --hard "origin/${GST_KVAZAAR_VERSION}")
else
    print_info "Cloning gst-kvazaar repository..."
    git clone --depth 1 --branch "${GST_KVAZAAR_VERSION}" "${GST_KVAZAAR_REPO}" "${GST_KVAZAAR_SRC_DIR}"
fi

# --- 4. Apply Patches to gst-kvazaar ---
GST_KVAZAAR_PATCH_SRC="${PATCHES_DIR}/gstkvazaarenc.c"
GST_KVAZAAR_PATCH_DEST="${GST_KVAZAAR_SRC_DIR}/src/gstkvazaarenc.c"
if [ -f "${GST_KVAZAAR_PATCH_SRC}" ]; then
    print_info "Found patched gstkvazaarenc.c file. Applying patch..."
    cp -f "${GST_KVAZAAR_PATCH_SRC}" "${GST_KVAZAAR_PATCH_DEST}"
    print_success "Applied patched gstkvazaarenc.c to fix compilation issues."
else
    print_warning "No patched gstkvazaarenc.c found at ${GST_KVAZAAR_PATCH_SRC}. Using original source (may have compilation issues)."
fi

# --- 5. Build and Install Kvazaar into lib/kvazaar ---
print_info "Building and installing kvazaar into ${KVAZAAR_INSTALL_PREFIX}..."
(
    cd "${KVAZAAR_SRC_DIR}"
    print_info "Generating configure script..."
    ./autogen.sh
    print_info "Configuring build..."
    ./configure --prefix="${KVAZAAR_INSTALL_PREFIX}"
    print_info "Compiling kvazaar (this may take a few minutes)..."
    make -j"$(nproc)"
    print_info "Installing kvazaar..."
    make install
)
print_success "Kvazaar installed successfully."

# --- 6. Build and Install gst-kvazaar into lib/kvazaar ---
GST_KVAZAAR_BUILD_DIR="${GST_KVAZAAR_SRC_DIR}/build"
if [ -d "${GST_KVAZAAR_BUILD_DIR}" ]; then
    print_info "Removing old gst-kvazaar build directory..."
    rm -rf "${GST_KVAZAAR_BUILD_DIR}"
fi
LIB_ARCH="$(uname -m)-linux-gnu"
PKG_CONFIG_PATHS="${KVAZAAR_INSTALL_PREFIX}/lib/pkgconfig"
[ -d "${KVAZAAR_INSTALL_PREFIX}/lib/${LIB_ARCH}/pkgconfig" ] && PKG_CONFIG_PATHS="${KVAZAAR_INSTALL_PREFIX}/lib/${LIB_ARCH}/pkgconfig:${PKG_CONFIG_PATHS}"
(
    cd "${GST_KVAZAAR_SRC_DIR}"
    export PKG_CONFIG_PATH="${PKG_CONFIG_PATHS}:${PKG_CONFIG_PATH:-}"
    print_info "Configuring gst-kvazaar build..."
    meson setup build --prefix="${KVAZAAR_INSTALL_PREFIX}"
    print_info "Compiling gst-kvazaar plugin..."
    ninja -C build
    print_info "Installing gst-kvazaar plugin..."
    ninja -C build install
)
print_success "gst-kvazaar plugin installed successfully."

# --- 7. Verify Installation ---
export PKG_CONFIG_PATH="${KVAZAAR_INSTALL_PREFIX}/lib/pkgconfig:${KVAZAAR_INSTALL_PREFIX}/lib/${LIB_ARCH}/pkgconfig:${PKG_CONFIG_PATH:-}"
if pkg-config --exists kvazaar 2>/dev/null; then
    VERSION=$(pkg-config --modversion kvazaar)
    print_success "Kvazaar version ${VERSION} is available in lib/kvazaar."
else
    print_warning "Kvazaar pkg-config not found; ensure PKG_CONFIG_PATH includes lib/kvazaar/lib/pkgconfig when building."
fi
KVAAZAARNC_SO=""
for dir in "${KVAZAAR_INSTALL_PREFIX}/lib/gstreamer-1.0" "${KVAZAAR_INSTALL_PREFIX}/lib/${LIB_ARCH}/gstreamer-1.0"; do
    if [ -f "${dir}/libgstkvazaarenc.so" ]; then
        KVAAZAARNC_SO="${dir}/libgstkvazaarenc.so"
        break
    fi
done
if [ -n "${KVAAZAARNC_SO}" ]; then
    print_success "gst-kvazaar plugin is at: ${KVAAZAARNC_SO}"
else
    print_warning "libgstkvazaarenc.so not found under lib/kvazaar; encoder_cli may not find the plugin."
fi

# --- 8. Cleanup ---
print_info "Cleaning up temporary files..."
rm -rf "${TEMP_DIR}"

# --- 9. Final Instructions ---
print_success "Kvazaar and gst-kvazaar installation is complete!"
echo ""
print_info "Installation prefix: ${KVAZAAR_INSTALL_PREFIX}"
print_info "encoder_cli/run.sh will pick up this install automatically when present."
print_info "rtp_server uses its configured runtime/plugin paths and does not consume lib/kvazaar directly."
echo ""
print_info "To use kvazaar in other builds, set:"
print_info "  export PKG_CONFIG_PATH=\"${KVAZAAR_INSTALL_PREFIX}/lib/pkgconfig:\$PKG_CONFIG_PATH\""
print_info "  export LD_LIBRARY_PATH=\"${KVAZAAR_INSTALL_PREFIX}/lib:\$LD_LIBRARY_PATH\""
print_info "To verify: pkg-config --modversion kvazaar && gst-inspect-1.0 kvazaarenc"
echo ""
