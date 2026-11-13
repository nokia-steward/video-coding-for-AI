#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.
#
# Host installer for NVIDIA GPU stack + DeepStream 7.1 (matches project containers and YOLO tooling).
#
# Requires: sudo, Ubuntu 22.04 on x86_64 or arm64 (other releases are best-effort with a warning).
#
# Typical usage (after downloading the DeepStream .deb from NVIDIA NGC):
#   export DEEPSTREAM_DEB="$HOME/Downloads/deepstream-7.1_7.1.0-1_amd64.deb"
#   sudo ./scripts/setup/bin/install_nvidia_deepstream.sh
#
# Environment variables (all optional unless noted):
#   DEEPSTREAM_DEB          Path to deepstream-7.1_*.{deb} (required to install DeepStream unless already present)
#   DEEPSTREAM_TARBALL      Path to deepstream_sdk_v7.1.0_*.tbz2 (alternative to .deb; runs bundled install.sh)
#   NGC_API_KEY             NVIDIA NGC API key to download the .deb when DEEPSTREAM_DEB is unset
#   CUDA_APT_PACKAGE        Default: cuda-toolkit-12-6 (DeepStream 7.1 / Ubuntu 22.04)
#   TENSORRT_LOCAL_DEB      Path to nv-tensorrt-local-repo-ubuntu2204-* .deb (TensorRT 10.3 for DS 7.1)
#   TENSORRT_VERSION        Default: 10.3.0.26-1+cuda12.5
#   SKIP_CUDA=1             Do not install CUDA toolkit
#   SKIP_TENSORRT=1         Do not install TensorRT packages
#   SKIP_DEEPSTREAM=1       Only apt deps / CUDA / TensorRT
#   DEPS_ONLY=1             Same as --deps-only (apt dependencies only)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/../../yolo/common_paths.sh"

DEEPSTREAM_VERSION="${DEEPSTREAM_VERSION:-7.1}"
DEEPSTREAM_VERSIONED_DIR="/opt/nvidia/deepstream/deepstream-${DEEPSTREAM_VERSION}"
DEEPSTREAM_SYMLINK="/opt/nvidia/deepstream/deepstream"
CUDA_APT_PACKAGE="${CUDA_APT_PACKAGE:-cuda-toolkit-12-6}"
TENSORRT_VERSION="${TENSORRT_VERSION:-10.3.0.26-1+cuda12.5}"
NGC_DS_DEB_X86="https://api.ngc.nvidia.com/v2/resources/nvidia/deepstream/versions/7.1/files/deepstream-7.1_7.1.0-1_amd64.deb"
NGC_DS_DEB_ARM="https://api.ngc.nvidia.com/v2/resources/nvidia/deepstream/versions/7.1/files/deepstream-7.1_7.1.0-1_arm64.deb"

DEPS_ONLY="${DEPS_ONLY:-0}"
SKIP_CUDA="${SKIP_CUDA:-0}"
SKIP_TENSORRT="${SKIP_TENSORRT:-0}"
SKIP_DEEPSTREAM="${SKIP_DEEPSTREAM:-0}"

log() { echo "[install_nvidia_deepstream] $*"; }
warn() { echo "[install_nvidia_deepstream] WARNING: $*" >&2; }
die() { echo "[install_nvidia_deepstream] ERROR: $*" >&2; exit 1; }

run_install_deepstream_sdk_sources() {
    if [ -n "$(find_deepstream_includes_dir)" ]; then
        return 0
    fi
    local tbz="${DEEPSTREAM_TARBALL:-}"
    if [ -z "$tbz" ]; then
        tbz="$(discover_deepstream_tarball)"
    fi
    if [ -n "$tbz" ] && [ -f "$tbz" ]; then
        log "Dev headers missing after .deb — merging SDK sources from $tbz"
        DEEPSTREAM_TARBALL="$tbz" bash "$SCRIPT_DIR/install_deepstream_sdk_sources.sh"
        return 0
    fi
    warn "DeepStream .deb did not provide sources/includes/nvdsinfer_custom_impl.h"
    echo "  Download deepstream_sdk_v7.1.0_*.tbz2 from NGC and run:"
    echo "  sudo DEEPSTREAM_TARBALL=/path/to/tbz2 ./scripts/setup/bin/install_deepstream_sdk_sources.sh"
    return 1
}

