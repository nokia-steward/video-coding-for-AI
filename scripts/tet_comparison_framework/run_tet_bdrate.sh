#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# ---------------------------------------------------------------------------
# run_tet_bdrate.sh — Multi-QP BD-rate evaluation for TET
#
# Runs TET-on vs TET-off at multiple constant-QP points and produces a CSV
# suitable for BD-rate computation via
# scripts/tet_comparison_framework/tools/compute_bdrate.py.
#
# For each QP value the script:
#   1. Starts rtp_server with TET disabled (TET-off anchor)
#   2. Encodes the input video at constant QP via --encoder-qp, using COCO
#      groundtruth bounding boxes as ROIs (--roi-groundtruth-file) to
#      isolate encoding effects from detection variability
#   3. Waits for the test-mode JSON result
#   4. Measures the actual encoded bitrate from the H.265 output file
#   5. Repeats steps 1-4 with TET enabled (TET-on test)
#
# The --groundtruth COCO JSON file serves two purposes:
#   - rtp_server uses it to evaluate detection accuracy (test mode)
#   - encoder_cli uses it as the ROI source (groundtruth detection mode)
#
# At the end, an aggregation step collects per-case JSON results and bitrates
# into a single CSV (bdrate_input.csv) that compute_bdrate.py can consume.
#
# If --input does not exist, the script attempts PandaSet YUV generation via
# scripts/tet_comparison_framework/tools/generate_pandaset_front_camera_yuv.py
# using --dataset-dir (or a
# fallback dataset-root inference from --groundtruth/--input paths).
# ---------------------------------------------------------------------------

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RTP_SERVER_DIR="${ROOT_DIR}/rtp_server"
ENCODER_DIR="${ROOT_DIR}/encoder_cli"

DEFAULT_GROUNDTRUTH="${ROOT_DIR}/Traffic_2560x1600_30_val/annotations/Traffic_2560x1600_30_val.json"
DEFAULT_TILE="${RTP_SERVER_DIR}/tiles/yuv_32x32_checkerboard.tet"
DEFAULT_OUTDIR="${ROOT_DIR}/tet_bdrate_results_$(date +%Y%m%d_%H%M%S)"

# QP values to test — covering the range from low to high compression.
# These match the six QP values the TET was trained on.
QP_VALUES="${QP_VALUES:-22 27 32 37 42 47}"

INPUT_FILE=""
GROUNDTRUTH_FILE="${DEFAULT_GROUNDTRUTH}"
TILE_FILE="${DEFAULT_TILE}"
OUTPUT_DIR="${DEFAULT_OUTDIR}"
DATASET_DIR=""

SLEEP_BEFORE_ENCODER=5
SERVER_SHUTDOWN_TIMEOUT=30
# Time (seconds) to wait for the server to complete inference on all buffered
# frames AND write the JSON summary file.  With YOLOv11x (~1-2 s per frame)
# and 33 frames, plus the 30 s session-monitor inactivity timeout, 180 s is a
# generous upper bound.
RESULT_WAIT_TIMEOUT=180

usage() {
    cat << 'EOF'
Usage:
  ./scripts/tet_comparison_framework/run_tet_bdrate.sh [options]

Description:
  Multi-QP BD-rate evaluation for TET (Tile Enhancement Tiles).
  Runs TET-on vs TET-off at multiple constant-QP points and produces a CSV
  for BD-rate computation.

Options:
  --input PATH         Path to input YUV file. If omitted and PandaSet
                       auto-generation is needed, uses
                       pandaset_<W>x<H>_<FPS>.yuv under dataset root.
  --groundtruth PATH   Path to COCO groundtruth JSON (used as both the ROI
                       source for encoder_cli and the evaluation reference
                       for rtp_server test mode)
  --dataset-dir PATH   Optional PandaSet dataset root used for input-YUV
                       auto-generation when --input is missing
  --tile PATH          Path to .tet tile file
  --outdir PATH        Output directory for logs/results
  --qp-values "LIST"   Space-separated QP values (default: "22 27 32 37 42 47")
  --sleep SECONDS      Delay before starting encoder (default: 5)
  --shutdown SECONDS   Max seconds to wait for rtp_server shutdown (default: 30)
  --result-wait SECS   Max seconds to wait for JSON result file (default: 180)
  -h, --help           Show this help

Environment Variables:
  QP_VALUES            Space-separated QP values (overridden by --qp-values)

Output Files:
  <outdir>/bdrate_input.csv              Combined CSV for BD-rate computation
  <outdir>/qp<N>_tet_off/               Per-case directory (TET disabled)
  <outdir>/qp<N>_tet_on/                Per-case directory (TET enabled)
  <outdir>/qp<N>_tet_*/bitrate_bps.txt  Measured encoded bitrate
  <outdir>/qp<N>_tet_*/test_mode_*.json Detection metric results

Examples:
  # Run with default QP values (22 27 32 37 42 47)
  ./scripts/tet_comparison_framework/run_tet_bdrate.sh --input video.yuv --groundtruth gt.json --tile tiles/yuv.tet

  # Run with custom QP values
  ./scripts/tet_comparison_framework/run_tet_bdrate.sh --qp-values "27 32 37 42" --input video.yuv --groundtruth gt.json --tile tiles/yuv.tet

  # PandaSet COCO workflow (auto-generate YUV if missing)
  ./scripts/tet_comparison_framework/run_tet_bdrate.sh \
      --input PandaSet/pandaset_1920x1080_30.yuv \
      --groundtruth PandaSet/annotations/pandaset_1920x1080_30_coco5.json \
      --dataset-dir PandaSet

  # PandaSet workflow with auto-generated default YUV name
  ./scripts/tet_comparison_framework/run_tet_bdrate.sh \
      --groundtruth PandaSet/annotations/pandaset_1920x1080_30_coco5.json \
      --dataset-dir PandaSet

  # Compute BD-rate from results
  python3 scripts/tet_comparison_framework/tools/compute_bdrate.py <outdir>/bdrate_input.csv --metric f1_score
  python3 scripts/tet_comparison_framework/tools/compute_bdrate.py <outdir>/bdrate_input.csv --metric mean_iou
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --input)         INPUT_FILE="$2";           shift 2 ;;
        --groundtruth)   GROUNDTRUTH_FILE="$2";     shift 2 ;;
        --dataset-dir)   DATASET_DIR="$2";          shift 2 ;;
        --tile)          TILE_FILE="$2";             shift 2 ;;
        --outdir)        OUTPUT_DIR="$2";            shift 2 ;;
        --qp-values)     QP_VALUES="$2";            shift 2 ;;
        --sleep)         SLEEP_BEFORE_ENCODER="$2";  shift 2 ;;
        --shutdown)      SERVER_SHUTDOWN_TIMEOUT="$2"; shift 2 ;;
        --result-wait)   RESULT_WAIT_TIMEOUT="$2";   shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        *)               echo "Unknown argument: $1"; usage; exit 1 ;;
    esac
