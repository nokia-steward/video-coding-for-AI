#!/bin/bash

# Shared path and environment helpers for YOLO/DeepStream scripts.
# This file is intended to be sourced by other scripts.
#
# Conventions:
# - No sudo inside scripts. If root actions are required, print instructions for the user.
# - Always treat VCAI repo root as the anchor for paths.

set -euo pipefail

get_vcai_root() {
    # Resolve the repo root by locating a directory that contains both encoder_cli/ and rtp_server/.
    local start_dir="${1:-}"
    local script_dir=""
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

    local search_dir="${start_dir:-$script_dir}"
    search_dir="$(cd "$search_dir" && pwd)"

    while [ "$search_dir" != "/" ]; do
        if [ -d "$search_dir/encoder_cli" ] && [ -d "$search_dir/rtp_server" ] && [ -d "$search_dir/scripts" ]; then
            echo "$search_dir"
            return 0
        fi
        search_dir="$(dirname "$search_dir")"
    done

    # Fallback: assume scripts/yolo is under repo root/scripts/yolo
    local fallback_root
    fallback_root="$(cd "$script_dir/../.." && pwd)"
    echo "$fallback_root"
}

detect_arch_triplet() {
    local arch
    arch="$(uname -m)"
    case "$arch" in
        x86_64) echo "x86_64-linux-gnu" ;;
        aarch64|arm64) echo "aarch64-linux-gnu" ;;
        *) echo "x86_64-linux-gnu" ;;
    esac
}

_deepstream_root_is_plausible() {
    local root="${1:-}"
    [ -n "$root" ] && [ -d "$root" ] || return 1
    [ -d "$root/lib" ] || return 1
    [ -f "$root/lib/libnvds_meta.so" ] ||
        [ -f "$root/lib/libnvds_infer.so" ] ||
        [ -d "$root/sources/includes" ] ||
        [ -d "$root/lib/gst-plugins" ]
}

_deepstream_roots_add() {
    local -n _out_refs=$1
    local candidate="${2:-}"
    local root="$candidate"
    [ -n "$root" ] || return 0
    if [ -L "$root" ]; then
        root="$(readlink -f "$root" 2>/dev/null || echo "$root")"
    fi
    _deepstream_root_is_plausible "$root" || return 0
    local existing
    for existing in "${_out_refs[@]}"; do
        if [ "$existing" = "$root" ]; then
            return 0
        fi
    done
    _out_refs+=("$root")
}

enumerate_deepstream_roots() {
    local -a roots=()
    local path pkg line header="nvdsinfer_custom_impl.h"

    shopt -s nullglob
    for path in /opt/nvidia/deepstream/deepstream-*; do
        _deepstream_roots_add roots "$path"
    done
    shopt -u nullglob

    if [ -e /opt/nvidia/deepstream/deepstream ]; then
        _deepstream_roots_add roots "/opt/nvidia/deepstream/deepstream"
    fi

    shopt -s nullglob
    for path in /opt/nvidia/deepstream-*; do
        _deepstream_roots_add roots "$path"
    done
    shopt -u nullglob

    # Legacy pinned paths (deb installs sometimes match only after dpkg/finds below).
    local legacy=(
        "/opt/nvidia/deepstream/deepstream-7.1"
        "/opt/nvidia/deepstream/deepstream-7.0"
        "/opt/nvidia/deepstream/deepstream-6.4"
    )
    for path in "${legacy[@]}"; do
        _deepstream_roots_add roots "$path"
    done

    if command -v dpkg-query >/dev/null 2>&1; then
        for pkg in deepstream-7.1 deepstream; do
            if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q 'install ok installed'; then
                continue
            fi
            while IFS= read -r line; do
                case "$line" in
                    */sources/includes/"$header")
                        _deepstream_roots_add roots "$(dirname "$(dirname "$line")")"
                        ;;
                    */lib/libnvds_*|*/lib/libnvdsgst_*|*/lib/libnvdsmeta.so)
                        _deepstream_roots_add roots "$(dirname "$(dirname "$line")")"
                        ;;
                esac
            done < <(dpkg-query -L "$pkg" 2>/dev/null || true)
        done
    fi

    while IFS= read -r path; do
        [ -n "$path" ] || continue
        _deepstream_roots_add roots "$(dirname "$(dirname "$path")")"
    done < <(
        find /opt/nvidia \( -name 'libnvds_meta.so' -o -name 'libnvds_infer.so' \) -print 2>/dev/null | head -20
    )

    local r
    for r in "${roots[@]}"; do
        printf '%s\n' "$r"
    done
}

detect_deepstream_path() {
    local preferred="" fallback="" root
    while IFS= read -r root; do
        [ -n "$root" ] || continue
        if [[ "$root" == *deepstream-7.1* ]]; then
            preferred="$root"
            break
        fi
        if [ -z "$fallback" ]; then
            fallback="$root"
        fi
    done < <(enumerate_deepstream_roots)

    if [ -n "$preferred" ]; then
        echo "$preferred"
        return 0
    fi
    if [ -n "$fallback" ]; then
        echo "$fallback"
        return 0
    fi
    echo ""
}