usage() {
    cat <<'EOF'
Usage: install_nvidia_deepstream.sh [OPTIONS]

Installs apt prerequisites, optionally CUDA 12.6, TensorRT 10.3 (DeepStream 7.1), and DeepStream SDK.

Options:
  --deps-only       Install only DeepStream/GStreamer apt dependencies
  --skip-cuda       Do not install CUDA toolkit
  --skip-tensorrt   Do not install TensorRT packages
  --skip-deepstream Skip DeepStream SDK install (verify/symlink only if already present)
  -h, --help        Show this help

Obtain DeepStream from NVIDIA NGC (login required):
  https://catalog.ngc.nvidia.com/orgs/nvidia/resources/deepstream

Set DEEPSTREAM_DEB to the downloaded .deb, or set NGC_API_KEY to download it automatically.

sudo note: export variables with sudo env, or rely on auto-detect in ~/Downloads:
  sudo env DEEPSTREAM_DEB="$HOME/Downloads/deepstream-7.1_7.1.0-1_amd64.deb" $0

TensorRT local-repo step is optional; use --skip-tensorrt (default: try only if .deb present in Downloads).
EOF
}

require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        die "Run as root (e.g. sudo $0)"
    fi
}

detect_ubuntu() {
    if [ ! -f /etc/os-release ]; then
        warn "Cannot read /etc/os-release; continuing anyway."
        return 0
    fi
    # shellcheck source=/dev/null
    source /etc/os-release
    if [ "${ID:-}" != "ubuntu" ]; then
        warn "This script is written for Ubuntu; detected ID=${ID:-unknown}."
        return 0
    fi
    case "${VERSION_ID:-}" in
        22.04) return 0 ;;
        "")
            warn "Ubuntu VERSION_ID is empty."
            ;;
        *)
            warn "DeepStream 7.1 is officially supported on Ubuntu 22.04; you have ${VERSION_ID}."
            ;;
    esac
}

machine_arch() {
    uname -m
}

# When invoked via sudo, HOME is often /root — also search the invoking user's Downloads.
candidate_download_dirs() {
    local dir home
    if [ -n "${DEEPSTREAM_DOWNLOAD_DIR:-}" ] && [ -d "${DEEPSTREAM_DOWNLOAD_DIR}" ]; then
        printf '%s\n' "${DEEPSTREAM_DOWNLOAD_DIR}"
    fi
    for home in "${HOME}"; do
        [ -n "$home" ] && [ -d "${home}/Downloads" ] && printf '%s\n' "${home}/Downloads"
    done
    if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
        home="$(getent passwd "$SUDO_USER" 2>/dev/null | cut -d: -f6 || true)"
        if [ -n "$home" ] && [ -d "${home}/Downloads" ]; then
            printf '%s\n' "${home}/Downloads"
        fi
    fi
}

discover_deepstream_deb() {
    local dir
    while IFS= read -r dir; do
        [ -n "$dir" ] || continue
        shopt -s nullglob
        local matches=( "$dir"/deepstream-7.1_*.deb "$dir"/deepstream_*.deb "$dir"/deepstream-*.deb )
        shopt -u nullglob
        if [ "${#matches[@]}" -gt 0 ]; then
            echo "${matches[0]}"
            return 0
        fi
    done < <(candidate_download_dirs)
    echo ""
}

