#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.
#
# Lay down DeepStream SDK sources/includes when the .deb did not (or install predates verification).
# Requires the full SDK tarball from NGC (same version as the .deb, typically 7.1).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/../../yolo/common_paths.sh"

DEEPSTREAM_VERSION="${DEEPSTREAM_VERSION:-7.1}"
DEEPSTREAM_VERSIONED_DIR="/opt/nvidia/deepstream/deepstream-${DEEPSTREAM_VERSION}"
DEEPSTREAM_SYMLINK="/opt/nvidia/deepstream/deepstream"
HEADER="nvdsinfer_custom_impl.h"

usage() {
    cat <<'EOF'
Usage: install_deepstream_sdk_sources.sh [OPTIONS]

Extracts the DeepStream SDK .tbz2 into /opt/nvidia/deepstream so sources/includes
(e.g. nvdsinfer_custom_impl.h) exists for DeepStream-Yolo builds.

Set DEEPSTREAM_TARBALL to the .tbz2 path, or pass it as the first argument.

Example:
  sudo DEEPSTREAM_TARBALL="$HOME/Downloads/deepstream_sdk_v7.1.0_x86_64.tbz2" \
    ./scripts/setup/bin/install_deepstream_sdk_sources.sh
EOF
}

log() { echo "[install_deepstream_sdk_sources] $*"; }
die() { echo "[install_deepstream_sdk_sources] ERROR: $*" >&2; exit 1; }

require_root() {
    [ "$(id -u)" -eq 0 ] || die "Run with sudo"
}

default_tarball_path() {
    local arch
    arch="$(uname -m)"
    case "$arch" in
        x86_64)
            echo "${HOME}/Downloads/deepstream_sdk_v7.1.0_x86_64.tbz2"
            ;;
        aarch64)
            echo "${HOME}/Downloads/deepstream_sdk_v7.1.0_jetson.tbz2"
            ;;
        *)
            echo ""
            ;;
    esac
}

main() {
    local tarball="${DEEPSTREAM_TARBALL:-}"
    while [ $# -gt 0 ]; do
        case "$1" in
            -h|--help) usage; exit 0 ;;
            *)
                if [ -z "$tarball" ]; then
                    tarball="$1"
                else
                    die "Unexpected argument: $1"
                fi
                ;;
        esac
        shift
    done

    if [ -z "$tarball" ]; then
        tarball="$(default_tarball_path)"
    fi
    [ -n "$tarball" ] && [ -f "$tarball" ] || die "Set DEEPSTREAM_TARBALL or place SDK .tbz2 in ~/Downloads"

    require_root

    if [ -n "$(find_deepstream_includes_dir)" ]; then
        log "Dev header already present ($(find_deepstream_includes_dir)/$HEADER)"
        exit 0
    fi

    log "Extracting $tarball -> /opt/nvidia/deepstream"
    mkdir -p /opt/nvidia/deepstream
    tar -xjf "$tarball" -C /opt/nvidia/deepstream

    local install_sh="${DEEPSTREAM_VERSIONED_DIR}/install.sh"
    if [ ! -x "$install_sh" ]; then
        install_sh="$(find /opt/nvidia/deepstream -maxdepth 3 -name install.sh -path "*/deepstream-${DEEPSTREAM_VERSION}/*" 2>/dev/null | head -1)"
    fi
    if [ -n "$install_sh" ] && [ -x "$install_sh" ]; then
        log "Running $(dirname "$install_sh")/install.sh"
        (cd "$(dirname "$install_sh")" && ./install.sh)
    fi

    if [ -d "$DEEPSTREAM_VERSIONED_DIR" ] && [ ! -e "$DEEPSTREAM_SYMLINK" ]; then
        ln -sfn "$DEEPSTREAM_VERSIONED_DIR" "$DEEPSTREAM_SYMLINK"
    fi

    if [ -z "$(find_deepstream_includes_dir)" ]; then
        die "Still missing $HEADER after extracting tarball. Run ./scripts/setup/bin/diagnose_deepstream.sh"
    fi

    log "OK: $(find_deepstream_includes_dir)/$HEADER"
    log "Next: ./scripts/setup/bootstrap.sh yolo"
}

main "$@"
