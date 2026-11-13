#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." &> /dev/null && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/vcai_decoder/vcai_decoder"
APP_BIN="${BUILD_DIR}/sf_decoder_minimal_test_app"
PLUGIN_DIR="${BUILD_DIR}"

show_help() {
    cat <<EOF
Usage:
  $(basename "$0") --input <path/to/input.h265> [options]

Wrapper behavior:
  - Automatically sets --plugin-dir to:
      ${PLUGIN_DIR}
  - Forwards all other arguments to sf_decoder_minimal_test_app.

Forwarded options (from minimal app):
  -i, --input <PATH>         Input H.265 Annex-B file (required)
  -t, --tet-file <PATH>      Optional TET tile file (.tet)
  -d, --decoder <ELEMENT>    Decoder element (default: avdec_h265)
  -s, --sink <ELEMENT>       Sink element (default: fakesink)
  -q, --qp <0..63>           QP for TET tile selection (default: 27)
  -p, --plugin-dir <PATH>    Plugin directory (overridden by this wrapper)
  -h, --help                 Show app help

Examples:
  $(basename "$0") --input ../yolo/yolo_output_with_sei.h265
  $(basename "$0") --input ../yolo/yolo_output_with_sei.h265 \\
      --tet-file ../rtp_server/tiles/yuv_32x32_checkerboard.tet --qp 27
  $(basename "$0") --input ../yolo/yolo_output_with_sei.h265 --sink autovideosink
EOF
}

for arg in "$@"; do
    if [ "${arg}" = "--help" ] || [ "${arg}" = "-h" ]; then
        show_help
        exit 0
    fi
done

if [ ! -x "${APP_BIN}" ]; then
    echo "Minimal test app not found. Build first:"
    echo "  cd ${PROJECT_ROOT}/vcai_decoder && ./build.sh"
    exit 1
fi

"${APP_BIN}" --plugin-dir "${PLUGIN_DIR}" "$@"