# Directory containing nvdsinfer_custom_impl.h (DeepStream SDK sources/includes).
find_deepstream_includes_dir() {
    local header="nvdsinfer_custom_impl.h"
    local root includes

    while IFS= read -r root; do
        includes="$root/sources/includes/$header"
        if [ -f "$includes" ]; then
            echo "$root/sources/includes"
            return 0
        fi
    done < <(enumerate_deepstream_roots)

    if command -v dpkg-query >/dev/null 2>&1; then
        for pkg in deepstream-7.1 deepstream; do
            if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q 'install ok installed'; then
                continue
            fi
            local header_path
            header_path="$(dpkg-query -L "$pkg" 2>/dev/null | grep -F "/sources/includes/$header" | head -1 || true)"
            if [ -n "$header_path" ] && [ -f "$header_path" ]; then
                dirname "$header_path"
                return 0
            fi
        done
    fi

    local found
    found="$(find /opt/nvidia -name "$header" -print -quit 2>/dev/null || true)"
    if [ -n "$found" ] && [ -f "$found" ]; then
        dirname "$found"
        return 0
    fi

    echo ""
}

detect_cuda_version_from_nvcc() {
    local nvcc_candidates=(
        "/usr/local/cuda/bin/nvcc"
        "/usr/local/cuda-12/bin/nvcc"
        "/usr/local/cuda-12.9/bin/nvcc"
        "/usr/local/cuda-12.8/bin/nvcc"
        "/usr/local/cuda-12.7/bin/nvcc"
        "/usr/local/cuda-12.6/bin/nvcc"
        "/usr/local/cuda-11/bin/nvcc"
        "/usr/local/cuda-11.8/bin/nvcc"
        "/opt/cuda/bin/nvcc"
    )

    if command -v nvcc >/dev/null 2>&1; then
        nvcc_candidates+=("$(command -v nvcc)")
    fi

    local nvcc_path=""
    for path in "${nvcc_candidates[@]}"; do
        if [ -n "$path" ] && [ -x "$path" ]; then
            nvcc_path="$path"
            break
        fi
    done

    if [ -z "$nvcc_path" ]; then
        echo ""
        return 0
    fi

    local cuda_version
    cuda_version="$("$nvcc_path" --version 2>/dev/null | grep "release" | sed -n 's/.*release \([0-9]\+\.[0-9]\+\).*/\1/p' || true)"
    echo "$cuda_version"
}

detect_cuda_version_from_dirs() {
    if [ -L "/usr/local/cuda" ]; then
        local target
        target="$(readlink "/usr/local/cuda" || true)"
        local version
        version="$(echo "$target" | sed -n 's/.*cuda-\([0-9]\+\.[0-9]\+\).*/\1/p' || true)"
        if [ -n "$version" ]; then
            echo "$version"
            return 0
        fi
    fi

    local candidates=(
        "/usr/local/cuda-12.9"
        "/usr/local/cuda-12.8"
        "/usr/local/cuda-12.7"
        "/usr/local/cuda-12.6"
        "/usr/local/cuda-12.5"
        "/usr/local/cuda-12.4"
        "/usr/local/cuda-12.3"
        "/usr/local/cuda-12.2"
        "/usr/local/cuda-12.1"
        "/usr/local/cuda-12.0"
        "/usr/local/cuda-12"
        "/usr/local/cuda-11.8"
        "/usr/local/cuda-11.7"
        "/usr/local/cuda-11.6"
        "/usr/local/cuda-11"
        "/usr/local/cuda"
        "/opt/cuda"
    )

    for dir in "${candidates[@]}"; do
        if [ -d "$dir" ]; then
            local version
            version="$(echo "$dir" | sed -n 's/.*cuda-\([0-9]\+\.[0-9]\+\).*/\1/p' || true)"
            if [ -n "$version" ]; then
                echo "$version"
                return 0
            fi
        fi
    done

    echo ""
}

get_cuda_arch_from_version() {
    local cuda_version="${1:-}"
    local major
    major="$(echo "$cuda_version" | cut -d. -f1 || true)"
    local minor
    minor="$(echo "$cuda_version" | cut -d. -f2 || true)"

    case "$major" in
        12) echo "8.6" ;;
        11)
            if [ -n "$minor" ] && [ "$minor" -ge 8 ]; then
                echo "8.6"
            else
                echo "8.0"
            fi
            ;;
        10) echo "7.5" ;;
        *) echo "7.5" ;;
    esac
}

detect_and_export_cuda_vars() {
    local cuda_version
    cuda_version="$(detect_cuda_version_from_nvcc)"
    if [ -z "$cuda_version" ]; then
        cuda_version="$(detect_cuda_version_from_dirs)"
    fi
    if [ -z "$cuda_version" ]; then
        cuda_version="12.0"
    fi

    export CUDA_VER="$cuda_version"
    export CUDA_ARCH_BIN
    CUDA_ARCH_BIN="$(get_cuda_arch_from_version "$cuda_version")"
    export CUDA_ARCH_BIN
}

