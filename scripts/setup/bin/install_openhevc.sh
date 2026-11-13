#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# ==============================================================================
# OpenHEVC + gst-openhevc Installation Script (lib/openhevc)
#
# Installs OpenHEVC (HEVC decoder library) and the gst-openhevc GStreamer plugin
# into lib/openhevc. gst-openhevc requires the OpenHEVC ffmpeg_update branch.
# No root or sudo required. encoder_cli/run.sh will use this install when present.
#
# Project root is resolved via patches/ (same as install_kvazaar.sh).
# After install, verify: PKG_CONFIG_PATH=lib/openhevc/lib/pkgconfig pkg-config --modversion libopenhevc && gst-inspect-1.0 openhevcdec
# ==============================================================================

set -e

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

OPENHEVC_REPO="https://github.com/OpenHEVC/openHEVC.git"
OPENHEVC_BRANCH="ffmpeg_update"
GST_OPENHEVC_REPO="https://github.com/ystreet/gst-openhevc.git"
GST_OPENHEVC_BRANCH="master"

PROJECT_ROOT="$(pwd)"
OPENHEVC_INSTALL_PREFIX="${PROJECT_ROOT}/lib/openhevc"
TEMP_DIR="${PROJECT_ROOT}/temp"
PATCHES_DIR="${PROJECT_ROOT}/patches"
OPENHEVC_SRC_DIR="${TEMP_DIR}/openHEVC"
GST_OPENHEVC_SRC_DIR="${TEMP_DIR}/gst-openhevc"

print_info() { echo -e "\033[1;34m[INFO]\033[0m $1"; }
print_success() { echo -e "\033[1;32m[SUCCESS]\033[0m $1"; }
print_error() { echo -e "\033[1;31m[ERROR]\033[0m $1" >&2; exit 1; }
print_warning() { echo -e "\033[1;33m[WARNING]\033[0m $1"; }

print_info "Checking tools (git, cmake, make, gcc, meson, ninja)..."
command -v git >/dev/null 2>&1 || print_error "git not installed"
command -v cmake >/dev/null 2>&1 || print_error "cmake not installed"
command -v make >/dev/null 2>&1 || print_error "make not installed"
command -v gcc >/dev/null 2>&1 || print_error "gcc not installed"
command -v meson >/dev/null 2>&1 || print_error "meson not installed"
command -v ninja >/dev/null 2>&1 || print_error "ninja not installed"
if command -v yasm >/dev/null 2>&1; then
    print_info "yasm found"
else
    print_warning "yasm not found - OpenHEVC may build without asm optimizations"
fi
if pkg-config --exists gstreamer-1.0; then
    print_info "GStreamer $(pkg-config --modversion gstreamer-1.0) found"
else
    print_error "GStreamer dev packages missing (libgstreamer1.0-dev, plugins-base, etc.)"
fi

mkdir -p "${TEMP_DIR}"
mkdir -p "${OPENHEVC_INSTALL_PREFIX}"

# --- Clone OpenHEVC (ffmpeg_update required by gst-openhevc) ---
if [ -d "${OPENHEVC_SRC_DIR}" ]; then
    print_info "OpenHEVC source exists; updating..."
    (cd "${OPENHEVC_SRC_DIR}" && git fetch origin && git checkout "${OPENHEVC_BRANCH}" && git reset --hard "origin/${OPENHEVC_BRANCH}")
else
    print_info "Cloning OpenHEVC branch ${OPENHEVC_BRANCH}..."
    git clone --depth 1 --branch "${OPENHEVC_BRANCH}" "${OPENHEVC_REPO}" "${OPENHEVC_SRC_DIR}"
fi

# --- Clone gst-openhevc ---
if [ -d "${GST_OPENHEVC_SRC_DIR}" ]; then
    print_info "gst-openhevc source exists; updating..."
    (cd "${GST_OPENHEVC_SRC_DIR}" && git fetch origin && git reset --hard "origin/${GST_OPENHEVC_BRANCH}")
else
    print_info "Cloning gst-openhevc..."
    git clone --depth 1 --branch "${GST_OPENHEVC_BRANCH}" "${GST_OPENHEVC_REPO}" "${GST_OPENHEVC_SRC_DIR}"
fi

# OpenHEVC: append libopenhevc/openhevc.c to LibOpenHevcWrapper (oh_* API for gst-openhevc)
if [ -f "${PATCHES_DIR}/openhevc_cmake_append_openhevc_c.patch" ]; then
    print_info "Applying openhevc_cmake_append_openhevc_c.patch"
    (cd "${OPENHEVC_SRC_DIR}" && patch -p1 --forward --batch -i "${PATCHES_DIR}/openhevc_cmake_append_openhevc_c.patch") || true
    OPENHEVC_C="${OPENHEVC_SRC_DIR}/libopenhevc/openhevc.c"
    if [ -f "${OPENHEVC_C}" ] && grep -q '^unsigned openhevc_version(void)' "${OPENHEVC_C}"; then
        print_info "Removing duplicate openhevc_version/openhevc_configuration from openhevc.c"
        python3 << PY
