#!/bin/bash
# Sourced by setup_yolo.sh — pin TensorRT *headers* to match libnvinfer10 (DeepStream runtime).
# System libnvinfer-headers-dev 11.x breaks DeepStream-Yolo (kEXPLICIT_BATCH, BuilderFlag, etc.).

set -euo pipefail

_yolo_tensorrt_headers_need_pin() {
    if ! command -v dpkg-query >/dev/null 2>&1; then
        return 0
    fi
    if [ ! -f /usr/include/x86_64-linux-gnu/NvInferVersion.h ]; then
        return 0
    fi
    local sys_major
    sys_major="$(grep -E '^#define NV_TENSORRT_MAJOR[[:space:]]+' /usr/include/x86_64-linux-gnu/NvInferVersion.h \
        | sed -n 's/.*NV_TENSORRT_MAJOR[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -1)"
    [ -n "$sys_major" ] || return 0
    [ "$sys_major" -eq 10 ] && return 1
    return 0
}

_yolo_tensorrt_headers_target_version() {
    if command -v dpkg-query >/dev/null 2>&1 \
        && dpkg-query -W -f='${Status}' libnvinfer10 2>/dev/null | grep -q 'install ok installed'; then
        dpkg-query -W -f='${Version}' libnvinfer10
        return 0
    fi
    echo "10.4.0.26-1+cuda12.6"
}

ensure_tensorrt_headers_for_yolo() {
    local vcai_root="${1:?}"
    local triplet
    triplet="$(detect_arch_triplet)"
    local cache="${vcai_root}/lib/tensorrt-headers"
    local inc_dir="${cache}/${triplet}"

    if ! _yolo_tensorrt_headers_need_pin; then
        echo "Using system TensorRT 10 headers under /usr/include/${triplet}"
        YOLO_TRT_INC_FLAGS="-I/usr/include/${triplet} -I/usr/include"
        YOLO_TRT_LIB_FLAGS="-L/usr/lib/${triplet} -lnvinfer_plugin -lnvonnxparser -Wl,--no-as-needed /usr/lib/${triplet}/libnvinfer.so.10"
        return 0
    fi

    local want_ver
    want_ver="$(_yolo_tensorrt_headers_target_version)"

    if [ -f "${cache}/.installed_version" ] \
        && [ "$(cat "${cache}/.installed_version")" = "$want_ver" ] \
        && [ -f "${inc_dir}/NvInferVersion.h" ]; then
        echo "Using cached TensorRT headers ${want_ver} in ${cache}"
        YOLO_TRT_INC_FLAGS="-I${inc_dir} -I${cache}"
        YOLO_TRT_LIB_FLAGS="-L/usr/lib/${triplet} -lnvinfer_plugin -lnvonnxparser -Wl,--no-as-needed /usr/lib/${triplet}/libnvinfer.so.10"
        return 0
    fi

    echo "TensorRT headers are not 10.x (DeepStream-Yolo needs TRT 10 API). Fetching headers ${want_ver} into ${cache}..."

    if ! command -v apt-get >/dev/null 2>&1 || ! command -v dpkg-deb >/dev/null 2>&1; then
        echo "ERROR: apt-get and dpkg-deb required to download TensorRT header packages." >&2
        exit 1
    fi

    mkdir -p "${cache}/dl"
    local dl="${cache}/dl"
    rm -f "${dl}/"*.deb
    _download_trt_header_debs() {
        local ver="$1"
        (cd "${dl}" && apt-get download \
            "libnvinfer-headers-dev=${ver}" \
            "libnvinfer-headers-plugin-dev=${ver}" \
            "libnvonnxparsers-dev=${ver}")
    }
    if ! _download_trt_header_debs "$want_ver"; then
        echo "WARN: Could not download headers for ${want_ver}; trying DeepStream 7.1 default 10.4.0.26-1+cuda12.6" >&2
        want_ver="10.4.0.26-1+cuda12.6"
        _download_trt_header_debs "$want_ver" || {
            echo "ERROR: Failed to download TensorRT header debs. Install libnvinfer-headers-dev 10.x, or fix apt/cuda repo." >&2
            exit 1
        }
    fi

    local staging="${cache}/.staging"
    rm -rf "${staging}"
    mkdir -p "${staging}"
    shopt -s nullglob
    local deb
    for deb in "${dl}/"*.deb; do
        dpkg-deb -x "$deb" "${staging}"
    done
    shopt -u nullglob

    rm -rf "${cache:?}/${triplet}" "${cache}/include" 2>/dev/null || true
    cp -a "${staging}/usr/include/"* "${cache}/"
    rm -rf "${staging}"
    echo "${want_ver}" > "${cache}/.installed_version"

    echo "Cached TensorRT headers at ${inc_dir}"
    YOLO_TRT_INC_FLAGS="-I${inc_dir} -I${cache}"
    YOLO_TRT_LIB_FLAGS="-L/usr/lib/${triplet} -lnvinfer_plugin -lnvonnxparser -Wl,--no-as-needed /usr/lib/${triplet}/libnvinfer.so.10"
}

apply_yolo_tensorrt_makefile_pins() {
    local makefile="${1:?}"
    # shellcheck disable=SC2154
    local trt_inc="${YOLO_TRT_INC_FLAGS:?}"
    local trt_libs="${YOLO_TRT_LIB_FLAGS:?}"

    if grep -q '# VCAI_TENSORRT_PIN' "$makefile"; then
        sed -i '/^# VCAI_TENSORRT_PIN$/d' "$makefile"
        sed -i '/^TRT_INC :=/d' "$makefile"
        sed -i '/^TRT_LIBS :=/d' "$makefile"
    fi

    sed -i "/^CFLAGS:=/a\\
# VCAI_TENSORRT_PIN\\
TRT_INC := ${trt_inc}\\
TRT_LIBS := ${trt_libs}" "$makefile"

    if ! grep -q '\$(TRT_INC)' "$makefile"; then
        sed -i 's|CFLAGS+= -I|CFLAGS+= $(TRT_INC) -I|' "$makefile"
        sed -i 's|CUFLAGS:= -I|CUFLAGS:= $(TRT_INC) -I|' "$makefile"
    fi

    if grep -q '\-lnvinfer_plugin -lnvinfer -lnvonnxparser' "$makefile"; then
        sed -i 's|-lnvinfer_plugin -lnvinfer -lnvonnxparser|$(TRT_LIBS)|' "$makefile"
    fi
}
