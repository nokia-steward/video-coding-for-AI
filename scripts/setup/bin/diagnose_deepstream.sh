#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.
#
# Read-only DeepStream layout report for bootstrap / YOLO troubleshooting.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/../../yolo/common_paths.sh"

HEADER="nvdsinfer_custom_impl.h"

echo "=== DeepStream diagnosis ==="
echo

echo "## dpkg packages"
if command -v dpkg-query >/dev/null 2>&1; then
    dpkg -l 'deepstream*' 2>/dev/null || echo "(no deepstream* packages)"
else
    echo "dpkg-query not available"
fi
echo

echo "## /opt/nvidia layout"
if [ -d /opt/nvidia ]; then
    ls -la /opt/nvidia 2>/dev/null || true
    if [ -d /opt/nvidia/deepstream ]; then
        echo
        ls -la /opt/nvidia/deepstream 2>/dev/null || true
    fi
else
    echo "/opt/nvidia does not exist"
fi
echo

echo "## Detected install roots (enumerate_deepstream_roots)"
while IFS= read -r root; do
    [ -n "$root" ] || continue
    echo "  - $root"
    ls -la "$root/lib/libnvds_meta.so" 2>/dev/null || echo "    (no libnvds_meta.so)"
    ls -la "$root/sources/includes/$HEADER" 2>/dev/null || echo "    (no sources/includes/$HEADER)"
done < <(enumerate_deepstream_roots)
echo

echo "## Resolved paths"
echo "detect_deepstream_path: $(detect_deepstream_path || echo '<none>')"
echo "find_deepstream_includes_dir: $(find_deepstream_includes_dir || echo '<none>')"
echo

found="$(find /opt/nvidia -name "$HEADER" -print 2>/dev/null || true)"
if [ -n "$found" ]; then
    echo "## Header search under /opt/nvidia"
    echo "$found"
else
    echo "## Header search under /opt/nvidia"
    echo "(not found)"
fi
echo

ds_pkg_count=0
if command -v dpkg-query >/dev/null 2>&1; then
    ds_pkg_count="$(dpkg-query -W -f='${Package}\n' 'deepstream*' 2>/dev/null | wc -l | tr -d ' ')"
fi
ds_root="$(detect_deepstream_path)"
ds_includes="$(find_deepstream_includes_dir)"

find_installer_artifacts() {
    local dir="${1:-$HOME/Downloads}"
    shopt -s nullglob
    local debs=( "$dir"/deepstream-7.1_*.deb "$dir"/deepstream*.deb )
    local tbz2s=( "$dir"/deepstream_sdk_v7.1.0_*.tbz2 )
    shopt -u nullglob
    if [ "${#debs[@]}" -gt 0 ]; then
        echo "deb: ${debs[0]}"
    fi
    if [ "${#tbz2s[@]}" -gt 0 ]; then
        echo "tbz2: ${tbz2s[0]}"
    fi
}

echo "## Verdict"
if [ "$ds_pkg_count" = "0" ] && [ -z "$ds_root" ]; then
    echo "DeepStream is NOT installed on this host."
    echo "  - No deepstream* apt/dpkg packages"
    echo "  - No /opt/nvidia/deepstream/ tree (only other NVIDIA tools may appear under /opt/nvidia)"
    echo ""
    echo "Installing only CUDA / Nsight does not install DeepStream."
elif [ -n "$ds_root" ] && [ -z "$ds_includes" ]; then
    echo "DeepStream runtime appears present but dev headers are missing."
else
    echo "DeepStream SDK with dev headers looks OK."
fi
echo

echo "## Install artifacts in ~/Downloads (if present)"
artifacts="$(find_installer_artifacts "$HOME/Downloads" || true)"
if [ -n "$artifacts" ]; then
    echo "$artifacts"
else
    echo "(none — download from https://catalog.ngc.nvidia.com/orgs/nvidia/resources/deepstream)"
fi
echo

echo "## Recommended next steps"
if [ "$ds_pkg_count" = "0" ] && [ -z "$ds_root" ]; then
    cat <<'EOF'
1. Download from NGC (DeepStream 7.1):
   - deepstream-7.1_7.1.0-1_amd64.deb  (dGPU)
   - deepstream_sdk_v7.1.0_x86_64.tbz2  (optional; ensures sources/includes)

2. Install the .deb (must register with dpkg):
   cd ~/Downloads
   sudo apt install ./deepstream-7.1_7.1.0-1_amd64.deb

   Or from repo root (also installs apt prereqs):
   export DEEPSTREAM_DEB="$HOME/Downloads/deepstream-7.1_7.1.0-1_amd64.deb"
   sudo ./scripts/setup/bin/install_nvidia_deepstream.sh

3. Verify:
   dpkg -l 'deepstream*'
   ls /opt/nvidia/deepstream
   ./scripts/setup/bin/diagnose_deepstream.sh

4. If headers still missing, add the SDK tarball:
   sudo DEEPSTREAM_TARBALL="$HOME/Downloads/deepstream_sdk_v7.1.0_x86_64.tbz2" \
     ./scripts/setup/bin/install_deepstream_sdk_sources.sh

5. ./scripts/setup/bootstrap.sh core
EOF
elif [ -n "$ds_root" ] && [ -z "$ds_includes" ]; then
    echo "Extract SDK sources (.tbz2 from NGC):"
    echo "  sudo DEEPSTREAM_TARBALL=\"\$HOME/Downloads/deepstream_sdk_v7.1.0_x86_64.tbz2\" \\"
    echo "    ./scripts/setup/bin/install_deepstream_sdk_sources.sh"
    echo "  ./scripts/setup/bootstrap.sh core"
else
    echo "  ./scripts/setup/bootstrap.sh core"
fi