from pathlib import Path
p = Path("${OPENHEVC_C}")
t = p.read_text()
t = t.replace(
    """unsigned openhevc_version(void){
    return LIBOPENHEVC_VERSION_INT;
}

""",
    "",
    1,
)
t = t.replace(
    """const char *openhevc_configuration(void){
    return FFMPEG_CONFIGURATION;
}

""",
    "",
    1,
)
p.write_text(t)
PY
    fi
fi

# OpenHEVC: glibc 2.32+ removed sys/sysctl.h — patch libavutil/cpu.c before cmake
if [ -f "${PATCHES_DIR}/openhevc_libavutil_cpu_sysctl.patch" ]; then
    print_info "Applying openhevc_libavutil_cpu_sysctl.patch (Linux sysctl.h removal)"
    (cd "${OPENHEVC_SRC_DIR}" && patch -p1 --forward --batch -i "${PATCHES_DIR}/openhevc_libavutil_cpu_sysctl.patch") || \
        grep -q '__linux__' "${OPENHEVC_SRC_DIR}/libavutil/cpu.c" 2>/dev/null || \
        print_warning "sysctl patch not applied; build may fail on modern glibc"
fi

# OpenHEVC: aarch64 support (no x86 asm; use aarch64 config and timer)
if [ "$(uname -m)" = "aarch64" ]; then
    if [ -f "${PATCHES_DIR}/openhevc_aarch64_cmake.patch" ]; then
        print_info "Applying openhevc_aarch64_cmake.patch (aarch64 platform)"
        (cd "${OPENHEVC_SRC_DIR}" && patch -p1 --forward --batch -i "${PATCHES_DIR}/openhevc_aarch64_cmake.patch") || true
    fi
    if [ -f "${PATCHES_DIR}/openhevc_aarch64_cpu_cmake.patch" ]; then
        print_info "Applying openhevc_aarch64_cpu_cmake.patch (aarch64 cpu.c in build)"
        (cd "${OPENHEVC_SRC_DIR}" && patch -p1 --forward --batch -i "${PATCHES_DIR}/openhevc_aarch64_cpu_cmake.patch") || true
    fi
    print_info "Creating platform/aarch64/config.h and libavutil/aarch64/timer.h for aarch64 build"
    mkdir -p "${OPENHEVC_SRC_DIR}/platform/aarch64"
    sed -e 's/#define ARCH_AARCH64 0/#define ARCH_AARCH64 1/' \
        -e 's/#define ARCH_ARM 1/#define ARCH_ARM 0/' \
        -e 's/#define HAVE_INLINE_ASM 1/#define HAVE_INLINE_ASM 0/' \
        "${OPENHEVC_SRC_DIR}/platform/arm/config.h" > "${OPENHEVC_SRC_DIR}/platform/aarch64/config.h.tmp"
    # Use system libm on aarch64 to avoid static declaration conflicts (libm.h)
    (head -n -1 "${OPENHEVC_SRC_DIR}/platform/aarch64/config.h.tmp"
     echo "#define HAVE_COPYSIGN 1"
     echo "#define HAVE_ERF 1"
     echo "#define HAVE_ISFINITE 1"
     echo "#define HAVE_HYPOT 1"
     tail -1 "${OPENHEVC_SRC_DIR}/platform/aarch64/config.h.tmp") > "${OPENHEVC_SRC_DIR}/platform/aarch64/config.h"
    rm -f "${OPENHEVC_SRC_DIR}/platform/aarch64/config.h.tmp"
    mkdir -p "${OPENHEVC_SRC_DIR}/libavutil/aarch64"
    if [ -f "${PATCHES_DIR}/openhevc_aarch64_timer.h" ]; then
        cp -f "${PATCHES_DIR}/openhevc_aarch64_timer.h" "${OPENHEVC_SRC_DIR}/libavutil/aarch64/timer.h"
    else
        print_error "Missing ${PATCHES_DIR}/openhevc_aarch64_timer.h for aarch64 build"
    fi
    if [ -f "${PATCHES_DIR}/openhevc_aarch64_bswap.h" ]; then
        cp -f "${PATCHES_DIR}/openhevc_aarch64_bswap.h" "${OPENHEVC_SRC_DIR}/libavutil/aarch64/bswap.h"
    else
        print_error "Missing ${PATCHES_DIR}/openhevc_aarch64_bswap.h for aarch64 build"
    fi
    mkdir -p "${OPENHEVC_SRC_DIR}/libavcodec/aarch64"
    if [ -f "${PATCHES_DIR}/openhevc_aarch64_cabac.h" ]; then
        cp -f "${PATCHES_DIR}/openhevc_aarch64_cabac.h" "${OPENHEVC_SRC_DIR}/libavcodec/aarch64/cabac.h"
    else
        print_error "Missing ${PATCHES_DIR}/openhevc_aarch64_cabac.h for aarch64 build"
    fi
    if [ -f "${PATCHES_DIR}/openhevc_aarch64_cpu.c" ]; then
        cp -f "${PATCHES_DIR}/openhevc_aarch64_cpu.c" "${OPENHEVC_SRC_DIR}/libavutil/aarch64/cpu.c"
    else
        print_error "Missing ${PATCHES_DIR}/openhevc_aarch64_cpu.c for aarch64 build"
    fi