done

resolve_path() {
    local candidate="$1"
    [ -z "$candidate" ] && { echo ""; return; }
    [[ "$candidate" == /* ]] && { echo "$candidate"; return; }
    [ -e "${ROOT_DIR}/${candidate}" ] && { echo "${ROOT_DIR}/${candidate}"; return; }
    echo "$candidate"
}

INPUT_FILE="$(resolve_path "$INPUT_FILE")"
GROUNDTRUTH_FILE="$(resolve_path "$GROUNDTRUTH_FILE")"
TILE_FILE="$(resolve_path "$TILE_FILE")"
OUTPUT_DIR="$(resolve_path "$OUTPUT_DIR")"
DATASET_DIR="$(resolve_path "$DATASET_DIR")"

validate_pandaset_root_markers() {
    local candidate="$1"
    [ -z "$candidate" ] && return 1
    [ -d "${candidate}/camera/front_camera" ] || return 1
    [ -d "${candidate}/annotations/cuboids" ] || return 1
    return 0
}

infer_dataset_root_from_groundtruth() {
    local gt_path="$1"
    local gt_dir
    gt_dir="$(dirname "$gt_path")"
    if [ "$(basename "$gt_dir")" = "annotations" ]; then
        dirname "$gt_dir"
    else
        echo "$gt_dir"
    fi
}

infer_dataset_root_from_input() {
    local input_path="$1"
    dirname "$input_path"
}

resolve_pandaset_dataset_root() {
    local explicit_dir="$1"
    local input_path="$2"
    local groundtruth_path="$3"

    if [ -n "$explicit_dir" ]; then
        if validate_pandaset_root_markers "$explicit_dir"; then
            echo "$explicit_dir"
            return 0
        fi
        echo "ERROR: --dataset-dir does not look like PandaSet root: $explicit_dir" >&2
        return 1
    fi

    local inferred_from_groundtruth
    inferred_from_groundtruth="$(infer_dataset_root_from_groundtruth "$groundtruth_path")"
    if validate_pandaset_root_markers "$inferred_from_groundtruth"; then
        echo "$inferred_from_groundtruth"
        return 0
    fi

    local inferred_from_input
    inferred_from_input="$(infer_dataset_root_from_input "$input_path")"
    if validate_pandaset_root_markers "$inferred_from_input"; then
        echo "$inferred_from_input"
        return 0
    fi

    echo "ERROR: Could not resolve PandaSet dataset root. Provide --dataset-dir explicitly." >&2
    return 1
}

ensure_input_yuv_exists_or_generate() {
    local input_path="$1"
    local explicit_dataset_dir="$2"
    local groundtruth_path="$3"

    if [ -n "$input_path" ] && [ -f "$input_path" ]; then
        INPUT_FILE="$input_path"
        return 0
    fi

    if [ -n "$input_path" ]; then
        echo "Input YUV missing: $input_path"
    else
        echo "Input YUV not provided."
    fi
    echo "Attempting PandaSet YUV generation..."

    local dataset_root
    dataset_root="$(resolve_pandaset_dataset_root "$explicit_dataset_dir" "$input_path" "$groundtruth_path")" || return 1

    local generation_log
    generation_log="$(python3 "${ROOT_DIR}/scripts/tet_comparison_framework/tools/generate_pandaset_front_camera_yuv.py" \
        --dataset-dir "$dataset_root" 2>&1)" || {
        echo "$generation_log"
        return 1
    }
    echo "$generation_log"

    local generated_output_path=""
    while IFS= read -r line; do
        case "$line" in
            "  Output: "*) generated_output_path="${line#  Output: }" ;;
        esac
    done <<< "$generation_log"

    if [ -z "$generated_output_path" ]; then
        echo "ERROR: Could not parse generated YUV path from generator output." >&2
        return 1
    fi
    generated_output_path="$(resolve_path "$generated_output_path")"
    if [ ! -f "$generated_output_path" ]; then
        echo "ERROR: YUV generation finished but output is still missing: $generated_output_path" >&2
        return 1
    fi

    if [ -z "$input_path" ]; then
        INPUT_FILE="$generated_output_path"
        echo "Using generated input YUV: $INPUT_FILE"
        return 0
    fi

    local requested_dir
    local requested_base
    local requested_prefix
    local generated_suffix
    local canonical_input_path
    requested_dir="$(dirname "$input_path")"
    requested_base="$(basename "$input_path")"
    requested_prefix="$(derive_input_prefix_for_generated_yuv "$requested_base")"
    generated_suffix="$(extract_resolution_fps_suffix_from_yuv "$(basename "$generated_output_path")")" || {
        echo "ERROR: Generated YUV name does not contain expected _<W>x<H>_<FPS>.yuv suffix: $generated_output_path" >&2
        return 1
    }
    canonical_input_path="${requested_dir}/${requested_prefix}_${generated_suffix}.yuv"
    mkdir -p "$requested_dir"

    if [ "$canonical_input_path" != "$generated_output_path" ]; then
        ln -sfn "$generated_output_path" "$canonical_input_path"
        echo "Created canonical input alias: $canonical_input_path -> $generated_output_path"
    fi

    INPUT_FILE="$canonical_input_path"
    echo "Using generated input YUV: $INPUT_FILE"
}

derive_input_prefix_for_generated_yuv() {
    local requested_basename="$1"
    local stem="${requested_basename%.*}"

    if [[ "$stem" =~ ^(.+)_([0-9]+)x([0-9]+)(_([0-9]+))?$ ]]; then
        stem="${BASH_REMATCH[1]}"
    elif [[ "$stem" =~ ^(.+)_([0-9]+)_([0-9]+)(_([0-9]+))?$ ]]; then
        stem="${BASH_REMATCH[1]}"
    fi

    while [[ "$stem" == *_ ]]; do
        stem="${stem%_}"
    done

    if [ -z "$stem" ]; then
        echo "pandaset"
    else
        echo "$stem"
    fi
}

extract_resolution_fps_suffix_from_yuv() {
    local yuv_basename="$1"
    if [[ "$yuv_basename" =~ _([0-9]+x[0-9]+_[0-9]+)\.yuv$ ]]; then
        echo "${BASH_REMATCH[1]}"
        return 0
    fi
    return 1
}

normalize_input_filename_for_encoder_cli() {
    local input_path="$1"
    local input_dir
    local input_base
    input_dir="$(dirname "$input_path")"
    input_base="$(basename "$input_path")"

    # encoder_cli expects NAME_WIDTHxHEIGHT[_FPS].ext
    if [[ "$input_base" =~ _[0-9]+x[0-9]+(_[0-9]+)?\.yuv$ ]]; then
        echo "$input_path"
        return 0
    fi

    # Common typo variant: NAME_WIDTH_HEIGHT_FPS.yuv -> NAME_WIDTHxHEIGHT_FPS.yuv
    if [[ "$input_base" =~ ^(.+)_([0-9]+)_([0-9]+)_([0-9]+)\.yuv$ ]]; then
        local prefix="${BASH_REMATCH[1]}"
        local width="${BASH_REMATCH[2]}"
        local height="${BASH_REMATCH[3]}"
        local fps="${BASH_REMATCH[4]}"
        local normalized_path="${input_dir}/${prefix}_${width}x${height}_${fps}.yuv"
        if [ ! -e "$normalized_path" ]; then
            ln -s "$input_path" "$normalized_path"
            echo "Created encoder_cli-compatible alias: $normalized_path -> $input_path"
        fi
        echo "$normalized_path"
        return 0
    fi

    # Secondary typo variant: NAME_WIDTH_HEIGHT.yuv -> NAME_WIDTHxHEIGHT.yuv
    if [[ "$input_base" =~ ^(.+)_([0-9]+)_([0-9]+)\.yuv$ ]]; then
        local prefix="${BASH_REMATCH[1]}"
        local width="${BASH_REMATCH[2]}"
        local height="${BASH_REMATCH[3]}"
        local normalized_path="${input_dir}/${prefix}_${width}x${height}.yuv"
        if [ ! -e "$normalized_path" ]; then
            ln -s "$input_path" "$normalized_path"
            echo "Created encoder_cli-compatible alias: $normalized_path -> $input_path"
        fi
        echo "$normalized_path"
        return 0
    fi

    echo "ERROR: Input filename must match NAME_WIDTHxHEIGHT[_FPS].yuv for encoder_cli: $input_base" >&2
    echo "       Example: pandaset_1920x1080_30.yuv" >&2
    return 1
}

extract_fps_from_input_filename() {
    local input_basename="$1"
    python3 -c "
import re
name = '$input_basename'
m = re.search(r'_[0-9]+x[0-9]+_([0-9]+)\.yuv$', name)
if m:
    print(m.group(1))
else:
    print('30')
"
}

extract_fps_from_encoder_log() {
    local encoder_log="$1"
    if [ ! -f "$encoder_log" ]; then
        echo ""
        return 0
    fi
    python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
m = re.search(r'Video framerate set from command line:\s*([0-9]+(?:\.[0-9]+)?)', log)
if m:
    print(int(round(float(m.group(1)))))
" 2>/dev/null || true
}

[ ! -f "$GROUNDTRUTH_FILE" ] && { echo "Groundtruth file not found: $GROUNDTRUTH_FILE"; exit 1; }
[ ! -f "$TILE_FILE" ]        && { echo "Tile file not found: $TILE_FILE"; exit 1; }

if ! ensure_input_yuv_exists_or_generate "$INPUT_FILE" "$DATASET_DIR" "$GROUNDTRUTH_FILE"; then
    exit 1
fi
[ ! -f "$INPUT_FILE" ]       && { echo "Input file not found: $INPUT_FILE"; exit 1; }

INPUT_FILE="$(resolve_path "$INPUT_FILE")"
GROUNDTRUTH_FILE="$(resolve_path "$GROUNDTRUTH_FILE")"

if ! INPUT_FILE="$(normalize_input_filename_for_encoder_cli "$INPUT_FILE")"; then
    exit 1
fi

# ---------------------------------------------------------------------------
# Extract unique category names present in the groundtruth annotations so
# rtp_server evaluates only the classes that matter.
#
# For COCO JSON, the label list is extracted from the file.
# ---------------------------------------------------------------------------
GROUNDTRUTH_LABELS="$(python3 -c "
import json, sys
with open('$GROUNDTRUTH_FILE') as f:
    data = json.load(f)
annotated_ids = set(a['category_id'] for a in data['annotations'])
names = [cat['name'] for cat in data.get('categories', []) if cat['id'] in annotated_ids]
print(','.join(sorted(set(names))))
")"
echo "Groundtruth labels: $GROUNDTRUTH_LABELS"
echo "QP values to test: $QP_VALUES"

# Parse the input filename to extract framerate (needed for bitrate calculation)
INPUT_BASENAME="$(basename "$INPUT_FILE")"
FRAMERATE="$(extract_fps_from_input_filename "$INPUT_BASENAME")"
echo "Filename framerate hint: ${FRAMERATE} fps"

mkdir -p "$OUTPUT_DIR"

# ---------------------------------------------------------------------------
# One-time build of both binaries so we don't rebuild inside each run.sh call.
# ---------------------------------------------------------------------------
echo "=== Building rtp_server ==="
(cd "$RTP_SERVER_DIR" && ./build.sh)

echo "=== Building encoder_cli ==="
(cd "$ENCODER_DIR" && ./build.sh)

# ---------------------------------------------------------------------------
# Kill any stale rtp_server processes from previous runs before we begin.
# ---------------------------------------------------------------------------
echo "=== Cleaning up stale rtp_server processes ==="
stale_pids="$(pgrep -x rtp_server 2>/dev/null || true)"
if [ -n "$stale_pids" ]; then
    echo "Found stale rtp_server processes: $stale_pids"
    for pid in $stale_pids; do
        echo "  Killing pid $pid..."
        kill -INT "$pid" 2>/dev/null || true
    done
    sleep 3
    for pid in $stale_pids; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done
    sleep 1
    echo "Stale processes cleaned up."
else
    echo "No stale rtp_server processes found."
fi

# ---------------------------------------------------------------------------
# Helper: update TET section in an INI config file
# ---------------------------------------------------------------------------
update_tet_config() {
    local source_config="$1"
    local output_config="$2"
    local enabled="$3"
    local tile_file="$4"
    local qp_value="$5"
    local dynamic_qp="$6"

    awk -v enabled="$enabled" \
        -v tile_file="$tile_file" \
        -v qp_value="$qp_value" \
        -v dynamic_qp="$dynamic_qp" '
        BEGIN { in_tet = 0 }
        /^[[:space:]]*\[TET\][[:space:]]*$/ { in_tet = 1; print; next }
        /^[[:space:]]*\[/ { if (in_tet) { in_tet = 0 } }
        in_tet && $0 ~ /^[[:space:]]*enabled[[:space:]]*=/ { print "enabled = " enabled; next }
        in_tet && $0 ~ /^[[:space:]]*tile_file[[:space:]]*=/ { print "tile_file = " tile_file; next }
        in_tet && $0 ~ /^[[:space:]]*qp[[:space:]]*=/ { print "qp = " qp_value; next }
        in_tet && $0 ~ /^[[:space:]]*dynamic_qp[[:space:]]*=/ { print "dynamic_qp = " dynamic_qp; next }
        { print }
    ' "$source_config" > "$output_config"
}

# ---------------------------------------------------------------------------
# Helper: kill any rtp_server processes that are holding a given port.
# ---------------------------------------------------------------------------
kill_rtp_servers_on_port() {
    local port="${1:-5000}"
    local pids
    pids="$(ss -lnup 2>/dev/null | grep ":${port} " | grep -oP 'pid=\K[0-9]+' | sort -u)"
    if [ -z "$pids" ]; then
        return 0
    fi
    echo "Found stale rtp_server process(es) on port ${port}: $pids"
    for pid in $pids; do
        echo "  Sending SIGINT to pid $pid..."
        kill -INT "$pid" 2>/dev/null || true
    done
    sleep 3
    for pid in $pids; do
        if kill -0 "$pid" 2>/dev/null; then
            echo "  pid $pid still alive, sending SIGKILL..."
            kill -KILL "$pid" 2>/dev/null || true
        fi
    done
    sleep 1
}

# ---------------------------------------------------------------------------
# Helper: ensure no rtp_server is occupying the handshake port.
# ---------------------------------------------------------------------------
ensure_port_free() {
    local port="${1:-5000}"
    if ss -lnup 2>/dev/null | grep -q ":${port} "; then
        kill_rtp_servers_on_port "$port"
    fi
    local attempts=0
    while ss -lnup 2>/dev/null | grep -q ":${port} " && [ "$attempts" -lt 10 ]; do
        echo "Port $port still in use, waiting..."
        sleep 2
        attempts=$((attempts + 1))
    done
    if ss -lnup 2>/dev/null | grep -q ":${port} "; then
        echo "ERROR: Port $port could not be freed."
        return 1
    fi
}

# ---------------------------------------------------------------------------
# Helper: wait for the rtp_server log to contain "Listening" (server ready).
# ---------------------------------------------------------------------------
wait_for_server_ready() {
    local log_file="$1"
    local timeout="${2:-30}"
    local waited=0
    echo "Waiting for rtp_server to start listening..."
    while [ "$waited" -lt "$timeout" ]; do
        if [ -f "$log_file" ] && grep -q "Listening for new clients" "$log_file" 2>/dev/null; then
            echo "rtp_server is ready (after ${waited}s)."
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "WARNING: rtp_server did not report listening within ${timeout}s."
    return 1
}

# ---------------------------------------------------------------------------
# Helper: gracefully stop the rtp_server.
# ---------------------------------------------------------------------------
shutdown_rtp() {
    local pid="$1"
    if ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi

    echo "Sending SIGINT to rtp_server (pid $pid)..."
    kill -INT "$pid" 2>/dev/null || true

    local elapsed=0
    while kill -0 "$pid" 2>/dev/null && [ "$elapsed" -lt "$SERVER_SHUTDOWN_TIMEOUT" ]; do
        sleep 1
        elapsed=$((elapsed + 1))
    done

    if kill -0 "$pid" 2>/dev/null; then
        echo "rtp_server did not exit after ${SERVER_SHUTDOWN_TIMEOUT}s, sending SIGTERM..."
        kill -TERM "$pid" 2>/dev/null || true
        sleep 5
    fi

    if kill -0 "$pid" 2>/dev/null; then
        echo "rtp_server still running, sending SIGKILL..."
        kill -KILL "$pid" 2>/dev/null || true
    fi

    wait "$pid" 2>/dev/null || true
}

# ---------------------------------------------------------------------------
# Helper: measure encoded bitrate.
#
# Tries multiple extraction methods in priority order:
#   1. Structured "Bitrate summary" line from the encoder log (most reliable,
#      includes file_size_bytes, num_frames, framerate, bitrate_bps).
#   2. H.265 output file on disk (measures file size, extracts frame count
#      from encoder log, computes bitrate).
#   3. Structured "Encoded bitrate summary" from the rtp_server log (uses
#      h265_bytes and h265_frames tracked by the rtp_server pipeline).
#   4. Per-frame encoded buffer sizes from the rtp_server log
#      ("Frame #N buffer size: X bytes"), summed and converted to bps.
#   5. "Average bitrate: X.XX kbps" line from the encoder log (fallback).
#
# The result is written to ${case_dir}/bitrate_bps.txt.
# ---------------------------------------------------------------------------
measure_bitrate() {
    local case_dir="$1"
    local encoder_log="$2"
    local server_log="$3"
    local framerate="$4"

    local bitrate_bps=0

    # ---- Method 1: structured "Bitrate summary" from encoder log ----
    if [ -f "$encoder_log" ]; then
        bitrate_bps=$(python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
m = re.search(r'Bitrate summary:.*?bitrate_bps=(\d+)', log)
if m:
    print(m.group(1))
else:
    print(0)
" 2>/dev/null || echo "0")
        if [ "$bitrate_bps" -gt 0 ] 2>/dev/null; then
            echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
            echo "Measured bitrate (encoder Bitrate summary): ${bitrate_bps} bps"
            return 0
        fi
    fi

    # ---- Method 2: H.265 output file size on disk ----
    # The encoder writes the H.265 bitstream to ../yolo/yolo_output_with_sei.h265
    # (or _packed.h265 if ROI packing is used) relative to encoder_cli/.
    local h265_file=""
    for candidate in \
        "${ENCODER_DIR}/../yolo/yolo_output_with_sei.h265" \
        "${ENCODER_DIR}/../yolo/yolo_output_with_sei_packed.h265"; do
        if [ -f "$candidate" ]; then
            h265_file="$candidate"
            break
        fi
    done

    if [ -n "$h265_file" ]; then
        local file_size_bytes
        file_size_bytes=$(stat -c%s "$h265_file")

        # Extract frame count from encoder log
        local num_frames=0
        if [ -f "$encoder_log" ]; then
            num_frames=$(python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
# Try structured Bitrate summary first for frame count
m = re.search(r'Bitrate summary:.*?num_frames=(\d+)', log)
if m:
    print(m.group(1))
else:
    # Try 'Pushed to Encoder: N' from status report
    m = re.search(r'Pushed to Encoder:\s*(\d+)', log)
    if m:
        print(m.group(1))
    else:
        print(0)
" 2>/dev/null || echo "0")
        fi

        if [ "$num_frames" -gt 0 ] 2>/dev/null; then
            bitrate_bps=$(python3 -c "
file_size = $file_size_bytes
num_frames = $num_frames
fps = $framerate
duration_s = num_frames / fps
bitrate_bps = int(file_size * 8 / duration_s) if duration_s > 0 else 0
print(bitrate_bps)
")
            if [ "$bitrate_bps" -gt 0 ] 2>/dev/null; then
                echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
                echo "Measured bitrate (H.265 file): ${bitrate_bps} bps (file=${file_size_bytes} bytes, frames=${num_frames}, fps=${framerate})"
                return 0
            fi
        fi
    fi

    # ---- Method 3: structured "Encoded bitrate summary" from rtp_server log ----
    if [ -f "$server_log" ]; then
        bitrate_bps=$(python3 -c "
import re
log = open('$server_log', errors='ignore').read()
m = re.search(r'Encoded bitrate summary:.*?bitrate_bps=(\d+)', log)
if m:
    print(m.group(1))
else:
    print(0)
" 2>/dev/null || echo "0")
        if [ "$bitrate_bps" -gt 0 ] 2>/dev/null; then
            echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
            echo "Measured bitrate (rtp_server Encoded bitrate summary): ${bitrate_bps} bps"
            return 0
        fi
    fi

    # ---- Method 4: per-frame encoded buffer sizes from rtp_server log ----
    if [ -f "$server_log" ]; then
        local parsed_bitrate=0
        local parsed_bytes=0
        local parsed_frames=0
        read -r parsed_bitrate parsed_bytes parsed_frames <<< "$(python3 -c "
import re

log = open('$server_log', errors='ignore').read()
matches = re.findall(r'Frame\\s+#(\\d+)\\s+buffer size:\\s*(\\d+)\\s+bytes\\s+for\\s+port', log)
if not matches:
    print('0 0 0')
else:
    frame_sizes = {}
    for frame_idx, size_bytes in matches:
        fi = int(frame_idx)
        sb = int(size_bytes)
        prev = frame_sizes.get(fi, 0)
        frame_sizes[fi] = sb if sb > prev else prev

    num_frames = len(frame_sizes)
    total_bytes = sum(frame_sizes.values())
    fps = float('$framerate')
    if num_frames > 0 and total_bytes > 0 and fps > 0:
        duration_s = num_frames / fps
        bitrate_bps = int(total_bytes * 8 / duration_s) if duration_s > 0 else 0
        print(f'{bitrate_bps} {total_bytes} {num_frames}')
    else:
        print('0 0 0')
" 2>/dev/null || echo "0 0 0")"

        if [ "${parsed_bitrate}" -gt 0 ] 2>/dev/null; then
            bitrate_bps="${parsed_bitrate}"
            echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
            echo "Measured bitrate (rtp_server frame-size logs): ${bitrate_bps} bps (bytes=${parsed_bytes}, frames=${parsed_frames}, fps=${framerate})"
            return 0
        fi
    fi

    # ---- Method 5: "Average bitrate: X.XX kbps" from encoder log ----
    if [ -f "$encoder_log" ]; then
        bitrate_bps=$(python3 -c "
import re
log = open('$encoder_log', errors='ignore').read()
m = re.search(r'Average bitrate:\s*([0-9.]+)\s*kbps', log)
if m:
    print(int(float(m.group(1)) * 1000))
else:
    print(0)
" 2>/dev/null || echo "0")
        if [ "$bitrate_bps" -gt 0 ] 2>/dev/null; then
            echo "$bitrate_bps" > "${case_dir}/bitrate_bps.txt"
            echo "Measured bitrate (encoder Average bitrate): ${bitrate_bps} bps"
            return 0
        fi
    fi

    # ---- All methods failed ----
    echo "0" > "${case_dir}/bitrate_bps.txt"
    echo "WARNING: Could not measure bitrate for this case. All extraction methods failed."
}

# ---------------------------------------------------------------------------
# Global cleanup: kill any server we started if the script is interrupted.
# ---------------------------------------------------------------------------
RTP_SERVER_PID=""
cleanup_on_exit() {
    if [ -n "$RTP_SERVER_PID" ] && kill -0 "$RTP_SERVER_PID" 2>/dev/null; then
        echo "Script interrupted, shutting down rtp_server (pid $RTP_SERVER_PID)..."
        kill -INT "$RTP_SERVER_PID" 2>/dev/null || true
        sleep 2
        kill -KILL "$RTP_SERVER_PID" 2>/dev/null || true
        wait "$RTP_SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup_on_exit EXIT

# ---------------------------------------------------------------------------
# run_case <name> <qp_value> <tet_enabled>
# ---------------------------------------------------------------------------
run_case() {
    local case_name="$1"
    local qp_value="$2"
    local tet_enabled="$3"

    local case_dir="${OUTPUT_DIR}/${case_name}"
    mkdir -p "$case_dir"
    local summary_json="${case_dir}/test_mode_${case_name}.json"
    local summary_md="${case_dir}/test_mode_${case_name}.md"

    # Prepare per-case rtp_server config with TET on/off.
    # The TET qp field in the config controls which QP index the TET uses
    # internally; dynamic_qp=true means the server reads QP from NAL units.
    local temp_config="${case_dir}/rtp_server_config_${case_name}.ini"
    update_tet_config "${RTP_SERVER_DIR}/rtp_server_config.ini" "$temp_config" \
        "$tet_enabled" "$TILE_FILE" "$qp_value" "true"

    echo ""
    echo "================================================================"
    echo "=== Running case: ${case_name}"
    echo "===   QP=$qp_value  TET=$tet_enabled"
    echo "================================================================"

    # Make sure port 5000 is free from any previous run
    ensure_port_free 5000

    # Remove stale encoder output so we don't confuse old results with new ones
    rm -f "${ENCODER_DIR}/encoder_output.txt"
    # Remove stale H.265 output file so bitrate measurement is accurate
    rm -f "${ENCODER_DIR}/../yolo/yolo_output_with_sei.h265"
    rm -f "${ENCODER_DIR}/../yolo/yolo_output_with_sei_packed.h265"

    # ---- Start rtp_server ----
    local server_log="${case_dir}/rtp_server_output_${case_name}.txt"
    (
        cd "$RTP_SERVER_DIR"
        source scripts/common_rtp_server_setup.sh
        setup_environment
        RTP_SERVER_BINARY="$(find_rtp_server_binary)" || {
            echo "ERROR: Could not find rtp_server binary" >&2
            exit 1
        }
        exec "$RTP_SERVER_BINARY" \
            --server-transport-type rtp \
            --config "$temp_config" \
            --test-mode-groundtruth-file "$GROUNDTRUTH_FILE" \
            --test-mode-output-md "$summary_md" \
            --test-mode-output-json "$summary_json" \
            --inference-labels "$GROUNDTRUTH_LABELS"
    ) > "$server_log" 2>&1 &
    RTP_SERVER_PID=$!
    echo "rtp_server started (pid $RTP_SERVER_PID)"

    # Wait for the server to be ready before starting the encoder
    if ! wait_for_server_ready "$server_log" 60; then
        echo "ERROR: rtp_server failed to start for ${case_name}. Skipping."
        shutdown_rtp "$RTP_SERVER_PID"
        RTP_SERVER_PID=""
        return 1
    fi

    # Additional small delay to let things settle
    sleep "$SLEEP_BEFORE_ENCODER"

    # ---- Run encoder_cli with constant QP ----
    # Enable H.265 branch so the bitstream is saved to disk for bitrate measurement.
    # Use COCO groundtruth ROIs to isolate encoding/TET effects from detection
    # variability.
    echo "Starting encoder_cli for ${case_name} (QP=${qp_value})..."
    set +e
    (
        cd "$ENCODER_DIR"
        export NO_BUILD=true

        ROI_ARGS=(--roi-groundtruth-file "$GROUNDTRUTH_FILE")

        ./run.sh file rtp "$INPUT_FILE" -- \
            --encoder-qp "$qp_value" \
            "${ROI_ARGS[@]}" \
            --background-color 0 \
            --debug-enable-h265-branch
    )
    local encoder_exit_code=$?
    set -e

    if [ "$encoder_exit_code" -ne 0 ]; then
        echo "WARNING: Encoder exited with code ${encoder_exit_code} for ${case_name}."
    fi

    # ---- Wait for the JSON result file ----
    echo "Encoder finished. Waiting up to ${RESULT_WAIT_TIMEOUT}s for test mode results..."
    local waited=0
    while [ "$waited" -lt "$RESULT_WAIT_TIMEOUT" ]; do
        if [ -f "$summary_json" ]; then
            echo "JSON summary found after ${waited}s."
            break
        fi
        if ! kill -0 "$RTP_SERVER_PID" 2>/dev/null; then
            echo "rtp_server exited before producing results."
            break
        fi
        sleep 2
        waited=$((waited + 2))
    done
    if [ "$waited" -ge "$RESULT_WAIT_TIMEOUT" ]; then
        echo "WARNING: Timed out waiting for ${summary_json}."
    fi

    # ---- Shut down the server ----
    shutdown_rtp "$RTP_SERVER_PID"
    RTP_SERVER_PID=""

    # ---- Copy encoder log ----
    local encoder_log="${case_dir}/encoder_output_${case_name}.txt"
    [ -f "${ENCODER_DIR}/encoder_output.txt" ] && \
        cp "${ENCODER_DIR}/encoder_output.txt" "$encoder_log"

    # ---- Measure encoded bitrate ----
    local case_fps="$FRAMERATE"
    local case_fps_source="input filename"
    local encoder_fps=""
    encoder_fps="$(extract_fps_from_encoder_log "$encoder_log")"
    if [ -n "$encoder_fps" ]; then
        case_fps="$encoder_fps"
        case_fps_source="encoder log"
        if [ "$encoder_fps" != "$FRAMERATE" ]; then
            echo "WARNING: FPS mismatch for ${case_name}: filename=${FRAMERATE}, encoder=${encoder_fps}. Using encoder FPS."
        fi
    fi
    echo "Bitrate FPS for ${case_name}: ${case_fps} (${case_fps_source})"
    measure_bitrate "$case_dir" "$encoder_log" "$server_log" "$case_fps"

    echo "=== Completed case: ${case_name} ==="
}

# ---------------------------------------------------------------------------
# Run test cases: for each QP, run TET-off then TET-on
# ---------------------------------------------------------------------------
echo ""
echo "========================================"
echo "Starting BD-rate evaluation"
echo "QP values: $QP_VALUES"
echo "Output directory: $OUTPUT_DIR"
echo "========================================"

for qp in $QP_VALUES; do
    run_case "qp${qp}_tet_off" "$qp" "false"
    run_case "qp${qp}_tet_on"  "$qp" "true"
done

# ---------------------------------------------------------------------------
# Aggregate results into CSV for BD-rate computation
# ---------------------------------------------------------------------------
echo ""
echo "=== Aggregating results ==="

OUTPUT_DIR_ENV="$OUTPUT_DIR" QP_VALUES_ENV="$QP_VALUES" python3 - <<'PY'
import json
import os
import re
from pathlib import Path

output_dir = Path(os.environ["OUTPUT_DIR_ENV"])
qp_values = os.environ.get("QP_VALUES_ENV", "22 27 32 37 42 47").split()

columns = ["qp", "tet", "bitrate_bps", "precision", "recall",
           "f1_score", "mean_iou", "true_positive", "false_positive",
           "false_negative"]
rows = []

# Also collect data for the per-QP summary
summary_rows = []

for qp in qp_values:
    for tet_label in ["off", "on"]:
        case_name = f"qp{qp}_tet_{tet_label}"
        json_path = output_dir / case_name / f"test_mode_{case_name}.json"
        bitrate_path = output_dir / case_name / "bitrate_bps.txt"
        log_path = output_dir / case_name / f"rtp_server_output_{case_name}.txt"

        # Read bitrate
        bitrate_bps = 0
        if bitrate_path.exists():
            try:
                bitrate_bps = int(bitrate_path.read_text().strip() or "0")
            except ValueError:
                bitrate_bps = 0

        # Try to read metrics from JSON first, then fall back to log parsing
        data = None
        if json_path.exists():
            try:
                data = json.loads(json_path.read_text())
            except Exception:
                data = None

        if data is None and log_path.exists():
            # Fall back to parsing the rtp_server log
            log_text = log_path.read_text(errors="ignore")
            summary_pattern = re.compile(
                r"Test mode summary for port \d+: frames=(\d+)\s+"
                r"gt_total=(\d+)\s+det_total=(\d+)\s+"
                r"tp=(\d+)\s+fp=(\d+)\s+fn=(\d+)\s+"
                r"precision=([0-9.]+)\s+recall=([0-9.]+)\s+"
                r"f1=([0-9.]+)\s+mean_iou=([0-9.]+)"
            )
            match = summary_pattern.search(log_text)
            if match:
                data = {
                    "true_positive": int(match.group(4)),
                    "false_positive": int(match.group(5)),
                    "false_negative": int(match.group(6)),
                    "precision": float(match.group(7)),
                    "recall": float(match.group(8)),
                    "f1_score": float(match.group(9)),
                    "mean_iou": float(match.group(10)),
                }

        if data:
            row = {
                "qp": qp,
                "tet": tet_label,
                "bitrate_bps": bitrate_bps,
                "precision": data.get("precision", 0),
                "recall": data.get("recall", 0),
                "f1_score": data.get("f1_score", 0),
                "mean_iou": data.get("mean_iou", 0),
                "true_positive": data.get("true_positive", 0),
                "false_positive": data.get("false_positive", 0),
                "false_negative": data.get("false_negative", 0),
            }
            rows.append(row)
            summary_rows.append(row)
        else:
            print(f"WARNING: No results found for {case_name}")
            rows.append({
                "qp": qp,
                "tet": tet_label,
                "bitrate_bps": bitrate_bps,
                "precision": 0,
                "recall": 0,
                "f1_score": 0,
                "mean_iou": 0,
                "true_positive": 0,
                "false_positive": 0,
                "false_negative": 0,
            })

# Write the BD-rate input CSV
csv_path = output_dir / "bdrate_input.csv"
with csv_path.open("w") as f:
    f.write(",".join(columns) + "\n")
    for row in rows:
        f.write(",".join(str(row[c]) for c in columns) + "\n")
print(f"Wrote {csv_path}")

# Write a human-readable summary markdown
md_path = output_dir / "summary.md"
md_lines = [
    "# TET BD-rate Evaluation Summary",
    "",
    "## Per-QP Results",
    "",
    "| QP | TET | Bitrate (Mbps) | Precision | Recall | F1 Score | Mean IoU | TP | FP | FN |",
    "|----|-----|----------------|-----------|--------|----------|----------|----|----|------|",
]
for row in rows:
    bitrate_mbps = row["bitrate_bps"] / 1_000_000 if row["bitrate_bps"] > 0 else 0
    md_lines.append(
        f"| {row['qp']} | {row['tet']} | {bitrate_mbps:.2f} "
        f"| {row['precision']:.3f} | {row['recall']:.3f} "
        f"| {row['f1_score']:.3f} | {row['mean_iou']:.3f} "
        f"| {row['true_positive']} | {row['false_positive']} "
        f"| {row['false_negative']} |"
    )

# Add delta table (TET-on minus TET-off for each QP)
md_lines.extend([
    "",
    "## TET Deltas (TET-on minus TET-off)",
    "",
    "| QP | Delta Precision | Delta Recall | Delta F1 | Delta Mean IoU |",
    "|----|-----------------|--------------|----------|----------------|",
])
rows_by_key = {(r["qp"], r["tet"]): r for r in rows}
for qp in qp_values:
    off = rows_by_key.get((qp, "off"))
    on = rows_by_key.get((qp, "on"))
    if off and on:
        dp = on["precision"] - off["precision"]
        dr = on["recall"] - off["recall"]
        df = on["f1_score"] - off["f1_score"]
        di = on["mean_iou"] - off["mean_iou"]
        md_lines.append(
            f"| {qp} | {dp:+.3f} | {dr:+.3f} | {df:+.3f} | {di:+.3f} |"
        )

md_lines.append("")
md_path.write_text("\n".join(md_lines))
print(f"Wrote {md_path}")
PY

# ---------------------------------------------------------------------------
# Run BD-rate computation if compute_bdrate.py exists
# ---------------------------------------------------------------------------
BDRATE_SCRIPT="${ROOT_DIR}/scripts/tet_comparison_framework/tools/compute_bdrate.py"
if [ -f "$BDRATE_SCRIPT" ]; then
    echo ""
    echo "=== Computing BD-rate ==="
    echo "--- F1 Score ---"
    python3 "$BDRATE_SCRIPT" "${OUTPUT_DIR}/bdrate_input.csv" --metric f1_score || true
    echo ""
    echo "--- Mean IoU ---"
    python3 "$BDRATE_SCRIPT" "${OUTPUT_DIR}/bdrate_input.csv" --metric mean_iou || true
    echo ""
    echo "--- Precision ---"
    python3 "$BDRATE_SCRIPT" "${OUTPUT_DIR}/bdrate_input.csv" --metric precision || true
    echo ""
    echo "--- Recall ---"
    python3 "$BDRATE_SCRIPT" "${OUTPUT_DIR}/bdrate_input.csv" --metric recall || true
else
    echo ""
    echo "BD-rate script not found at: $BDRATE_SCRIPT"
    echo "Run manually: python3 scripts/tet_comparison_framework/tools/compute_bdrate.py ${OUTPUT_DIR}/bdrate_input.csv --metric f1_score"
fi

echo ""
echo "All runs completed. Results in: $OUTPUT_DIR"
echo "BD-rate input CSV: ${OUTPUT_DIR}/bdrate_input.csv"