discover_deepstream_tarball() {
    local arch dir
    arch="$(machine_arch)"
    while IFS= read -r dir; do
        [ -n "$dir" ] || continue
        shopt -s nullglob
        local matches=( "$dir"/deepstream_sdk_v7.1.0_*.tbz2 )
        shopt -u nullglob
        if [ "${#matches[@]}" -gt 0 ]; then
            for f in "${matches[@]}"; do
                case "$arch" in
                    x86_64)
                        [[ "$f" == *x86_64* ]] || continue
                        echo "$f"
                        return 0
                        ;;
                    aarch64)
                        [[ "$f" == *jetson* || "$f" == *arm64* ]] || continue
                        echo "$f"
                        return 0
                        ;;
                esac
            done
            echo "${matches[0]}"
            return 0
        fi
    done < <(candidate_download_dirs)
    echo ""
}

discover_tensorrt_local_deb() {
    local dir
    while IFS= read -r dir; do
        [ -n "$dir" ] || continue
        shopt -s nullglob
        local matches=( "$dir"/nv-tensorrt-local-repo-ubuntu2204*.deb )
        shopt -u nullglob
        if [ "${#matches[@]}" -gt 0 ]; then
            echo "${matches[0]}"
            return 0
        fi
    done < <(candidate_download_dirs)
    echo ""
}

install_apt_deps() {
    log "Installing apt packages (GStreamer, DeepStream prerequisites)..."
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends \
        wget \
        ca-certificates \
        gnupg \
        software-properties-common \
        build-essential \
        pkg-config \
        git \
        protobuf-compiler \
        libssl3 \
        libssl-dev \
        libgles2-mesa-dev \
        libgstreamer1.0-0 \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        gstreamer1.0-tools \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-libav \
        gstreamer1.0-plugins-base \
        libgstrtspserver-1.0-0 \
        libjansson4 \
        libyaml-cpp-dev \
        libjsoncpp-dev \
        libjson-glib-dev
}

check_nvidia_driver() {
    if command -v nvidia-smi >/dev/null 2>&1; then
        log "NVIDIA driver present:"
        nvidia-smi --query-gpu=driver_version,name --format=csv,noheader | head -n 3 || true
        return 0
    fi
    warn "nvidia-smi not found. Install a recent NVIDIA driver before using nvh265enc/nvvideoconvert."
    echo "  Example (Ubuntu): sudo ubuntu-drivers install"
    echo "  Or use NVIDIA's .run / network installer from https://www.nvidia.com/Download/index.aspx"
    return 1
}

install_cuda_toolkit() {
    if [ "$SKIP_CUDA" = "1" ]; then
        log "SKIP_CUDA=1 — skipping CUDA toolkit install."
        return 0
    fi
    if command -v nvcc >/dev/null 2>&1; then
        log "CUDA toolkit already available: $(nvcc --version | grep release | head -1)"
        return 0
    fi

    local arch
    arch="$(machine_arch)"
    if [ "$arch" = "aarch64" ]; then
        if [ -d /usr/local/cuda ] || command -v nvcc >/dev/null 2>&1; then
            log "CUDA already present on aarch64 — skipping desktop cuda-toolkit apt install."
            return 0
        fi
        warn "aarch64 without CUDA: install Jetson/L4T CUDA or SBSA toolkit per NVIDIA docs, then re-run."
        return 0
    fi
    if [ "$arch" != "x86_64" ]; then
        die "Unsupported architecture for CUDA apt repo: $arch"
    fi

    log "Installing NVIDIA CUDA apt repository (Ubuntu 22.04 keyring)..."
    local pin_url keyring_deb repo_base
    repo_base="https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204"
    pin_url="${repo_base}/x86_64/cuda-ubuntu2204.pin"
    keyring_deb="${repo_base}/x86_64/cuda-keyring_1.1-1_all.deb"

    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN

    wget -q "$pin_url" -O "$tmp/cuda.pin"
    mv "$tmp/cuda.pin" /etc/apt/preferences.d/cuda-repository-pin-600
    wget -q "$keyring_deb" -O "$tmp/cuda-keyring.deb"
    dpkg -i "$tmp/cuda-keyring.deb"
    apt-get update
    log "Installing ${CUDA_APT_PACKAGE}..."
    apt-get install -y --no-install-recommends "${CUDA_APT_PACKAGE}"
    local cuda_ver="${CUDA_APT_PACKAGE#cuda-toolkit-}"
    cuda_ver="$(echo "$cuda_ver" | tr '-' '.')"
    if [ ! -e /usr/local/cuda ] && [ -d "/usr/local/cuda-${cuda_ver}" ]; then
        ln -sfn "/usr/local/cuda-${cuda_ver}" /usr/local/cuda || true
    fi
    log "CUDA install finished."
}