fi

if [ -f "${PATCHES_DIR}/gstopenhevcviddec.c" ]; then
    print_info "Applying patched gstopenhevcviddec.c"
    cp -f "${PATCHES_DIR}/gstopenhevcviddec.c" "${GST_OPENHEVC_SRC_DIR}/ext/openhevc/gstopenhevcviddec.c"
fi

# --- Remove previous build artifacts ---
print_info "Removing previous OpenHEVC/gst-openhevc build artifacts..."
rm -rf "${OPENHEVC_SRC_DIR}/build" "${OPENHEVC_SRC_DIR}/_build"
rm -f "${OPENHEVC_SRC_DIR}/CMakeCache.txt"
rm -rf "${OPENHEVC_SRC_DIR}/CMakeFiles"
rm -rf "${GST_OPENHEVC_SRC_DIR}/build" "${GST_OPENHEVC_SRC_DIR}/_build"

# --- Build and install OpenHEVC into lib/openhevc ---
print_info "Building and installing OpenHEVC into ${OPENHEVC_INSTALL_PREFIX}..."
OPENHEVC_BUILD_DIR="${OPENHEVC_SRC_DIR}/build"
mkdir -p "${OPENHEVC_BUILD_DIR}"
(
    cd "${OPENHEVC_BUILD_DIR}"
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${OPENHEVC_INSTALL_PREFIX}" -DENABLE_EXECUTABLE=OFF ..
    make -j"$(nproc)"
    print_info "Installing OpenHEVC..."
    make install
)

# gst-openhevc includes <libopenhevc/openhevc.h>; upstream CMake only installs openHevcWrapper.h flat — copy API headers
OPENHEVC_INCLUDE_API="${OPENHEVC_SRC_DIR}/libopenhevc"
if [ -d "${OPENHEVC_INCLUDE_API}" ]; then
    print_info "Installing libopenhevc/*.h into ${OPENHEVC_INSTALL_PREFIX}/include/libopenhevc/"
    mkdir -p "${OPENHEVC_INSTALL_PREFIX}/include/libopenhevc"
    for f in "${OPENHEVC_INCLUDE_API}"/*.h; do
        [ -f "$f" ] || continue
        cp -f "$f" "${OPENHEVC_INSTALL_PREFIX}/include/libopenhevc/"
    done
fi

# OpenHEVC CMake does not install libopenhevc.pc; gst-openhevc requires it for meson dependency('libopenhevc')
OPENHEVC_PC_IN="${PATCHES_DIR}/libopenhevc.pc.in"
OPENHEVC_PC_DIR="${OPENHEVC_INSTALL_PREFIX}/lib/pkgconfig"
if [ -f "${OPENHEVC_PC_IN}" ]; then
    print_info "Installing libopenhevc.pc for pkg-config (gst-openhevc)"
    mkdir -p "${OPENHEVC_PC_DIR}"
    sed "s|@PREFIX@|${OPENHEVC_INSTALL_PREFIX}|g" "${OPENHEVC_PC_IN}" > "${OPENHEVC_PC_DIR}/libopenhevc.pc"
else
    print_warning "Missing ${OPENHEVC_PC_IN}; gst-openhevc meson may fail"
fi

export PKG_CONFIG_PATH="${OPENHEVC_INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
pkg-config --exists libopenhevc || print_error "libopenhevc not found after installing .pc; check libLibOpenHevcWrapper.so in lib/openhevc/lib"
print_info "pkg-config libopenhevc: $(pkg-config --modversion libopenhevc)"

# --- Build and install gst-openhevc into lib/openhevc ---
GST_OPENHEVC_BUILD_DIR="${GST_OPENHEVC_SRC_DIR}/build"
print_info "Building gst-openhevc..."
(
    cd "${GST_OPENHEVC_SRC_DIR}"
    export PKG_CONFIG_PATH="${OPENHEVC_INSTALL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    meson setup build --prefix="${OPENHEVC_INSTALL_PREFIX}"
    ninja -C build
    print_info "Installing gst-openhevc..."
    ninja -C build install
)

print_success "OpenHEVC and gst-openhevc installed into lib/openhevc."
print_info "Verify with: PKG_CONFIG_PATH=${OPENHEVC_INSTALL_PREFIX}/lib/pkgconfig pkg-config --modversion libopenhevc && gst-inspect-1.0 openhevcdec"
print_info "encoder_cli/run.sh will pick up this install automatically when present."
print_info "Cleaning temp..."
rm -rf "${TEMP_DIR}"
print_success "Done."
