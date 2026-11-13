#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Run gst-inspect-1.0 with GStreamer and local plugin paths.
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
RTP_SERVER_DIR="$(dirname "${SCRIPT_DIR}")"
PROJECT_ROOT="$(dirname "${RTP_SERVER_DIR}")"

ARCH_TRIPLET="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo "x86_64-linux-gnu")"
VCAI_DECODER_PLUGIN_PATH_1="${PROJECT_ROOT}/build/rtp_server/vcai_decoder"
VCAI_DECODER_PLUGIN_PATH_2="${PROJECT_ROOT}/build/vcai_decoder/vcai_decoder"
OPENHEVC_PLUGIN_PATH="${PROJECT_ROOT}/lib/openhevc/lib/gstreamer-1.0"
OPENHEVC_LIB_PATH="${PROJECT_ROOT}/lib/openhevc/lib"

# DeepStream plugins are typically installed in the system plugin directory under a 'deepstream' subfolder.
DEEPSTREAM_SYSTEM_PLUGIN_PATH="/usr/lib/${ARCH_TRIPLET}/gstreamer-1.0/deepstream"

# DeepStream library locations (use whichever exists).
DEEPSTREAM_SYMLINK_DIR="/opt/nvidia/deepstream/deepstream"
DEEPSTREAM_VERSIONED_DIR="/opt/nvidia/deepstream/deepstream-7.1"

gst_plugin_paths=()
if [ -d "${VCAI_DECODER_PLUGIN_PATH_1}" ]; then
    gst_plugin_paths+=("${VCAI_DECODER_PLUGIN_PATH_1}")
fi
if [ -d "${VCAI_DECODER_PLUGIN_PATH_2}" ]; then
    gst_plugin_paths+=("${VCAI_DECODER_PLUGIN_PATH_2}")
fi
if [ -d "${OPENHEVC_PLUGIN_PATH}" ]; then
    gst_plugin_paths+=("${OPENHEVC_PLUGIN_PATH}")
fi
if [ -d "${DEEPSTREAM_SYSTEM_PLUGIN_PATH}" ]; then
    gst_plugin_paths+=("${DEEPSTREAM_SYSTEM_PLUGIN_PATH}")
fi
if [ -d "${DEEPSTREAM_SYMLINK_DIR}/lib/gstreamer-1.0" ]; then
    gst_plugin_paths+=("${DEEPSTREAM_SYMLINK_DIR}/lib/gstreamer-1.0")
fi
if [ -d "${DEEPSTREAM_VERSIONED_DIR}/lib/gstreamer-1.0" ]; then
    gst_plugin_paths+=("${DEEPSTREAM_VERSIONED_DIR}/lib/gstreamer-1.0")
fi

ld_library_paths=()
if [ -d "${OPENHEVC_LIB_PATH}" ]; then
    ld_library_paths+=("${OPENHEVC_LIB_PATH}")
fi
if [ -d "${DEEPSTREAM_SYMLINK_DIR}/lib" ]; then
    ld_library_paths+=("${DEEPSTREAM_SYMLINK_DIR}/lib")
fi
if [ -d "${DEEPSTREAM_VERSIONED_DIR}/lib" ]; then
    ld_library_paths+=("${DEEPSTREAM_VERSIONED_DIR}/lib")
fi

if [ ${#gst_plugin_paths[@]} -gt 0 ]; then
    export GST_PLUGIN_PATH="$(IFS=:; echo "${gst_plugin_paths[*]}")"
fi
if [ ${#ld_library_paths[@]} -gt 0 ]; then
    export LD_LIBRARY_PATH="$(IFS=:; echo "${ld_library_paths[*]}")"
fi
unset GST_PLUGIN_SCANNER

exec gst-inspect-1.0 "$@"