install_tensorrt() {
    if [ "$SKIP_TENSORRT" = "1" ]; then
        log "SKIP_TENSORRT=1 — skipping TensorRT install."
        return 0
    fi
    if dpkg -l libnvinfer10 2>/dev/null | awk '{print $2,$3}' | grep -q "libnvinfer10 ${TENSORRT_VERSION}"; then
        log "TensorRT ${TENSORRT_VERSION} already installed."
        return 0
    fi

    local local_deb="${TENSORRT_LOCAL_DEB:-}"
    if [ -z "$local_deb" ]; then
        local_deb="$(discover_tensorrt_local_deb)"
    fi

    if [ -z "$local_deb" ] || [ ! -f "$local_deb" ]; then
        log "TensorRT local repo .deb not in Downloads (optional pre-step)."
        echo "  DeepStream 7.1 often pulls TensorRT via the DeepStream .deb dependencies."
        echo "  To pin TensorRT ${TENSORRT_VERSION} first, set TENSORRT_LOCAL_DEB=... or place"
        echo "  nv-tensorrt-local-repo-ubuntu2204-10.3.0-cuda-12.5_1.0-1_amd64.deb in ~/Downloads."
        return 0
    fi

    log "Installing TensorRT from local repository package: $local_deb"
    dpkg -i "$local_deb"
    local keyring_glob="/var/nv-tensorrt-local-repo-"*
    if compgen -G "${keyring_glob}/*-keyring.gpg" >/dev/null; then
        cp /var/nv-tensorrt-local-repo-*/*-keyring.gpg /usr/share/keyrings/ 2>/dev/null || true
    fi
    apt-get update
    apt-get install -y --no-install-recommends \
        libnvinfer-dev="${TENSORRT_VERSION}" \
        libnvinfer-plugin-dev="${TENSORRT_VERSION}" \
        libnvinfer-headers-dev="${TENSORRT_VERSION}" \
        libnvinfer-headers-plugin-dev="${TENSORRT_VERSION}" \
        libnvinfer-plugin10="${TENSORRT_VERSION}" \
        libnvinfer10="${TENSORRT_VERSION}" \
        libnvonnxparsers-dev="${TENSORRT_VERSION}" \
        libnvonnxparsers10="${TENSORRT_VERSION}" \
        libnvinfer-vc-plugin-dev="${TENSORRT_VERSION}" \
        libnvinfer-vc-plugin10="${TENSORRT_VERSION}" \
        python3-libnvinfer="${TENSORRT_VERSION}" || {
        warn "Some TensorRT packages failed to install at pinned version ${TENSORRT_VERSION}."
        echo "  Fix apt conflicts (often newer libnvinfer from CUDA repo) or install TensorRT manually."
        echo "  See NVIDIA DeepStream 7.1 installation guide and forums."
        return 1
    }
    log "TensorRT install finished."
}

download_deepstream_deb() {
    local api_key="${NGC_API_KEY:-}"
    [ -n "$api_key" ] || die "Set DEEPSTREAM_DEB to a local file or set NGC_API_KEY to download from NGC."

    local url dest arch
    arch="$(machine_arch)"
    case "$arch" in
        x86_64) url="$NGC_DS_DEB_X86" ;;
        aarch64) url="$NGC_DS_DEB_ARM" ;;
        *) die "Unsupported architecture for DeepStream deb: $arch" ;;
    esac
    dest="$(mktemp -d)/deepstream-7.1.deb"
    log "Downloading DeepStream .deb from NGC..."
    if ! wget --header="Authorization: Bearer ${api_key}" \
        --content-disposition "$url" -O "$dest"; then
        die "NGC download failed. Verify NGC_API_KEY and account access to DeepStream 7.1."
    fi
    echo "$dest"
}

install_deepstream_sdk() {
    if [ "$SKIP_DEEPSTREAM" = "1" ]; then
        log "SKIP_DEEPSTREAM=1 — skipping DeepStream SDK install."
        return 0
    fi

    if [ -n "$(find_deepstream_includes_dir)" ]; then
        log "DeepStream dev headers already present."
        return 0
    fi

    local deb="${DEEPSTREAM_DEB:-}"
    local tarball="${DEEPSTREAM_TARBALL:-}"

    if [ -z "$deb" ]; then
        deb="$(discover_deepstream_deb)"
        if [ -n "$deb" ]; then
            log "Auto-detected DeepStream .deb: $deb"
        fi
    fi
    if [ -z "$tarball" ]; then
        tarball="$(discover_deepstream_tarball)"
        if [ -n "$tarball" ]; then
            log "Auto-detected DeepStream SDK tarball: $tarball"
        fi
    fi

    if [ -z "$deb" ] && [ -z "$tarball" ]; then
        if [ -n "${NGC_API_KEY:-}" ]; then
            deb="$(download_deepstream_deb)"
        else
            die "DeepStream .deb not found.

Place deepstream-7.1_7.1.0-1_amd64.deb in ~/Downloads, or set DEEPSTREAM_DEB.

sudo does not keep your environment unless you pass it explicitly, e.g.:
  sudo env DEEPSTREAM_DEB=\"\$HOME/Downloads/deepstream-7.1_7.1.0-1_amd64.deb\" $0

Or install the .deb directly:
  cd ~/Downloads && sudo apt install ./deepstream-7.1_7.1.0-1_amd64.deb"
        fi
    fi

    local have_runtime=0
    if [ -n "$(detect_deepstream_path)" ]; then
        have_runtime=1
    fi

    if [ -n "$deb" ] && [ "$have_runtime" -eq 0 ]; then
        [ -f "$deb" ] || die "DEEPSTREAM_DEB not found: $deb"
        log "Installing DeepStream from .deb: $deb"
        # shellcheck disable=SC2086
        apt-get install -y -f || true
        apt-get install -y "$deb" || {
            warn "apt install of DeepStream .deb failed (often TensorRT/CUDA version mismatch)."
            echo "  Try: cd $(dirname "$deb") && sudo apt install ./$(basename "$deb")"
            echo "  See apt error above; you may need TENSORRT_LOCAL_DEB in ~/Downloads and re-run."
            return 1
        }
        have_runtime=1
    elif [ -n "$deb" ] && [ "$have_runtime" -eq 1 ]; then
        log "DeepStream runtime already present; skipping .deb (set FORCE_DEEPSTREAM_DEB=1 to reinstall)."
        if [ "${FORCE_DEEPSTREAM_DEB:-0}" = "1" ]; then
            apt-get install -y "$deb"
        fi
    fi

    if [ -z "$(find_deepstream_includes_dir)" ]; then
        if [ -n "$tarball" ] && [ -f "$tarball" ]; then
            DEEPSTREAM_TARBALL="$tarball" run_install_deepstream_sdk_sources || true
        elif [ "$have_runtime" -eq 1 ]; then
            run_install_deepstream_sdk_sources || true
        elif [ -n "$tarball" ]; then
            [ -f "$tarball" ] || die "DEEPSTREAM_TARBALL not found: $tarball"
            log "Installing DeepStream from tarball: $tarball"
            mkdir -p /opt/nvidia/deepstream
            tar -xjf "$tarball" -C /opt/nvidia/deepstream
            local install_sh="${DEEPSTREAM_VERSIONED_DIR}/install.sh"
            if [ ! -x "$install_sh" ]; then
                install_sh="$(find /opt/nvidia/deepstream -maxdepth 3 -name install.sh -path "*/deepstream-${DEEPSTREAM_VERSION}/*" | head -1)"
            fi
            [ -n "$install_sh" ] && [ -x "$install_sh" ] || die "Could not find deepstream-${DEEPSTREAM_VERSION}/install.sh after extracting tarball."
            (cd "$(dirname "$install_sh")" && ./install.sh)
        fi
    fi

    if [ -z "$(find_deepstream_includes_dir)" ] && [ -z "$(detect_deepstream_path)" ]; then
        die "DeepStream install incomplete. Run ./scripts/setup/bin/diagnose_deepstream.sh"
    fi
}

ensure_deepstream_symlink() {
    local root
    root="$(detect_deepstream_path)"
    if [ -z "$root" ] && [ -d "$DEEPSTREAM_VERSIONED_DIR" ]; then
        root="$DEEPSTREAM_VERSIONED_DIR"
    fi
    if [ -n "$root" ] && [ -d "$root" ] && [ ! -e "$DEEPSTREAM_SYMLINK" ]; then
        log "Creating symlink ${DEEPSTREAM_SYMLINK} -> ${root}"
        ln -sfn "$root" "$DEEPSTREAM_SYMLINK"
    fi
}

verify_installation() {
    local ok=0

    check_nvidia_driver || ok=1

    if command -v nvcc >/dev/null 2>&1; then
        log "nvcc: $(nvcc --version | grep release | head -1)"
    else
        warn "nvcc not in PATH (CUDA toolkit may still be present under /usr/local/cuda)."
        ok=1
    fi

    local ds_includes ds_root
    ds_includes="$(find_deepstream_includes_dir)"
    ds_root="$(detect_deepstream_path)"
    if [ -n "$ds_includes" ] && [ -f "$ds_includes/nvdsinfer_custom_impl.h" ]; then
        log "DeepStream dev header OK: $ds_includes/nvdsinfer_custom_impl.h"
    else
        warn "Missing nvdsinfer_custom_impl.h (expected under .../sources/includes/)"
        echo "  Run: ./scripts/setup/bin/diagnose_deepstream.sh"
        echo "  Then: sudo DEEPSTREAM_TARBALL=... ./scripts/setup/bin/install_deepstream_sdk_sources.sh"
        ok=1
    fi

    if [ -z "$ds_root" ]; then
        ds_root="$DEEPSTREAM_VERSIONED_DIR"
    fi
    ensure_deepstream_symlink

    local triplet
    triplet="$(detect_arch_triplet)"
    export LD_LIBRARY_PATH="${ds_root}/lib:${LD_LIBRARY_PATH:-}"
    export GST_PLUGIN_PATH="${ds_root}/lib/gst-plugins:/usr/lib/${triplet}/gstreamer-1.0/deepstream:${GST_PLUGIN_PATH:-}"

    for plugin in nvvideoconvert nvh265enc nvh265dec; do
        if gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
            log "GStreamer plugin OK: $plugin"
        else
            warn "GStreamer plugin not found: $plugin (check driver, DeepStream, GST_PLUGIN_PATH)"
            ok=1
        fi
    done

    if [ "$ok" -eq 0 ]; then
        log "Verification passed. Next: ./scripts/setup/bootstrap.sh core"
    else
        warn "Verification reported issues — see messages above."
        return 1
    fi
}

parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --deps-only) DEPS_ONLY=1 ;;
            --skip-cuda) SKIP_CUDA=1 ;;
            --skip-tensorrt) SKIP_TENSORRT=1 ;;
            --skip-deepstream) SKIP_DEEPSTREAM=1 ;;
            -h|--help) usage; exit 0 ;;
            *) die "Unknown option: $1 (use --help)" ;;
        esac
        shift
    done
}

main() {
    parse_args "$@"
    require_root
    detect_ubuntu

    install_apt_deps

    if [ "$DEPS_ONLY" = "1" ]; then
        log "--deps-only complete."
        exit 0
    fi

    check_nvidia_driver || true
    install_cuda_toolkit
    install_tensorrt || true
    install_deepstream_sdk
    ensure_deepstream_symlink
    verify_installation
}

main "$@"
