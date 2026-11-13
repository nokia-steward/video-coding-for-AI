#!/bin/bash
# ---------------------------------------------------------------------------
# run_tet_comparison.sh — TET comparison across multiple QP values
#
# For each QP value, runs a seven-case single-mode matrix:
#   - baseline: all modes off
#   - roi packing only
#   - background blur only
#   - qp_map_delta only
#   - TET only
#   - luma scaling only (static factor)
#   - adaptive luma scaling only (input-analyzer driven)
# using constant-QP encoding (--encoder-qp).  For each case the script:
#   1. Starts rtp_server with the appropriate TET and inference config
#   2. Runs encoder_cli at the given constant QP, using COCO groundtruth
#      bounding boxes as ROIs (--roi-groundtruth-file) to isolate encoding
#      effects from detection variability
#   3. Waits for the test-mode JSON result
#   4. Measures the actual encoded bitrate from the H.265 output file
#   5. Shuts down the server and moves to the next case
#
# The --groundtruth file is a COCO JSON used by:
#   - rtp_server for evaluation (test mode)
#   - encoder_cli as ROI source (--roi-groundtruth-file)
#
# If --groundtruth does not exist, the script attempts PandaSet COCO
# conversion via scripts/tet_comparison_framework/tools/convert_pandaset_to_coco.py
# using --dataset-dir (or
# fallback dataset-root inference from --groundtruth/--input paths).
#
# If --input does not exist, the script attempts PandaSet YUV generation via
# scripts/tet_comparison_framework/tools/generate_pandaset_front_camera_yuv.py
# using --dataset-dir (or
# fallback dataset-root inference from --groundtruth/--input paths).
#
# Results are aggregated into per-QP and cross-QP CSV/Markdown summaries.
# ---------------------------------------------------------------------------

set -euo pipefail
SCRIPT_START_TS="$(date +%s)"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
RTP_SERVER_DIR="${ROOT_DIR}/rtp_server"
ENCODER_DIR="${ROOT_DIR}/encoder_cli"

DEFAULT_TILE="${RTP_SERVER_DIR}/tiles/yuv_32x32_checkerboard.tet"

# QP values to test.  Set to a single value (e.g. "37") to reproduce the
# original single-QP behaviour.  The default covers the six QP values
# the TET was trained on.
QP_VALUES="${QP_VALUES:-22 27 32 37 42 47}"

INPUT_FILE=""
GROUNDTRUTH_FILE=""
TILE_FILE="${DEFAULT_TILE}"
OUTPUT_DIR=""
DATASET_DIR=""
SEMSEG_GROUNDTRUTH_FILE=""

SLEEP_BEFORE_ENCODER=5
SERVER_SHUTDOWN_TIMEOUT=30
# Time (seconds) to wait for the server to complete inference on all buffered
# frames AND write the JSON summary file.  With YOLOv11x (~1-2 s per frame)
# and 33 frames, plus the 30 s session-monitor inactivity timeout, 180 s is a
# generous upper bound.
RESULT_WAIT_TIMEOUT=180
BITRATE_POLICY="${BITRATE_POLICY:-strict}"
QP_MAP_BACKGROUND_DELTA="${QP_MAP_BACKGROUND_DELTA:-10}"
ENCODER_TIMEOUT="${ENCODER_TIMEOUT:-10}"
ENCODER_TIMEOUT_LEGACY="${ENCODER_TIMEOUT_LEGACY:-45}"
ENCODER_NO_MARKER_TIMEOUT="${ENCODER_NO_MARKER_TIMEOUT:-180}"
SAVE_FRAMES=false
RUN_MODE="${RUN_MODE:-both}"
PANDASET_PYTHON_VENV_DIR="${ROOT_DIR}/.env/pandaset"
PANDASET_PYTHON_BIN=""
SEMSEG_EVALUATOR_SCRIPT="${ROOT_DIR}/scripts/tet_comparison_framework/ah0187/evaluate_semseg_sparse.py"
SEMSEG_EVALUATOR_BACKEND="ah0187_detectron2_panoptic_fpn_r101_3x"
AH0187_PYTHON_RESOLVED=false
AH0187_PYTHON_MODE=""
AH0187_PYTHON_BIN=""
AH0187_PYTHON_VENV_DIR="${ROOT_DIR}/.env/semseg"
AH0187_AUTO_SETUP="${AH0187_AUTO_SETUP:-true}"
AH0187_CONDA_BIN=""
AH0187_CONDA_ENV="${AH0187_SEMSEG_CONDA_ENV:-pandaset}"
SEMSEG_FRAME_DRAIN_TIMEOUT="${SEMSEG_FRAME_DRAIN_TIMEOUT:-90}"
SEMSEG_MIN_CAPTURE_RATIO="${SEMSEG_MIN_CAPTURE_RATIO:-0.95}"
SEMSEG_INCOMPLETE_CAPTURE_POLICY="${SEMSEG_INCOMPLETE_CAPTURE_POLICY:-degrade}"

usage() {
    cat << 'EOF'
Usage:
  ./scripts/run_tet_comparison.sh [options]

Options:
  --input PATH         Path to input YUV file. If omitted and PandaSet
                       auto-generation is needed, uses
                       pandaset_<W>x<H>_<FPS>.yuv under dataset root.
  --groundtruth PATH   Path to COCO groundtruth JSON (required in single-
                       dataset mode).
  --dataset-dir PATH   Optional PandaSet dataset root used for input-YUV
                       or groundtruth auto-generation when those files are
  --dataset PATH       Alias for --dataset-dir
                       missing at the explicitly provided paths.
                       If this directory contains numeric sequence subfolders
                       (e.g. 057, 058, ...), the script enters sequence mode
                       and runs once per sequence folder.
  --tile PATH          Path to .tet tile file
  --semseg-groundtruth PATH
                      Path to semantic-segmentation sparse groundtruth JSON.
                      If missing and --mode includes semseg, it is generated
                      from PandaSet .pkl data and evaluated through the
                      AH0187 Detectron2 backend.
  --outdir PATH        Output directory for logs/results (default:
                       tet_test_results_<dataset>_<timestamp>)
  --qp-values "LIST"   Space-separated QP values (default: "22 27 32 37 42 47")
  --sleep SECONDS      Delay before starting encoder (default: 5)
  --shutdown SECONDS   Max seconds to wait for rtp_server shutdown (default: 30)
  --result-wait SECS   Max seconds to wait for JSON result file (default: 180)
  --bitrate-policy P   Bitrate handling policy: strict|tolerant (default: strict)
  --encoder-timeout S  Max seconds allowed after shutdown starts for qp_map_delta cases (default: 10)
  --encoder-timeout-legacy S
                     Max seconds allowed after shutdown starts for non-qp_map_delta cases (default: 45)
  --encoder-no-marker-timeout S
                     Max seconds allowed if no encoder shutdown marker appears (default: 180)
  --save-frames        Enable debug frame saving for both apps and store frames
                       under each case directory:
                       <case>/encoder_cli/{debug_frames,original_frames}
                       <case>/rtp_server/debug_frames/port_<PORT>
  --mode MODE          Comparison mode: bbox|semseg|both (default: both)
  -h, --help           Show this help

Environment Variables:
  QP_VALUES            Space-separated QP values (overridden by --qp-values)
  BITRATE_POLICY       strict|tolerant (overridden by --bitrate-policy)
  QP_MAP_BACKGROUND_DELTA
                      Background QP delta for qp_map_delta cases (default: 10)
  ENCODER_TIMEOUT      Max qp_map_delta shutdown duration in seconds (default: 10)
  ENCODER_TIMEOUT_LEGACY
                      Max non-qp_map_delta shutdown duration in seconds (default: 45)
  ENCODER_NO_MARKER_TIMEOUT
                      Max encoder runtime before shutdown marker appears (default: 180)
  AH0187_SEMSEG_PYTHON
                     Explicit Python interpreter for AH0187 semseg evaluator.
  AH0187_SEMSEG_CONDA_ENV
                     Conda env name to probe for AH0187 semseg evaluator (default: pandaset).
  AH0187_SEMSEG_CONDA_BIN
                     Optional full path to conda binary when conda is not on PATH.
  AH0187_AUTO_SETUP    true|false. If true, auto-creates .env/semseg and installs
                     evaluator dependencies when detectron2 runtime is missing.
  AH0187_TORCH_INDEX_URL
                     Optional index URL used when installing torch/torchvision during auto-setup.
  SEMSEG_FRAME_DRAIN_TIMEOUT
                     Seconds to wait for AH0187 frame export drain before shutdown (default: 90).
  SEMSEG_MIN_CAPTURE_RATIO
                     Minimum captured/expected frame ratio for semseg completeness (default: 0.95).
  SEMSEG_INCOMPLETE_CAPTURE_POLICY
                     Action when ratio is below threshold: degrade|fail (default: degrade).
  RUN_MODE             bbox|semseg|both (overridden by --mode)

Notes:
  - For each QP value the script runs seven test cases:
      baseline all-off
      roi packing only
      background blur only
      qp_map_delta only
      TET only
      luma scaling only (fixed factor 0.5; adaptive disabled)
      adaptive luma scaling only (input analyzer enabled)
  - For each case it starts rtp_server, runs encoder_cli at constant QP,
    waits for the test mode JSON summary, measures the encoded bitrate,
    then shuts down the server before moving to the next.
  - This script forces display output off for both applications during tests:
      encoder_cli uses per-case config with enable_display_branch=false
      rtp_server uses per-case config with enable_display_sink=false (headless fakesink)
  - encoder_cli always uses COCO groundtruth boxes as ROIs
    (--roi-groundtruth-file), isolating encoding/TET effects from detection
    variability.
  - If --groundtruth does not exist, the script attempts PandaSet conversion
    using scripts/tet_comparison_framework/tools/convert_pandaset_to_coco.py
    and a resolved dataset root
    (--dataset-dir or inferred from --groundtruth/--input).
  - If --input is omitted or does not exist, the script attempts PandaSet YUV generation
    using scripts/tet_comparison_framework/tools/generate_pandaset_front_camera_yuv.py
    and a resolved
    dataset root (--dataset-dir or inferred from --groundtruth/--input).
    If --input was provided, the generated alias is forced to:
      <input_basename_prefix>_<W>x<H>_<FPS>.yuv
  - Sequence mode:
      If --dataset-dir contains numeric sequence folders (e.g. 057, 058),
      this script runs one full comparison per sequence.
      Folder names starting with "_" are skipped.
      In sequence mode, explicit --input/--groundtruth values are ignored and
      per-sequence defaults/auto-generation are used.
  - Both rtp_server and encoder_cli are built once at the beginning.
  - strict policy aborts the run if a case bitrate cannot be measured.
    tolerant policy writes bitrate_bps=0 and continues.
  - Any encoder timeout/non-zero exit aborts the entire run.
  - Full script stdout/stderr is mirrored to:
      <outdir>/run_tet_comparison.log

Output structure:
  <outdir>/
    run_tet_comparison.log
    qp<N>/
      baseline_all_off/   (per-case logs, JSON, bitrate_bps.txt)
        encoder_cli/      (present when --save-frames is used)
        rtp_server/       (present when --save-frames is used)
      mode_roi_packing/
      mode_background_blur/
      mode_qp_map/
      mode_tet/
      mode_luma_scaling/
      mode_adaptive_luma_scaling/
    summary.csv           (all QP x mode x TET results)
    summary.md
    summary_deltas.csv

Examples:
  # Run with default QP values (22 27 32 37 42 47)
  ./scripts/run_tet_comparison.sh \
      --input /path/to/video.yuv \
      --groundtruth /path/to/gt.json

  # Run at a single QP (original behaviour)
  ./scripts/run_tet_comparison.sh --qp-values "37" \
      --input /path/to/video.yuv \
      --groundtruth /path/to/gt.json

  # Custom input, custom QP range
  ./scripts/run_tet_comparison.sh --qp-values "27 32 37 42" \
      --input /path/to/video.yuv --groundtruth /path/to/gt.json

  # PandaSet COCO workflow (auto-generate YUV if missing)
  ./scripts/run_tet_comparison.sh \
      --input PandaSet/pandaset_1920x1080_30.yuv \
      --groundtruth PandaSet/annotations/pandaset_1920x1080_30_coco5.json \
      --dataset-dir PandaSet

  # PandaSet workflow with auto-generated default YUV name
  ./scripts/run_tet_comparison.sh \
      --groundtruth PandaSet/annotations/pandaset_1920x1080_30_coco5.json \
      --dataset-dir PandaSet

  # Sequence mode (runs each numeric subfolder under PandaSet; skips "_*")
  ./scripts/run_tet_comparison.sh --dataset-dir PandaSet --qp-values "37"
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --input)         INPUT_FILE="$2";           shift 2 ;;
        --groundtruth)   GROUNDTRUTH_FILE="$2";     shift 2 ;;
        --dataset-dir|--dataset) DATASET_DIR="$2";  shift 2 ;;
        --tile)          TILE_FILE="$2";             shift 2 ;;
        --semseg-groundtruth) SEMSEG_GROUNDTRUTH_FILE="$2"; shift 2 ;;
        --outdir)        OUTPUT_DIR="$2";            shift 2 ;;
        --qp-values)     QP_VALUES="$2";            shift 2 ;;
        --sleep)         SLEEP_BEFORE_ENCODER="$2";  shift 2 ;;
        --shutdown)      SERVER_SHUTDOWN_TIMEOUT="$2"; shift 2 ;;
        --result-wait)   RESULT_WAIT_TIMEOUT="$2";   shift 2 ;;
        --bitrate-policy) BITRATE_POLICY="$2";       shift 2 ;;
        --encoder-timeout) ENCODER_TIMEOUT="$2";     shift 2 ;;
        --encoder-timeout-legacy) ENCODER_TIMEOUT_LEGACY="$2"; shift 2 ;;
        --encoder-no-marker-timeout) ENCODER_NO_MARKER_TIMEOUT="$2"; shift 2 ;;
        --save-frames)   SAVE_FRAMES=true;           shift 1 ;;
        --mode)          RUN_MODE="$2";              shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        *)               echo "Unknown argument: $1"; usage; exit 1 ;;
    esac
done

case "$BITRATE_POLICY" in
    strict|tolerant) ;;
    *)
        echo "Invalid --bitrate-policy: ${BITRATE_POLICY}. Expected strict or tolerant."
        exit 1
        ;;
esac

case "$RUN_MODE" in
    bbox|semseg|both) ;;
    *)
        echo "Invalid --mode: ${RUN_MODE}. Expected bbox, semseg, or both."
        exit 1
        ;;
esac

RUN_BBOX=false
RUN_SEMSEG=false
if [ "$RUN_MODE" = "bbox" ] || [ "$RUN_MODE" = "both" ]; then
    RUN_BBOX=true
fi
if [ "$RUN_MODE" = "semseg" ] || [ "$RUN_MODE" = "both" ]; then
    RUN_SEMSEG=true
fi

if ! [[ "$ENCODER_TIMEOUT" =~ ^[0-9]+$ ]] || [ "$ENCODER_TIMEOUT" -le 0 ]; then
    echo "Invalid --encoder-timeout: ${ENCODER_TIMEOUT}. Expected a positive integer."
    exit 1
fi
if ! [[ "$ENCODER_TIMEOUT_LEGACY" =~ ^[0-9]+$ ]] || [ "$ENCODER_TIMEOUT_LEGACY" -le 0 ]; then
    echo "Invalid --encoder-timeout-legacy: ${ENCODER_TIMEOUT_LEGACY}. Expected a positive integer."
    exit 1
fi
if ! [[ "$ENCODER_NO_MARKER_TIMEOUT" =~ ^[0-9]+$ ]] || [ "$ENCODER_NO_MARKER_TIMEOUT" -le 0 ]; then
    echo "Invalid --encoder-no-marker-timeout: ${ENCODER_NO_MARKER_TIMEOUT}. Expected a positive integer."
    exit 1
fi
if ! [[ "$SEMSEG_FRAME_DRAIN_TIMEOUT" =~ ^[0-9]+$ ]] || [ "$SEMSEG_FRAME_DRAIN_TIMEOUT" -le 0 ]; then
    echo "Invalid SEMSEG_FRAME_DRAIN_TIMEOUT: ${SEMSEG_FRAME_DRAIN_TIMEOUT}. Expected a positive integer."
    exit 1
fi
if ! python3 - "$SEMSEG_MIN_CAPTURE_RATIO" <<'PY'
import sys
try:
    value = float(sys.argv[1])
except Exception:
    raise SystemExit(1)
raise SystemExit(0 if 0.0 <= value <= 1.0 else 1)
PY
then
    echo "Invalid SEMSEG_MIN_CAPTURE_RATIO: ${SEMSEG_MIN_CAPTURE_RATIO}. Expected a number in [0.0, 1.0]."
    exit 1
fi
case "$SEMSEG_INCOMPLETE_CAPTURE_POLICY" in
    degrade|fail) ;;
    *)
        echo "Invalid SEMSEG_INCOMPLETE_CAPTURE_POLICY: ${SEMSEG_INCOMPLETE_CAPTURE_POLICY}. Expected degrade or fail."
        exit 1
        ;;
esac

# Load shared setup/path/dataset helpers for the TET comparison workflow.
source "${ROOT_DIR}/scripts/tet_comparison_framework/lib/tet_setup_helpers.sh"

build_projects_once() {
    echo "=== Building rtp_server ==="
    (cd "$RTP_SERVER_DIR" && ./build.sh)

    echo "=== Building encoder_cli ==="
    (cd "$ENCODER_DIR" && ./build.sh)
}

initialize_output_logging() {
    if [ -z "$OUTPUT_DIR" ]; then
        local dataset_name
        dataset_name="$(infer_default_output_dataset_name)"
        OUTPUT_DIR="${ROOT_DIR}/tet_test_results_${dataset_name}_$(date +%Y%m%d_%H%M%S)"
    else
        OUTPUT_DIR="$(resolve_path "$OUTPUT_DIR")"
    fi

    mkdir -p "$OUTPUT_DIR"

    local script_log_file="${OUTPUT_DIR}/run_tet_comparison.log"
    exec > >(tee "$script_log_file") 2>&1
    echo "Logging full script output to: $script_log_file"
}

initialize_output_logging

[ ! -f "$TILE_FILE" ]        && { echo "Tile file not found: $TILE_FILE"; exit 1; }

SEQUENCE_MODE=false
declare -a DISCOVERED_SEQUENCE_IDS=()
if [ -n "$DATASET_DIR" ]; then
    mapfile -t DISCOVERED_SEQUENCE_IDS < <(discover_pandaset_sequence_dirs "$DATASET_DIR")
    if [ "${#DISCOVERED_SEQUENCE_IDS[@]}" -gt 0 ]; then
        SEQUENCE_MODE=true
    fi
fi

if [ "$SEQUENCE_MODE" = "true" ]; then
    [ -n "$INPUT_FILE" ] && echo "Sequence mode: ignoring explicit --input (${INPUT_FILE})."
    [ -n "$GROUNDTRUTH_FILE" ] && echo "Sequence mode: ignoring explicit --groundtruth (${GROUNDTRUTH_FILE})."

    if [ -z "$OUTPUT_DIR" ]; then
        DATASET_NAME="$(basename "$DATASET_DIR")"
        OUTPUT_DIR="${ROOT_DIR}/tet_test_results_${DATASET_NAME}_$(date +%Y%m%d_%H%M%S)"
    else
        OUTPUT_DIR="$(resolve_path "$OUTPUT_DIR")"
    fi
    mkdir -p "$OUTPUT_DIR"

    echo "Detected PandaSet sequences under ${DATASET_DIR}: ${DISCOVERED_SEQUENCE_IDS[*]}"
    echo "Sequence output root: ${OUTPUT_DIR}"

    if [ "${SKIP_BUILDS:-false}" != "true" ]; then
        build_projects_once
    else
        echo "=== Skipping builds (SKIP_BUILDS=true) ==="
    fi

    for sequence_id in "${DISCOVERED_SEQUENCE_IDS[@]}"; do
        sequence_dataset_dir="${DATASET_DIR}/${sequence_id}"
        sequence_label="$(strip_leading_zeros "$sequence_id")"
        sequence_output_dir="${OUTPUT_DIR}/${sequence_label}"
        sequence_groundtruth="${sequence_dataset_dir}/annotations/groundtruth_coco5.json"

        echo ""
        echo "========================================"
        echo "Starting sequence ${sequence_id} (label ${sequence_label})"
        echo "Dataset dir: ${sequence_dataset_dir}"
        echo "Output dir: ${sequence_output_dir}"
        echo "========================================"

        sequence_cmd=(
            bash "${ROOT_DIR}/scripts/run_tet_comparison.sh"
            --dataset-dir "$sequence_dataset_dir"
            --groundtruth "$sequence_groundtruth"
            --tile "$TILE_FILE"
            --mode "$RUN_MODE"
            --outdir "$sequence_output_dir"
            --qp-values "$QP_VALUES"
            --sleep "$SLEEP_BEFORE_ENCODER"
            --shutdown "$SERVER_SHUTDOWN_TIMEOUT"
            --result-wait "$RESULT_WAIT_TIMEOUT"
            --bitrate-policy "$BITRATE_POLICY"
            --encoder-timeout "$ENCODER_TIMEOUT"
            --encoder-timeout-legacy "$ENCODER_TIMEOUT_LEGACY"
            --encoder-no-marker-timeout "$ENCODER_NO_MARKER_TIMEOUT"
        )
        if [ "$RUN_SEMSEG" = "true" ]; then
            sequence_cmd+=(--semseg-groundtruth "${sequence_dataset_dir}/annotations/groundtruth_semseg_sparse_vcm.json")
        fi
        if [ "$SAVE_FRAMES" = "true" ]; then
            sequence_cmd+=(--save-frames)
        fi

        SKIP_BUILDS=true "${sequence_cmd[@]}"
    done

    echo ""
    echo "All sequence runs completed. Results in: $OUTPUT_DIR"
    echo "Total runtime: $(format_elapsed_runtime)"
    exit 0
fi

[ -z "$GROUNDTRUTH_FILE" ] && { echo "Missing required argument: --groundtruth PATH"; usage; exit 1; }

if ! ensure_groundtruth_exists_or_generate "$GROUNDTRUTH_FILE" "$DATASET_DIR" "$INPUT_FILE"; then
    exit 1
fi
[ ! -f "$GROUNDTRUTH_FILE" ] && { echo "Groundtruth file not found: $GROUNDTRUTH_FILE"; exit 1; }

if ! ensure_input_yuv_exists_or_generate "$INPUT_FILE" "$DATASET_DIR" "$GROUNDTRUTH_FILE"; then
    exit 1
fi
[ ! -f "$INPUT_FILE" ]       && { echo "Input file not found: $INPUT_FILE"; exit 1; }

if [ "$RUN_SEMSEG" = "true" ]; then
    if ! ensure_semseg_groundtruth_exists_or_generate "$SEMSEG_GROUNDTRUTH_FILE" "$DATASET_DIR" "$INPUT_FILE" "$GROUNDTRUTH_FILE"; then
        exit 1
    fi
    [ ! -f "$SEMSEG_GROUNDTRUTH_FILE" ] && { echo "SemSeg groundtruth file not found: $SEMSEG_GROUNDTRUTH_FILE"; exit 1; }
    if ! ensure_ah0187_python_runtime; then
        exit 1
    fi
fi

# Canonicalize file paths after any auto-generation so downstream commands
# remain valid even when this script changes working directories.
INPUT_FILE="$(resolve_path "$INPUT_FILE")"
GROUNDTRUTH_FILE="$(resolve_path "$GROUNDTRUTH_FILE")"
SEMSEG_GROUNDTRUTH_FILE="$(resolve_path "$SEMSEG_GROUNDTRUTH_FILE")"

if ! INPUT_FILE="$(normalize_input_filename_for_encoder_cli "$INPUT_FILE")"; then
    exit 1
fi

# Derive default output directory from the input file's parent directory name
# so that results clearly show which dataset was used.
# Example: .../Traffic_2560x1600_30_val/Traffic_2560x1600_30.yuv
#       -> tet_test_results_Traffic_2560x1600_30_val_20260211_143000
if [ -z "$OUTPUT_DIR" ]; then
    DATASET_NAME="$(basename "$(dirname "$INPUT_FILE")")"
    OUTPUT_DIR="${ROOT_DIR}/tet_test_results_${DATASET_NAME}_$(date +%Y%m%d_%H%M%S)"
else
    OUTPUT_DIR="$(resolve_path "$OUTPUT_DIR")"
fi

# ---------------------------------------------------------------------------
# Extract unique category names present in the groundtruth annotations so
# rtp_server evaluates only the classes that matter (avoids FP inflation from
# unrelated detection classes like "traffic light").
#
# For COCO JSON files, the label list is extracted from the JSON.
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
echo "Bitrate policy: ${BITRATE_POLICY}"
echo "Encoder shutdown timeout (qp_map_delta): ${ENCODER_TIMEOUT}s"
echo "Encoder shutdown timeout (legacy modes): ${ENCODER_TIMEOUT_LEGACY}s"
echo "Encoder no-marker timeout: ${ENCODER_NO_MARKER_TIMEOUT}s"
echo "Save frames: ${SAVE_FRAMES}"

mkdir -p "$OUTPUT_DIR"

# ---------------------------------------------------------------------------
# One-time build of both binaries so we don't rebuild inside each run.sh call.
# ---------------------------------------------------------------------------
if [ "${SKIP_BUILDS:-false}" != "true" ]; then
    build_projects_once
else
    echo "=== Skipping builds (SKIP_BUILDS=true) ==="
fi

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
# Load shared per-case execution helpers (config patching, process control, bitrate).
source "${ROOT_DIR}/scripts/tet_comparison_framework/lib/tet_case_helpers.sh"

# ---------------------------------------------------------------------------
# Global cleanup: kill any server we started if the script is interrupted.
# ---------------------------------------------------------------------------
RTP_SERVER_PID=""
ENCODER_RUN_PID=""
INTERRUPTED=false
CLEANUP_RUNNING=false

handle_interrupt() {
    echo ""
    echo "Interrupt received, shutting down active processes..."
    INTERRUPTED=true
    if [ -n "$ENCODER_RUN_PID" ]; then
        echo "Interrupt context: active encoder process group pgid=${ENCODER_RUN_PID}"
        stop_encoder_process_group "$ENCODER_RUN_PID"
        ENCODER_RUN_PID=""
    fi
    if [ -n "$RTP_SERVER_PID" ]; then
        echo "Interrupt context: active rtp_server pid=${RTP_SERVER_PID}"
        shutdown_rtp "$RTP_SERVER_PID"
        RTP_SERVER_PID=""
    fi
    kill_encoder_residual_processes
    # Exit still triggers cleanup_on_exit; this should now be mostly a no-op.
    exit 130
}

cleanup_on_exit() {
    if [ "$CLEANUP_RUNNING" = true ]; then
        return 0
    fi
    CLEANUP_RUNNING=true
    # Do not allow Ctrl-C/TERM to interrupt teardown itself.
    trap '' INT TERM
    set +e

    echo "cleanup_on_exit: interrupted=${INTERRUPTED}, encoder_pgid=${ENCODER_RUN_PID:-none}, rtp_pid=${RTP_SERVER_PID:-none}"
    if [ -n "$ENCODER_RUN_PID" ]; then
        stop_encoder_process_group "$ENCODER_RUN_PID"
        ENCODER_RUN_PID=""
    fi

    if [ -n "$RTP_SERVER_PID" ] && kill -0 "$RTP_SERVER_PID" 2>/dev/null; then
        echo "Script interrupted, shutting down rtp_server (pid $RTP_SERVER_PID)..."
        kill -INT "$RTP_SERVER_PID" 2>/dev/null || true
        sleep 2
        kill -KILL "$RTP_SERVER_PID" 2>/dev/null || true
        wait "$RTP_SERVER_PID" 2>/dev/null || true
    fi

    # Final fallback sweep to avoid orphaned encoder processes.
    kill_encoder_residual_processes

    echo "Total runtime: $(format_elapsed_runtime)"
}
trap handle_interrupt INT TERM
trap cleanup_on_exit EXIT

# ---------------------------------------------------------------------------
# run_case <name> <qp_value> <mode> <blur_enabled> <pack_rois_enabled> <tet_enabled> [encoder_groundtruth_enabled] [semseg_enabled] [luma_adaptation_enabled] [luma_downscale_factor] [adaptive_luma_adaptation_enabled]
# ---------------------------------------------------------------------------
run_case() {
    local case_name="$1"
    local qp_value="$2"
    local mode="$3"
    local blur_enabled="$4"
    local pack_rois_enabled="$5"
    local tet_enabled="$6"
    local encoder_groundtruth_enabled="${7:-true}"
    local semseg_enabled="${8:-false}"
    local luma_adaptation_enabled="${9:-false}"
    local luma_downscale_factor="${10:-1.0}"
    local adaptive_luma_adaptation_enabled="${11:-false}"

    local case_dir="${OUTPUT_DIR}/qp${qp_value}/${case_name}"
    mkdir -p "$case_dir"
    local encoder_frames_dir="${case_dir}/encoder_cli"
    local rtp_server_frames_dir="${case_dir}/rtp_server"
    local -a rtp_server_extra_args=()
    local -a encoder_extra_args=()
    local -a mode_args=()
    if [ "$SAVE_FRAMES" = "true" ] || [ "$semseg_enabled" = "true" ]; then
        mkdir -p "$rtp_server_frames_dir"
        # Save debug PNGs only; do not enable stream recording branch.
        # Keep output_directory empty so filesink remains disabled.
        rtp_server_extra_args+=(--debug-save-frames --server-output-directory "")
    fi
    if [ "$SAVE_FRAMES" = "true" ]; then
        mkdir -p "$encoder_frames_dir"
        encoder_extra_args+=(--debug-save-frames --output-directory "$encoder_frames_dir")
    fi
    encoder_extra_args+=(--enable-luma-adaptation "$luma_adaptation_enabled")
    encoder_extra_args+=(--enable-adaptive-luma-adaptation "$adaptive_luma_adaptation_enabled")
    if [ "$luma_adaptation_enabled" = "true" ] && [ "$adaptive_luma_adaptation_enabled" != "true" ]; then
        encoder_extra_args+=(--luma-downscale-factor "$luma_downscale_factor")
    fi
    local summary_json="${case_dir}/test_mode_${case_name}.json"
    local summary_md="${case_dir}/test_mode_${case_name}.md"
    local semseg_summary_json="${case_dir}/test_mode_semseg_${case_name}.json"
    local semseg_summary_md="${case_dir}/test_mode_semseg_${case_name}.md"
    local expected_summary_json="$summary_json"
    local -a rtp_server_test_mode_args=()
    if [ "$semseg_enabled" = "true" ]; then
        expected_summary_json="$semseg_summary_json"
    else
        rtp_server_test_mode_args+=(
            --test-mode-groundtruth-file "$GROUNDTRUTH_FILE"
            --test-mode-output-md "$summary_md"
            --test-mode-output-json "$summary_json"
        )
    fi

    # Prepare per-case rtp_server config
    local temp_config_dir="${case_dir}"
    if [ "$SAVE_FRAMES" = "true" ] || [ "$semseg_enabled" = "true" ]; then
        # With output_directory empty, debug PNG root falls back to config directory.
        # Place per-case config under <case>/rtp_server to keep outputs separated.
        temp_config_dir="${rtp_server_frames_dir}"
    fi
    mkdir -p "$temp_config_dir"
    local temp_config="${temp_config_dir}/rtp_server_config_${case_name}.ini"
    update_tet_config "${RTP_SERVER_DIR}/rtp_server_config.ini" "$temp_config" \
        "$tet_enabled" "$TILE_FILE" "$qp_value" "true"
    if [ "$semseg_enabled" = "true" ]; then
        apply_semseg_case_rtp_server_overrides "$temp_config"
    fi
    local temp_encoder_config="${case_dir}/encoder_config_${case_name}.ini"
    update_encoder_config "${ENCODER_DIR}/encoder_config.ini" "$temp_encoder_config"

    local encoder_inference_cfg_value=""
    local encoder_inference_cfg_effective=""
    local encoder_inference_enabled_value=""
    local encoder_inference_enabled_effective="false"
    encoder_inference_cfg_value="$(read_ini_value_in_group "$temp_encoder_config" "ROI" "deepstream_config_file")"
    encoder_inference_cfg_effective="$(resolve_preferred_existing_path "$encoder_inference_cfg_value" \
        "$ENCODER_DIR" "$(dirname "$temp_encoder_config")")"
    encoder_inference_enabled_value="$(read_ini_value_in_group "$temp_encoder_config" "ROI" "inference_engine")"
    case "$(echo "${encoder_inference_enabled_value:-false}" | tr '[:upper:]' '[:lower:]')" in
        1|true|yes|on) encoder_inference_enabled_effective="true" ;;
        *) encoder_inference_enabled_effective="false" ;;
    esac

    local rtp_inference_cfg_value=""
    local rtp_inference_cfg_effective=""
    local rtp_inference_enabled_value=""
    local rtp_inference_enabled_effective="false"
    local case_semseg_config_source="n/a"
    rtp_inference_cfg_value="$(read_ini_value_in_group "$temp_config" "Inference" "deepstream_config_file")"
    rtp_inference_cfg_effective="$(resolve_preferred_existing_path "$rtp_inference_cfg_value" \
        "$RTP_SERVER_DIR" "$(dirname "$temp_config")")"
    rtp_inference_enabled_value="$(read_ini_value_in_group "$temp_config" "Inference" "inference_engine")"
    case "$(echo "${rtp_inference_enabled_value:-false}" | tr '[:upper:]' '[:lower:]')" in
        1|true|yes|on) rtp_inference_enabled_effective="true" ;;
        *) rtp_inference_enabled_effective="false" ;;
    esac
    if [ "$semseg_enabled" = "true" ]; then
        case_semseg_config_source="$SEMSEG_EVALUATOR_BACKEND"
    fi

    local case_metadata_file="${case_dir}/case_metadata.json"
    CASE_METADATA_FILE="$case_metadata_file" \
    CASE_NAME="$case_name" \
    CASE_QP="$qp_value" \
    CASE_MODE="$mode" \
    CASE_BLUR_ENABLED="$blur_enabled" \
    CASE_PACK_ROIS_ENABLED="$pack_rois_enabled" \
    CASE_TET_ENABLED="$tet_enabled" \
    CASE_ENCODER_GROUNDTRUTH_ENABLED="$encoder_groundtruth_enabled" \
    CASE_SEMSEG_ENABLED="$semseg_enabled" \
    CASE_LUMA_ADAPTATION_ENABLED="$luma_adaptation_enabled" \
    CASE_ADAPTIVE_LUMA_ADAPTATION_ENABLED="$adaptive_luma_adaptation_enabled" \
    CASE_LUMA_DOWNSCALE_FACTOR="$luma_downscale_factor" \
    CASE_ENCODER_CONFIG_FILE="$temp_encoder_config" \
    CASE_ENCODER_CONFIG_BASE_DIR="$ENCODER_DIR" \
    CASE_ENCODER_INFERENCE_CONFIG_FILE="$encoder_inference_cfg_effective" \
    CASE_ENCODER_INFERENCE_ENABLED="$encoder_inference_enabled_effective" \
    CASE_RTP_CONFIG_FILE="$temp_config" \
    CASE_RTP_CONFIG_BASE_DIR="$RTP_SERVER_DIR" \
    CASE_RTP_INFERENCE_CONFIG_FILE="$rtp_inference_cfg_effective" \
    CASE_RTP_INFERENCE_ENABLED="$rtp_inference_enabled_effective" \
    CASE_GROUNDTRUTH_FILE="$GROUNDTRUTH_FILE" \
    CASE_SEMSEG_GROUNDTRUTH_FILE="$SEMSEG_GROUNDTRUTH_FILE" \
    CASE_SEMSEG_CONFIG_RESOLUTION_SOURCE="$case_semseg_config_source" \
    python3 - <<'PY'
import json
import os
from pathlib import Path

metadata = {
    "case_name": os.environ.get("CASE_NAME", ""),
    "qp": os.environ.get("CASE_QP", ""),
    "mode": os.environ.get("CASE_MODE", ""),
    "blur_enabled": os.environ.get("CASE_BLUR_ENABLED", ""),
    "pack_rois_enabled": os.environ.get("CASE_PACK_ROIS_ENABLED", ""),
    "tet_enabled": os.environ.get("CASE_TET_ENABLED", ""),
    "encoder_groundtruth_enabled": os.environ.get("CASE_ENCODER_GROUNDTRUTH_ENABLED", ""),
    "semseg_enabled": os.environ.get("CASE_SEMSEG_ENABLED", ""),
    "luma_adaptation_enabled": os.environ.get("CASE_LUMA_ADAPTATION_ENABLED", ""),
    "adaptive_luma_adaptation_enabled": os.environ.get("CASE_ADAPTIVE_LUMA_ADAPTATION_ENABLED", ""),
    "luma_downscale_factor": os.environ.get("CASE_LUMA_DOWNSCALE_FACTOR", ""),
    "encoder_config_file": os.environ.get("CASE_ENCODER_CONFIG_FILE", ""),
    "encoder_config_base_dir": os.environ.get("CASE_ENCODER_CONFIG_BASE_DIR", ""),
    "encoder_inference_config_file": os.environ.get("CASE_ENCODER_INFERENCE_CONFIG_FILE", ""),
    "encoder_inference_enabled": os.environ.get("CASE_ENCODER_INFERENCE_ENABLED", ""),
    "rtp_config_file": os.environ.get("CASE_RTP_CONFIG_FILE", ""),
    "rtp_config_base_dir": os.environ.get("CASE_RTP_CONFIG_BASE_DIR", ""),
    "rtp_inference_config_file": os.environ.get("CASE_RTP_INFERENCE_CONFIG_FILE", ""),
    "rtp_inference_enabled": os.environ.get("CASE_RTP_INFERENCE_ENABLED", ""),
    "groundtruth_file": os.environ.get("CASE_GROUNDTRUTH_FILE", ""),
    "semseg_groundtruth_file": os.environ.get("CASE_SEMSEG_GROUNDTRUTH_FILE", ""),
    "semseg_config_resolution_source": os.environ.get("CASE_SEMSEG_CONFIG_RESOLUTION_SOURCE", ""),
}
path = Path(os.environ["CASE_METADATA_FILE"])
path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
PY

    echo ""
    echo "================================================================"
    echo "=== Running case: ${case_name}"
    echo "===   QP=$qp_value  mode=$mode  blur=$blur_enabled  pack_rois=$pack_rois_enabled  tet=$tet_enabled  luma=$luma_adaptation_enabled  adaptive_luma=$adaptive_luma_adaptation_enabled  luma_factor=$luma_downscale_factor  encoder_gt=$encoder_groundtruth_enabled  semseg=$semseg_enabled"
    echo "================================================================"

    if [ "$mode" = "qp_map_delta" ]; then
        mode_args=(
            --roi-background-mode qp_map_delta
            --roi-pack-rois false
            --roi-qp-map-background-delta "$QP_MAP_BACKGROUND_DELTA"
        )
    else
        local background_mode_arg="off"
        if [ "$blur_enabled" = "true" ]; then
            background_mode_arg="background_blur"
        fi
        mode_args=(
            --roi-background-mode "$background_mode_arg"
            --roi-pack-rois "$pack_rois_enabled"
        )
    fi

    # Make sure port 5000 is free from any previous run
    ensure_port_free 5000

    # Remove stale encoder output so we don't confuse old results with new ones
    rm -f "${ENCODER_DIR}/encoder_output.txt"
    # Remove stale H.265 output file so bitrate measurement is accurate
    rm -f "${ENCODER_DIR}/../yolo/yolo_output_with_sei.h265"
    rm -f "${ENCODER_DIR}/../yolo/yolo_output_with_sei_packed.h265"

    # ---- Start rtp_server ----
    # We need the rtp_server runtime environment (GStreamer, DeepStream, YOLO, CUDA).
    # Source it in a subshell so it doesn't pollute the encoder's environment,
    # then exec the binary so the PID we track IS the rtp_server binary — making
    # signal delivery (kill -INT) reliable.
    local server_log="${case_dir}/rtp_server_output_${case_name}.txt"
    (
        cd "$RTP_SERVER_DIR"
        source scripts/common_rtp_server_setup.sh
        setup_environment
        if [ "$semseg_enabled" = "true" ]; then
            export SF_TET_COMPARISON_SEMSEG_MODE=1
        else
            unset SF_TET_COMPARISON_SEMSEG_MODE
        fi
        RTP_SERVER_BINARY="$(find_rtp_server_binary)" || {
            echo "ERROR: Could not find rtp_server binary" >&2
            exit 1
        }
        exec "$RTP_SERVER_BINARY" \
            --server-transport-type rtp \
            --config "$temp_config" \
            --inference-labels "$GROUNDTRUTH_LABELS" \
            "${rtp_server_test_mode_args[@]}" \
            "${rtp_server_extra_args[@]}"
    ) > "$server_log" 2>&1 &
    RTP_SERVER_PID=$!
    echo "rtp_server started (pid $RTP_SERVER_PID)"

    # Wait for the server to be ready before starting the encoder
    local server_ready_timeout=60
    if ! wait_for_server_ready "$server_log" "$server_ready_timeout" "$RTP_SERVER_PID"; then
        echo "ERROR: rtp_server failed to start for ${case_name}. Skipping."
        shutdown_rtp "$RTP_SERVER_PID"
        RTP_SERVER_PID=""
        return 1
    fi

    # Additional small delay to let things settle
    sleep "$SLEEP_BEFORE_ENCODER"

    # ---- Run encoder_cli ----
    # Use COCO groundtruth boxes as ROIs so encoding/TET changes are isolated
    # from detector variability.
    echo "Starting encoder_cli for ${case_name} (QP=${qp_value})..."
    local encoder_exit_code=0
    local case_encoder_timeout="$ENCODER_TIMEOUT"
    if [ "$mode" != "qp_map_delta" ]; then
        case_encoder_timeout="$ENCODER_TIMEOUT_LEGACY"
    fi
    local case_no_marker_timeout="$ENCODER_NO_MARKER_TIMEOUT"
    local shutdown_elapsed=0
    local total_wait_elapsed=0
    local shutdown_seen=false
    local shutdown_marker=""
    local tolerate_watchdog_qp_map_shutdown_timeout=false
    pushd "$ENCODER_DIR" >/dev/null
    export NO_BUILD=true
    local -a ROI_ARGS=()
    if [ "$encoder_groundtruth_enabled" = "true" ]; then
        ROI_ARGS=(--roi-groundtruth-file "$GROUNDTRUTH_FILE")
    fi
    setsid ./run.sh file rtp "$INPUT_FILE" -- \
        --config "$temp_encoder_config" \
        --encoder-qp "$qp_value" \
        "${ROI_ARGS[@]}" \
        "${mode_args[@]}" \
        --background-color 0 \
        --debug-enable-h265-branch \
        "${encoder_extra_args[@]}" &
    ENCODER_RUN_PID=$!
    echo "encoder_cli started (pgid ${ENCODER_RUN_PID})"

    set +e
    echo "Timeout policy: marker timeout ${case_encoder_timeout}s; no-marker timeout ${case_no_marker_timeout}s."
    while kill -0 "$ENCODER_RUN_PID" 2>/dev/null; do
        sleep 1
        total_wait_elapsed=$((total_wait_elapsed + 1))

        if [ "$shutdown_seen" = false ] && [ -f "${ENCODER_DIR}/encoder_output.txt" ]; then
            if grep -Fq "global_data::shutdown begin" "${ENCODER_DIR}/encoder_output.txt"; then
                shutdown_seen=true
                shutdown_marker="global_data::shutdown begin"
            elif grep -Fq "ROI processing shutdown" "${ENCODER_DIR}/encoder_output.txt"; then
                shutdown_seen=true
                shutdown_marker="ROI processing shutdown"
            elif grep -Fq "Application shutdown initiated" "${ENCODER_DIR}/encoder_output.txt"; then
                shutdown_seen=true
                shutdown_marker="Application shutdown initiated"
            elif grep -Fq "Triggering main-thread controlled global_data::shutdown" "${ENCODER_DIR}/encoder_output.txt"; then
                shutdown_seen=true
                shutdown_marker="File mode controlled shutdown trigger"
            fi

            if [ "$shutdown_seen" = true ]; then
                shutdown_elapsed=0
                echo "encoder_cli shutdown marker detected (${shutdown_marker}); timeout window begins now (${case_encoder_timeout}s)."
            fi
        fi

        if [ "$shutdown_seen" = true ]; then
            shutdown_elapsed=$((shutdown_elapsed + 1))
            if [ "$shutdown_elapsed" -ge "$case_encoder_timeout" ]; then
                break
            fi
        elif [ "$total_wait_elapsed" -ge "$case_no_marker_timeout" ]; then
            break
        fi
    done

    if kill -0 "$ENCODER_RUN_PID" 2>/dev/null; then
        encoder_exit_code=124
        local known_watchdog_qp_map_timeout=false
        if [ "$mode" = "qp_map_delta" ] && [ "$shutdown_seen" = true ] && \
           [ "$shutdown_marker" = "Application shutdown initiated" ] && \
           [ -f "${ENCODER_DIR}/encoder_output.txt" ] && \
           grep -Fq "File-mode shutdown watchdog forcing cleanup" "${ENCODER_DIR}/encoder_output.txt"; then
            known_watchdog_qp_map_timeout=true
            tolerate_watchdog_qp_map_shutdown_timeout=true
        fi

        if [ "$shutdown_seen" = true ]; then
            if [ "$known_watchdog_qp_map_timeout" = true ]; then
                echo "WARNING: encoder_cli shutdown exceeded ${case_encoder_timeout}s for ${case_name} (marker: ${shutdown_marker}); treating as known qp_map_delta watchdog path."
            else
                echo "ERROR: encoder_cli shutdown timed out after ${case_encoder_timeout}s for ${case_name} (marker: ${shutdown_marker})."
            fi
        else
            echo "ERROR: encoder_cli timed out before shutdown marker appeared for ${case_name} after ${total_wait_elapsed}s."
        fi
        if [ "$known_watchdog_qp_map_timeout" = true ]; then
            echo "WARNING: Detected known qp_map_delta watchdog shutdown hang after marker for ${case_name}."
            echo "WARNING: Forcing encoder stop and continuing because outputs are already finalized."
        fi
        stop_encoder_process_group "$ENCODER_RUN_PID"
        if [ "$tolerate_watchdog_qp_map_shutdown_timeout" = true ]; then
            encoder_exit_code=0
        fi
    else
        wait "$ENCODER_RUN_PID"
        encoder_exit_code=$?
    fi
    set -e
    ENCODER_RUN_PID=""
    unset NO_BUILD
    popd >/dev/null

    if [ "$encoder_exit_code" -ne 0 ]; then
        if [ "$encoder_exit_code" -ne 124 ]; then
            echo "ERROR: encoder_cli exited with code ${encoder_exit_code} for ${case_name}."
        fi
        shutdown_rtp "$RTP_SERVER_PID"
        RTP_SERVER_PID=""
        local encoder_log="${case_dir}/encoder_output_${case_name}.txt"
        [ -f "${ENCODER_DIR}/encoder_output.txt" ] && \
            cp "${ENCODER_DIR}/encoder_output.txt" "$encoder_log"
        echo "ERROR: Aborting run due to encoder failure in ${case_name} at QP ${qp_value}."
        return 1
    fi

    # Copy encoder log immediately after encoder exits so semseg frame-drain
    # checks can derive expected frame counts from this case run.
    local encoder_log="${case_dir}/encoder_output_${case_name}.txt"
    [ -f "${ENCODER_DIR}/encoder_output.txt" ] && \
        cp "${ENCODER_DIR}/encoder_output.txt" "$encoder_log"

    # ---- Wait for the JSON result file ----
    # The encoder sends a disconnect request on exit, which triggers
    # cleanup_client_by_port() in the server and writes the JSON summary.
    # If that doesn't happen, the session monitor will time out (30 s) and
    # write it.  We poll for the file.
    if [ "$semseg_enabled" != "true" ]; then
        echo "Encoder finished. Waiting up to ${RESULT_WAIT_TIMEOUT}s for test mode results..."
        local waited=0
        while [ "$waited" -lt "$RESULT_WAIT_TIMEOUT" ]; do
            if [ -f "$expected_summary_json" ]; then
                echo "JSON summary found after ${waited}s."
                break
            fi
            # Also check if the server died unexpectedly
            if ! kill -0 "$RTP_SERVER_PID" 2>/dev/null; then
                echo "rtp_server exited before producing results."
                break
            fi
            sleep 2
            waited=$((waited + 2))
        done
        if [ "$waited" -ge "$RESULT_WAIT_TIMEOUT" ]; then
            echo "WARNING: Timed out waiting for ${expected_summary_json}."
        fi
    fi

    local semseg_frames_dir=""
    local semseg_expected_frames=0
    local semseg_captured_frames=0
    local semseg_capture_ratio="1.000000"
    local semseg_capture_state="n/a"
    if [ "$semseg_enabled" = "true" ]; then
        semseg_frames_dir="$(discover_semseg_frames_directory "$rtp_server_frames_dir")"
        if [ -z "$semseg_frames_dir" ]; then
            echo "ERROR: Could not find semseg debug frames directory under ${rtp_server_frames_dir}/debug_frames" >&2
            shutdown_rtp "$RTP_SERVER_PID"
            RTP_SERVER_PID=""
            return 1
        fi

        semseg_expected_frames="$(extract_expected_frame_count_from_encoder_log "$encoder_log")"
        if [ "$semseg_expected_frames" -le 0 ]; then
            echo "WARNING: Could not derive expected frame count from encoder log; waiting for semseg frame export stabilization."
        else
            echo "Semseg expected frame count from encoder log: ${semseg_expected_frames}"
        fi

        local drain_result=""
        drain_result="$(wait_for_semseg_frame_drain "$semseg_frames_dir" "$semseg_expected_frames" "$SEMSEG_FRAME_DRAIN_TIMEOUT")"
        semseg_captured_frames="$(echo "$drain_result" | awk '{print $1}')"
        local semseg_drain_reason
        semseg_drain_reason="$(echo "$drain_result" | awk '{print $2}')"
        local semseg_drain_elapsed
        semseg_drain_elapsed="$(echo "$drain_result" | awk '{print $3}')"
        echo "Semseg frame-drain pre-shutdown: captured=${semseg_captured_frames} expected=${semseg_expected_frames} reason=${semseg_drain_reason} elapsed=${semseg_drain_elapsed}s"

        # For semseg battery runs, allow graceful rtp_server stop so multifilesink can flush.
        shutdown_rtp "$RTP_SERVER_PID"
        RTP_SERVER_PID=""

        semseg_captured_frames="$(count_semseg_png_frames "$semseg_frames_dir")"
        semseg_capture_ratio="$(calculate_frame_capture_ratio "$semseg_captured_frames" "$semseg_expected_frames")"
        semseg_capture_state="complete"
        if [ "$semseg_expected_frames" -gt 0 ] && ! python3 - "$semseg_capture_ratio" "$SEMSEG_MIN_CAPTURE_RATIO" <<'PY'
import sys
ratio = float(sys.argv[1])
threshold = float(sys.argv[2])
raise SystemExit(0 if ratio >= threshold else 1)
PY
        then
            semseg_capture_state="incomplete"
        fi
    else
        # ---- Shut down the server ----
        kill_rtp_immediately "$RTP_SERVER_PID"
        RTP_SERVER_PID=""
    fi

    if [ "$semseg_enabled" = "true" ]; then
        local semseg_port="5000"
        if [[ "$(basename "$semseg_frames_dir")" =~ ^port_([0-9]+)$ ]]; then
            semseg_port="${BASH_REMATCH[1]}"
        fi

        rm -f "$semseg_summary_json" "$semseg_summary_md"
        echo "Semseg capture summary: captured=${semseg_captured_frames} expected=${semseg_expected_frames} ratio=${semseg_capture_ratio} threshold=${SEMSEG_MIN_CAPTURE_RATIO} state=${semseg_capture_state}"
        if [ "$semseg_capture_state" = "incomplete" ]; then
            echo "WARNING: Incomplete semseg frame capture for ${case_name}: captured=${semseg_captured_frames}, expected=${semseg_expected_frames}, ratio=${semseg_capture_ratio}"
        fi
        echo "Running AH0187 semseg evaluator on frames: $semseg_frames_dir"
        if ! run_ah0187_semseg_evaluator "$semseg_frames_dir" "$SEMSEG_GROUNDTRUTH_FILE" \
            "$semseg_summary_json" "$semseg_summary_md" "$semseg_port"; then
            echo "ERROR: AH0187 semseg evaluation failed for ${case_name}." >&2
            return 1
        fi

        if [ -f "$semseg_summary_json" ]; then
            python3 - "$semseg_summary_json" "$semseg_expected_frames" "$semseg_captured_frames" \
                "$semseg_capture_ratio" "$SEMSEG_MIN_CAPTURE_RATIO" "$semseg_capture_state" <<'PY'
import json
import pathlib
import sys

summary_path = pathlib.Path(sys.argv[1])
expected_frames = int(sys.argv[2])
captured_frames = int(sys.argv[3])
capture_ratio = float(sys.argv[4])
min_ratio = float(sys.argv[5])
capture_state = sys.argv[6]

payload = json.loads(summary_path.read_text(encoding="utf-8"))
payload["capture_expected_frames"] = expected_frames
payload["capture_observed_frames"] = captured_frames
payload["capture_ratio"] = capture_ratio
payload["capture_min_ratio_threshold"] = min_ratio
payload["capture_state"] = capture_state
summary_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
PY
        fi

        if [ "$semseg_capture_state" = "incomplete" ] && [ "$SEMSEG_INCOMPLETE_CAPTURE_POLICY" = "fail" ]; then
            echo "ERROR: Semseg capture ratio ${semseg_capture_ratio} is below threshold ${SEMSEG_MIN_CAPTURE_RATIO} (policy=fail)." >&2
            return 1
        fi
    fi

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
    if ! measure_bitrate "$case_dir" "$encoder_log" "$server_log" "$case_fps"; then
        echo "ERROR: Aborting run because bitrate measurement failed for ${case_name} at QP ${qp_value}."
        return 1
    fi

    echo "=== Completed case: ${case_name} ==="
}

# ---------------------------------------------------------------------------
# Run test cases: for each QP, run baseline + one-mode-only cases.
# ---------------------------------------------------------------------------
TOTAL_QP_COUNT=0
for _ in $QP_VALUES; do TOTAL_QP_COUNT=$((TOTAL_QP_COUNT + 1)); done
TOTAL_CASES=0
if [ "$RUN_BBOX" = "true" ]; then
    TOTAL_CASES=$((TOTAL_CASES + TOTAL_QP_COUNT * 7))
fi
if [ "$RUN_SEMSEG" = "true" ]; then
    TOTAL_CASES=$((TOTAL_CASES + TOTAL_QP_COUNT * 7))
fi

echo ""
echo "========================================"
echo "Starting TET comparison"
echo "Mode: ${RUN_MODE}"
echo "QP values: $QP_VALUES  (${TOTAL_CASES} planned cases)"
echo "Output directory: $OUTPUT_DIR"
echo "========================================"

for qp in $QP_VALUES; do
    echo ""
    echo "########################################"
    echo "### QP = ${qp}"
    echo "########################################"

    if [ "$RUN_BBOX" = "true" ]; then
        run_case "baseline_all_off"      "$qp" "legacy"       "false" "false" "false" "true" "false" "false" "1.0"
        run_case "mode_roi_packing"      "$qp" "legacy"       "false" "true"  "false" "true" "false" "false" "1.0"
        run_case "mode_background_blur"  "$qp" "legacy"       "true"  "false" "false" "true" "false" "false" "1.0"
        run_case "mode_qp_map"           "$qp" "qp_map_delta" "false" "false" "false" "true" "false" "false" "1.0"
        run_case "mode_tet"              "$qp" "legacy"       "false" "false" "true"  "true" "false" "false" "1.0"
        run_case "mode_luma_scaling"     "$qp" "legacy"       "false" "false" "false" "true" "false" "true"  "0.5" "false"
        run_case "mode_adaptive_luma_scaling" "$qp" "legacy"  "false" "false" "false" "true" "false" "true"  "1.0" "true"
    fi

    if [ "$RUN_SEMSEG" = "true" ]; then
        run_case "semseg_baseline_all_off"      "$qp" "legacy"       "false" "false" "false" "true" "true" "false" "1.0"
        run_case "semseg_mode_roi_packing"      "$qp" "legacy"       "false" "true"  "false" "true" "true" "false" "1.0"
        run_case "semseg_mode_background_blur"  "$qp" "legacy"       "true"  "false" "false" "true" "true" "false" "1.0"
        run_case "semseg_mode_qp_map"           "$qp" "qp_map_delta" "false" "false" "false" "true" "true" "false" "1.0"
        run_case "semseg_mode_tet"              "$qp" "legacy"       "false" "false" "true"  "true" "true" "false" "1.0"
        run_case "semseg_mode_luma_scaling"     "$qp" "legacy"       "false" "false" "false" "true" "true" "true"  "0.5" "false"
        run_case "semseg_mode_adaptive_luma_scaling" "$qp" "legacy"  "false" "false" "false" "true" "true" "true"  "1.0" "true"
    fi
done

# ---------------------------------------------------------------------------
# Aggregate results into CSV / Markdown
# ---------------------------------------------------------------------------
if [ "$RUN_BBOX" = "true" ]; then
OUTPUT_DIR_ENV="$OUTPUT_DIR" QP_VALUES_ENV="$QP_VALUES" python3 - <<'PY'
import json
import os
import configparser
import re
from pathlib import Path

output_dir = Path(os.environ["OUTPUT_DIR_ENV"])
qp_values = os.environ.get("QP_VALUES_ENV", "22 27 32 37 42 47").split()

test_cases = [
    "baseline_all_off",
    "mode_roi_packing",
    "mode_background_blur",
    "mode_qp_map",
    "mode_tet",
    "mode_luma_scaling",
    "mode_adaptive_luma_scaling",
]
qp_map_delta_cases = ["mode_qp_map"]
contract_json_prefix = "CONTRACT_JSON:"
contract_schema_version = "encoder_backend_contract/v1"
contract_required_keys = (
    "schema",
    "requested_background_mode",
    "effective_background_mode",
    "direct_qp_map_backend",
)

summary_pattern = re.compile(
    r"Test mode summary for port \d+: frames=(\d+)\s+"
    r"gt_total=(\d+)\s+det_total=(\d+)\s+"
    r"tp=(\d+)\s+fp=(\d+)\s+fn=(\d+)\s+"
    r"precision=([0-9.]+)\s+recall=([0-9.]+)\s+"
    r"f1=([0-9.]+)\s+mean_iou=([0-9.]+)(?:\s+map_50=([0-9.]+))?"
)

frame_pattern = re.compile(
    r"Test mode frame (\d+):\s+gt=(\d+)\s+det=(\d+)\s+"
    r"tp=(\d+)\s+fp=(\d+)\s+fn=(\d+)\s+"
    r"precision=([0-9.]+)\s+recall=([0-9.]+)\s+"
    r"f1=([0-9.]+)\s+mean_iou=([0-9.]+)"
)


def clean_value(raw):
    value = (raw or "").strip()
    for token in (" #", " ;", "\t#", "\t;"):
        idx = value.find(token)
        if idx != -1:
            value = value[:idx]
    return value.strip().strip('"').strip("'")


def parse_bool(raw):
    return clean_value(raw).lower() in {"1", "true", "yes", "on"}


def resolve_path(path_text, base_dir):
    path_text = clean_value(path_text)
    if not path_text:
        return ""
    candidate = Path(path_text)
    if candidate.is_absolute():
        return str(candidate)
    return str((Path(base_dir) / candidate).resolve())


def read_ini_value(config_path, section, key, default=""):
    if not config_path:
        return default
    path = Path(config_path)
    if not path.exists():
        return default
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    try:
        parser.read(path, encoding="utf-8")
    except Exception:
        return default
    for sec in parser.sections():
        if sec.lower() != section.lower():
            continue
        for opt, value in parser.items(sec):
            if opt.lower() == key.lower():
                return clean_value(value)
    return default


def parse_key_value_file(config_path):
    kv = {}
    path = Path(config_path)
    if not path.exists():
        return kv
    text = path.read_text(errors="ignore")
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith(";") or line.startswith("["):
            continue
        match = re.match(r"([A-Za-z0-9_.-]+)\s*[:=]\s*(.+)$", line)
        if not match:
            continue
        key = match.group(1).strip().lower()
        if key in kv:
            continue
        kv[key] = clean_value(match.group(2))
    return kv


def normalize_network_type(value):
    raw = clean_value(value)
    lower = raw.lower()
    if raw == "2" or "segmentation" in lower or "semseg" in lower:
        return "segmentation"
    if raw == "0" or "detection" in lower:
        return "detection"
    if raw == "1" or "classifier" in lower:
        return "classification"
    return raw or "unknown"


def describe_model_usage(deepstream_config_file):
    info = {
        "deepstream_config_file": deepstream_config_file or "",
        "nvinfer_config_file": "",
        "model_engine_file": "",
        "onnx_file": "",
        "network_type": "unknown",
        "model_hint": "none",
        "model_artifact": "none",
    }
    if not deepstream_config_file:
        return info

    ds_path = Path(deepstream_config_file)
    if not ds_path.exists():
        return info

    ds_kv = parse_key_value_file(ds_path)
    nvinfer_rel = ds_kv.get("config-file-path", "")
    nvinfer_path = Path(resolve_path(nvinfer_rel, ds_path.parent)) if nvinfer_rel else ds_path
    info["nvinfer_config_file"] = str(nvinfer_path)

    nvinfer_kv = parse_key_value_file(nvinfer_path)
    nvinfer_dir = nvinfer_path.parent

    model_engine = resolve_path(nvinfer_kv.get("model-engine-file", ""), nvinfer_dir)
    onnx_file = resolve_path(nvinfer_kv.get("onnx-file", ""), nvinfer_dir)
    tlt_model = resolve_path(nvinfer_kv.get("tlt-encoded-model", ""), nvinfer_dir)
    caffemodel = resolve_path(nvinfer_kv.get("model-file", ""), nvinfer_dir)
    custom_network = resolve_path(nvinfer_kv.get("custom-network-config", ""), nvinfer_dir)

    artifact = model_engine or onnx_file or tlt_model or caffemodel or custom_network
    info["model_engine_file"] = model_engine
    info["onnx_file"] = onnx_file
    info["network_type"] = normalize_network_type(nvinfer_kv.get("network-type", ""))
    if artifact:
        info["model_artifact"] = artifact
        info["model_hint"] = Path(artifact).name
    else:
        info["model_hint"] = Path(ds_path).name
    return info


def resolve_existing_path(path_text, *base_dirs):
    path_text = clean_value(path_text)
    if not path_text:
        return ""

    candidate = Path(path_text)
    if candidate.is_absolute() and candidate.exists():
        return str(candidate)

    for base in base_dirs:
        base_text = clean_value(str(base)) if base else ""
        if not base_text:
            continue
        resolved = Path(resolve_path(path_text, base_text))
        if resolved.exists():
            return str(resolved)

    if candidate.is_absolute():
        return str(candidate)

    for base in base_dirs:
        base_text = clean_value(str(base)) if base else ""
        if not base_text:
            continue
        return str((Path(base_text) / path_text).resolve())
    return path_text


def is_placeholder(value):
    return clean_value(value).lower() in {"", "none", "unknown", "n/a"}


def normalize_model_summary_fields(row, prefix):
    engine_enabled = clean_value(row.get(f"{prefix}_inference_engine", "")).lower() == "true"
    hint_key = f"{prefix}_model_hint"
    artifact_key = f"{prefix}_model_artifact"
    network_key = f"{prefix}_network_type"

    if not engine_enabled:
        row[hint_key] = "n/a"
        row[artifact_key] = "n/a"
        row[network_key] = "n/a"
        return

    if is_placeholder(row.get(hint_key, "")):
        row[hint_key] = "n/a"
    if is_placeholder(row.get(artifact_key, "")):
        row[artifact_key] = "n/a"
    if is_placeholder(row.get(network_key, "")):
        row[network_key] = "n/a"


def load_case_metadata(case_dir):
    path = Path(case_dir) / "case_metadata.json"
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text())
    except Exception:
        return {}


def collect_case_runtime_info(case_dir):
    metadata = load_case_metadata(case_dir)
    semseg_enabled = parse_bool(metadata.get("semseg_enabled", "false"))
    luma_adaptation_enabled = parse_bool(metadata.get("luma_adaptation_enabled", "false"))
    adaptive_luma_adaptation_enabled = parse_bool(metadata.get("adaptive_luma_adaptation_enabled", "false"))
    luma_downscale_factor = clean_value(metadata.get("luma_downscale_factor", ""))
    if not luma_adaptation_enabled:
        luma_downscale_factor = "n/a"
    elif adaptive_luma_adaptation_enabled:
        luma_downscale_factor = "n/a"
    elif not luma_downscale_factor:
        luma_downscale_factor = "0.5"

    encoder_config = metadata.get("encoder_config_file", "")
    encoder_base_dir = metadata.get("encoder_config_base_dir", "")
    if "encoder_inference_enabled" in metadata:
        encoder_inference_enabled = parse_bool(metadata.get("encoder_inference_enabled", "false"))
    else:
        encoder_inference_enabled = parse_bool(read_ini_value(encoder_config, "ROI", "inference_engine", "false"))
    encoder_deepstream = resolve_existing_path(
        metadata.get("encoder_inference_config_file", ""),
        encoder_base_dir,
        Path(encoder_config).parent if encoder_config else "",
        output_dir,
    )
    if not encoder_deepstream:
        encoder_deepstream = resolve_existing_path(
            read_ini_value(encoder_config, "ROI", "deepstream_config_file", ""),
            encoder_base_dir,
            Path(encoder_config).parent if encoder_config else "",
            output_dir,
        )
    encoder_model = describe_model_usage(encoder_deepstream)

    rtp_config = metadata.get("rtp_config_file", "")
    rtp_base_dir = metadata.get("rtp_config_base_dir", "")
    if "rtp_inference_enabled" in metadata:
        rtp_inference_enabled = parse_bool(metadata.get("rtp_inference_enabled", "false"))
    else:
        rtp_inference_enabled = parse_bool(read_ini_value(rtp_config, "Inference", "inference_engine", "false"))
    rtp_deepstream = resolve_existing_path(
        metadata.get("rtp_inference_config_file", ""),
        rtp_base_dir,
        Path(rtp_config).parent if rtp_config else "",
        output_dir,
    )
    if not rtp_deepstream:
        rtp_deepstream = resolve_existing_path(
            read_ini_value(rtp_config, "Inference", "deepstream_config_file", ""),
            rtp_base_dir,
            Path(rtp_config).parent if rtp_config else "",
            output_dir,
        )
    rtp_model = describe_model_usage(rtp_deepstream)

    result = {
        "encoder_inference_engine": "true" if encoder_inference_enabled else "false",
        "encoder_deepstream_config_file": encoder_model.get("deepstream_config_file", ""),
        "encoder_nvinfer_config_file": encoder_model.get("nvinfer_config_file", ""),
        "encoder_model_engine_file": encoder_model.get("model_engine_file", ""),
        "encoder_onnx_file": encoder_model.get("onnx_file", ""),
        "encoder_model_artifact": encoder_model.get("model_artifact", "none"),
        "encoder_network_type": encoder_model.get("network_type", "unknown"),
        "encoder_model_hint": encoder_model.get("model_hint", "none"),
        "rtp_inference_engine": "true" if rtp_inference_enabled else "false",
        "rtp_deepstream_config_file": rtp_model.get("deepstream_config_file", ""),
        "rtp_nvinfer_config_file": rtp_model.get("nvinfer_config_file", ""),
        "rtp_model_engine_file": rtp_model.get("model_engine_file", ""),
        "rtp_onnx_file": rtp_model.get("onnx_file", ""),
        "rtp_model_artifact": rtp_model.get("model_artifact", "none"),
        "rtp_network_type": rtp_model.get("network_type", "unknown"),
        "rtp_model_hint": rtp_model.get("model_hint", "none"),
        "luma_adaptation_enabled": "true" if luma_adaptation_enabled else "false",
        "adaptive_luma_adaptation_enabled": "true" if adaptive_luma_adaptation_enabled else "false",
        "luma_downscale_factor": luma_downscale_factor,
        "semseg_config_resolution_source": metadata.get("semseg_config_resolution_source", "") or "n/a",
    }
    if semseg_enabled:
        normalize_model_summary_fields(result, "encoder")
        normalize_model_summary_fields(result, "rtp")
    return result


def parse_metrics_from_log(log_path):
    """Parse test mode metrics from rtp_server log as fallback."""
    if not log_path.exists():
        return None
    log_text = log_path.read_text(errors="ignore")

    # Try bbox summary line first
    match = summary_pattern.search(log_text)
    if match:
        return {
            "status": "ok_log",
            "frames_with_groundtruth": int(match.group(1)),
            "groundtruth_total": int(match.group(2)),
            "detection_total": int(match.group(3)),
            "true_positive": int(match.group(4)),
            "false_positive": int(match.group(5)),
            "false_negative": int(match.group(6)),
            "precision": float(match.group(7)),
            "recall": float(match.group(8)),
            "f1_score": float(match.group(9)),
            "mean_iou": float(match.group(10)),
            "map_50": float(match.group(11)) if match.group(11) is not None else None,
            "iou_threshold": None,
        }

    # Fall back to per-frame aggregation.
    frame_matches = frame_pattern.findall(log_text)

    if not frame_matches:
        return None

    frames_seen = set()
    gt_total = det_total = tp = fp = fn = 0
    total_iou = 0.0
    for m in frame_matches:
        fi = int(m[0])
        if fi in frames_seen:
            continue
        frames_seen.add(fi)
        gt_total += int(m[1])
        det_total += int(m[2])
        t, f_p, f_n = int(m[3]), int(m[4]), int(m[5])
        tp += t; fp += f_p; fn += f_n
        total_iou += float(m[9]) * t

    precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
    recall = tp / (tp + fn) if (tp + fn) > 0 else 0.0
    f1 = (2 * precision * recall / (precision + recall)) if (precision + recall) > 0 else 0.0
    miou = total_iou / tp if tp > 0 else 0.0

    return {
        "status": "ok_log_frames",
        "frames_with_groundtruth": len(frames_seen),
        "groundtruth_total": gt_total,
        "detection_total": det_total,
        "true_positive": tp,
        "false_positive": fp,
        "false_negative": fn,
        "precision": precision,
        "recall": recall,
        "f1_score": f1,
        "mean_iou": miou,
        "map_50": None,
        "iou_threshold": None,
    }


def parse_qp_map_backend_contract(encoder_log_path):
    """
    Parse the stable CONTRACT_JSON line emitted by encoder_cli.

    NOTE: This parser is part of the qp_map summary inclusion contract.
    Keep it backward compatible when evolving encoder-side fields.
    """
    if not encoder_log_path.exists():
        return {"state": "missing", "reason": "encoder log is missing"}

    contract_payloads = []
    for line in encoder_log_path.read_text(errors="ignore").splitlines():
        if contract_json_prefix not in line:
            continue
        _, payload = line.split(contract_json_prefix, 1)
        payload = payload.strip()
        if payload:
            contract_payloads.append(payload)

    if not contract_payloads:
        return {"state": "missing", "reason": "CONTRACT_JSON line not found"}
    if len(contract_payloads) != 1:
        return {
            "state": "invalid",
            "reason": f"expected exactly 1 CONTRACT_JSON line, found {len(contract_payloads)}",
        }

    payload_text = contract_payloads[0]
    try:
        payload = json.loads(payload_text)
    except Exception as exc:
        return {"state": "invalid", "reason": f"invalid CONTRACT_JSON payload: {exc}"}

    missing_keys = [key for key in contract_required_keys if key not in payload]
    if missing_keys:
        return {
            "state": "invalid",
            "reason": f"missing required CONTRACT_JSON key(s): {', '.join(missing_keys)}",
        }

    schema_value = clean_value(str(payload.get("schema", "")))
    if schema_value != contract_schema_version:
        return {
            "state": "invalid",
            "reason": (
                f"unsupported CONTRACT_JSON schema '{schema_value}' "
                f"(expected '{contract_schema_version}')"
            ),
        }

    requested_mode = clean_value(str(payload.get("requested_background_mode", "")))
    effective_mode = clean_value(str(payload.get("effective_background_mode", "")))
    direct_backend = parse_bool(str(payload.get("direct_qp_map_backend", "false")))
    fallback_reason = clean_value(str(payload.get("fallback_reason", "")))
    return {
        "state": "valid",
        "requested_background_mode": requested_mode,
        "effective_background_mode": effective_mode,
        "direct_qp_map_backend": direct_backend,
        "fallback_reason": fallback_reason,
    }


def direct_qp_map_backend_used_legacy(encoder_log_path):
    if not encoder_log_path.exists():
        return False
    log_text = encoder_log_path.read_text(errors="ignore")
    fallback_markers = [
        "falling back to background_blur",
        "Requested qp_map_delta is unsupported",
    ]
    if any(marker in log_text for marker in fallback_markers):
        return False

    direct_backend_markers = [
        "Using direct NvEncodeAPI backend for qp_map_delta mode",
        "Skipping appsrc EOS in file mode with direct NVENC backend to avoid shutdown hang",
        "ROI processing shutdown: skipping direct backend signal_eos in file mode",
    ]
    return any(marker in log_text for marker in direct_backend_markers)


# ---- Collect all rows ----
rows = []
for qp in qp_values:
    for case in test_cases:
        case_dir = output_dir / f"qp{qp}" / case
        json_path = case_dir / f"test_mode_{case}.json"
        log_path = case_dir / f"rtp_server_output_{case}.txt"
        bitrate_path = case_dir / "bitrate_bps.txt"

        # Read bitrate
        bitrate_bps = 0
        if bitrate_path.exists():
            try:
                bitrate_bps = int(bitrate_path.read_text().strip() or "0")
            except ValueError:
                bitrate_bps = 0

        # Try JSON first
        data = None
        if json_path.exists():
            try:
                raw = json.loads(json_path.read_text())
                data = {
                    "status": "ok_json",
                    "frames_with_groundtruth": raw.get("frames_with_groundtruth"),
                    "groundtruth_total": raw.get("groundtruth_total"),
                    "detection_total": raw.get("detection_total"),
                    "true_positive": raw.get("true_positive"),
                    "false_positive": raw.get("false_positive"),
                    "false_negative": raw.get("false_negative"),
                    "precision": raw.get("precision"),
                    "recall": raw.get("recall"),
                    "f1_score": raw.get("f1_score"),
                    "mean_iou": raw.get("mean_iou"),
                    "map_50": raw.get("map_50"),
                    "iou_threshold": raw.get("iou_threshold"),
                }
            except Exception:
                data = None

        # Fall back to log parsing
        if data is None:
            data = parse_metrics_from_log(log_path)

        if data is None:
            data = {"status": "missing"}

        data["qp"] = qp
        data["case"] = case
        data["bitrate_bps"] = bitrate_bps
        data.update(collect_case_runtime_info(case_dir))
        rows.append(data)

qp_map_delta_gate_failures = []
qp_map_delta_contract_errors = []
qp_map_delta_legacy_fallback_cases = []
for qp in qp_values:
    for case in qp_map_delta_cases:
        case_dir = output_dir / f"qp{qp}" / case
        encoder_log_path = case_dir / f"encoder_output_{case}.txt"
        case_label = f"qp={qp}:{case}"
        contract = parse_qp_map_backend_contract(encoder_log_path)
        if contract["state"] == "valid":
            if contract["requested_background_mode"] != "qp_map_delta":
                qp_map_delta_gate_failures.append(
                    f"{case_label} (requested={contract['requested_background_mode']})"
                )
                continue
            if (
                contract["effective_background_mode"] != "qp_map_delta"
                or not contract["direct_qp_map_backend"]
            ):
                fallback_suffix = ""
                if contract["fallback_reason"]:
                    fallback_suffix = f", fallback_reason={contract['fallback_reason']}"
                qp_map_delta_gate_failures.append(
                    f"{case_label} (effective={contract['effective_background_mode']}, "
                    f"direct={contract['direct_qp_map_backend']}{fallback_suffix})"
                )
            continue

        if contract["state"] == "invalid":
            qp_map_delta_contract_errors.append(f"{case_label} ({contract['reason']})")
            qp_map_delta_gate_failures.append(case_label)
            continue

        if direct_qp_map_backend_used_legacy(encoder_log_path):
            qp_map_delta_legacy_fallback_cases.append(case_label)
            continue

        qp_map_delta_gate_failures.append(case_label)

qp_map_delta_available = len(qp_map_delta_gate_failures) == 0
qp_map_delta_note = ""
qp_map_delta_fallback_note = ""
if not qp_map_delta_available:
    rows = [r for r in rows if r.get("case") not in qp_map_delta_cases]
    test_cases = [c for c in test_cases if c not in qp_map_delta_cases]
    failure_preview = ", ".join(qp_map_delta_gate_failures[:4])
    if len(qp_map_delta_gate_failures) > 4:
        failure_preview += ", ..."
    qp_map_delta_note = (
        "qp_map_delta mode was requested but direct NvEncodeAPI backend was not "
        "confirmed for all qp_map_delta cases. qp_map_delta results were excluded "
        f"from this summary (checks failed for: {failure_preview})."
    )
    if qp_map_delta_contract_errors:
        contract_error_preview = ", ".join(qp_map_delta_contract_errors[:3])
        if len(qp_map_delta_contract_errors) > 3:
            contract_error_preview += ", ..."
        qp_map_delta_note += (
            " CONTRACT_JSON validation failed for: "
            f"{contract_error_preview}."
        )

if qp_map_delta_legacy_fallback_cases:
    fallback_preview = ", ".join(qp_map_delta_legacy_fallback_cases[:4])
    if len(qp_map_delta_legacy_fallback_cases) > 4:
        fallback_preview += ", ..."
    qp_map_delta_fallback_note = (
        "CONTRACT_JSON was not present for some qp_map_delta cases, so legacy "
        f"log markers were used as fallback ({fallback_preview})."
    )


# ---- Write combined CSV ----
columns = [
    "qp",
    "case",
    "luma_adaptation_enabled",
    "adaptive_luma_adaptation_enabled",
    "luma_downscale_factor",
    "bitrate_bps",
    "status",
    "frames_with_groundtruth",
    "groundtruth_total",
    "detection_total",
    "true_positive",
    "false_positive",
    "false_negative",
    "precision",
    "recall",
    "f1_score",
    "map_50",
    "mean_iou",
    "iou_threshold",
    "encoder_inference_engine",
    "encoder_model_hint",
    "encoder_model_artifact",
    "encoder_network_type",
    "encoder_deepstream_config_file",
    "encoder_nvinfer_config_file",
    "encoder_model_engine_file",
    "encoder_onnx_file",
    "rtp_inference_engine",
    "rtp_model_hint",
    "rtp_model_artifact",
    "rtp_network_type",
    "rtp_deepstream_config_file",
    "rtp_nvinfer_config_file",
    "rtp_model_engine_file",
    "rtp_onnx_file",
    "semseg_config_resolution_source",
]


def fmt(value):
    if isinstance(value, float):
        return f"{value:.3f}"
    return "" if value is None else str(value)


csv_path = output_dir / "summary.csv"
csv_lines = [",".join(columns)]
for row in rows:
    csv_lines.append(",".join(fmt(row.get(col)) for col in columns))
csv_path.write_text("\n".join(csv_lines) + "\n")


# ---- Write Markdown ----
md_path = output_dir / "summary.md"
md_lines = ["# TET Comparison Summary", ""]
if qp_map_delta_note:
    md_lines.append(f"**Note:** {qp_map_delta_note}")
    md_lines.append("")
if qp_map_delta_fallback_note:
    md_lines.append(f"**Note:** {qp_map_delta_fallback_note}")
    md_lines.append("")

md_lines.append("## Runtime Config and Model Usage")
md_lines.append("")
model_cols = [
    "case",
    "luma_adaptation_enabled",
    "adaptive_luma_adaptation_enabled",
    "luma_downscale_factor",
    "encoder_inference_engine",
    "rtp_inference_engine",
    "semseg_config_resolution_source",
]
has_reliable_model_details = False
for row in rows:
    if clean_value(row.get("encoder_model_hint", "")).lower() not in {"", "n/a", "none"}:
        has_reliable_model_details = True
    if clean_value(row.get("encoder_model_artifact", "")).lower() not in {"", "n/a", "none"}:
        has_reliable_model_details = True
    if clean_value(row.get("rtp_model_hint", "")).lower() not in {"", "n/a", "none"}:
        has_reliable_model_details = True
    if clean_value(row.get("rtp_model_artifact", "")).lower() not in {"", "n/a", "none"}:
        has_reliable_model_details = True
if has_reliable_model_details:
    model_cols = [
        "case",
        "luma_adaptation_enabled",
        "adaptive_luma_adaptation_enabled",
        "luma_downscale_factor",
        "encoder_inference_engine",
        "encoder_model_hint",
        "encoder_model_artifact",
        "rtp_inference_engine",
        "rtp_model_hint",
        "rtp_model_artifact",
        "rtp_network_type",
        "semseg_config_resolution_source",
    ]
md_lines.append("| " + " | ".join(model_cols) + " |")
md_lines.append("| " + " | ".join(["---"] * len(model_cols)) + " |")
model_rows_seen = set()
for row in rows:
    row_key = tuple(row.get(col, "") for col in model_cols)
    if row_key in model_rows_seen:
        continue
    model_rows_seen.add(row_key)
    md_lines.append("| " + " | ".join(fmt(row.get(col)) for col in model_cols) + " |")
md_lines.append("")

# Build per-QP tables into a separate list; they will be appended after the
# executive summary so the text narrative comes first.
table_lines = []

for qp in qp_values:
    qp_rows = [r for r in rows if r["qp"] == qp]
    if not qp_rows:
        continue
    bitrate_str = ""
    br = qp_rows[0].get("bitrate_bps", 0)
    if br and br > 0:
        bitrate_str = f" (~{br / 1_000_000:.2f} Mbps)"
    table_lines.append(f"## QP {qp}{bitrate_str}")
    table_lines.append("")
    tbl_cols = ["case", "status", "precision", "recall", "f1_score", "map_50", "mean_iou",
                "bitrate_bps", "true_positive", "false_positive", "false_negative"]
    table_lines.append("| " + " | ".join(tbl_cols) + " |")
    table_lines.append("| " + " | ".join(["---"] * len(tbl_cols)) + " |")
    for r in qp_rows:
        table_lines.append("| " + " | ".join(fmt(r.get(c)) for c in tbl_cols) + " |")
    table_lines.append("")


# ---- Deltas: each single mode versus all-off baseline for each QP ----
def get_metric(row, key):
    v = row.get(key)
    return float(v) if isinstance(v, (int, float)) else None


def average_or_none(values):
    return sum(values) / len(values) if values else None


delta_metrics = [
    "precision", "recall", "f1_score", "map_50", "mean_iou",
    "true_positive", "false_positive", "false_negative",
    "groundtruth_total", "detection_total",
]

rows_by_key = {(r["qp"], r["case"]): r for r in rows}
baseline_case = "baseline_all_off"
mode_case_labels = [
    ("roi_packing_vs_baseline", "mode_roi_packing", "ROI packing"),
    ("background_blur_vs_baseline", "mode_background_blur", "Background blur"),
    ("qp_map_vs_baseline", "mode_qp_map", "QP map"),
    ("tet_vs_baseline", "mode_tet", "TET"),
    ("luma_scaling_vs_baseline", "mode_luma_scaling", "Luma scaling"),
    ("adaptive_luma_scaling_vs_baseline", "mode_adaptive_luma_scaling", "Adaptive luma scaling"),
]

delta_header = ["qp", "comparison", "base", "compare", "status", *delta_metrics]
delta_rows = []

for qp in qp_values:
    for label, cmp_case, _ in mode_case_labels:
        if cmp_case not in test_cases:
            continue
        base = rows_by_key.get((qp, baseline_case), {})
        compare = rows_by_key.get((qp, cmp_case), {})
        if not base.get("status", "").startswith("ok") or \
           not compare.get("status", "").startswith("ok"):
            delta_rows.append([qp, label, baseline_case, cmp_case, "missing"])
            continue
        deltas = []
        for key in delta_metrics:
            base_value = get_metric(base, key)
            compare_value = get_metric(compare, key)
            if base_value is None or compare_value is None:
                deltas.append("")
            else:
                deltas.append(f"{compare_value - base_value:.3f}")
        delta_rows.append([qp, label, baseline_case, cmp_case, "ok", *deltas])

table_lines.append("## Deltas (compare - baseline)")
table_lines.append("")
table_lines.append("| " + " | ".join(delta_header) + " |")
table_lines.append("| " + " | ".join(["---"] * len(delta_header)) + " |")
for row in delta_rows:
    table_lines.append("| " + " | ".join(str(v) for v in row) + " |")
table_lines.append("")

delta_csv_path = output_dir / "summary_deltas.csv"
delta_csv_lines = [",".join(delta_header)]
for row in delta_rows:
    delta_csv_lines.append(",".join(str(v) for v in row))
delta_csv_path.write_text("\n".join(delta_csv_lines) + "\n")


# ---------------------------------------------------------------------------
# Generate a plain-text executive summary suitable for email.
# This is placed before the detailed tables in the output.
# ---------------------------------------------------------------------------
md_lines.append("## Summary")
md_lines.append("")

ok_qps = sorted(set(
    r["qp"] for r in rows if r.get("status", "").startswith("ok")
))
missing_qps = sorted(set(qp_values) - set(ok_qps))

if not ok_qps:
    md_lines.append("No valid results were collected. All test cases failed or timed out.")
    md_lines.append("")
else:
    n_qps = len(ok_qps)
    qp_range_str = f"QP {ok_qps[0]}" if n_qps == 1 else f"QP {ok_qps[0]} to {ok_qps[-1]} ({n_qps} points)"
    md_lines.append(
        f"This evaluation tested a single-mode matrix across {qp_range_str}, using "
        f"H.265 constant-QP encoding with an NVIDIA hardware encoder. Each QP "
        f"point ran one all-off baseline and one case per mode (ROI packing, "
        f"background blur, qp_map, TET, luma scaling, and adaptive luma scaling)."
    )
    md_lines.append("")

    if missing_qps:
        md_lines.append(
            f"Note: results for QP {', '.join(missing_qps)} could not be "
            f"collected (test cases failed or timed out)."
        )
        md_lines.append("")

    md_lines.append("### Mode Deltas Versus Baseline")
    md_lines.append("")
    md_lines.append("- Baseline case: `baseline_all_off`")
    md_lines.append("- Static luma scaling mode uses `--luma-downscale-factor 0.5 --enable-adaptive-luma-adaptation false`.")
    md_lines.append("- Adaptive luma scaling mode uses `--enable-adaptive-luma-adaptation true`.")
    md_lines.append("")

    for _, cmp_case, mode_label in mode_case_labels:
        if cmp_case not in test_cases:
            continue
        f1_deltas = []
        iou_deltas = []
        bitrate_deltas = []
        valid_qp_points = 0
        for qp in ok_qps:
            base = rows_by_key.get((qp, baseline_case), {})
            compare = rows_by_key.get((qp, cmp_case), {})
            if not base.get("status", "").startswith("ok") or \
               not compare.get("status", "").startswith("ok"):
                continue
            valid_qp_points += 1
            base_f1 = get_metric(base, "f1_score")
            compare_f1 = get_metric(compare, "f1_score")
            if base_f1 is not None and compare_f1 is not None:
                f1_deltas.append(compare_f1 - base_f1)
            base_iou = get_metric(base, "mean_iou")
            compare_iou = get_metric(compare, "mean_iou")
            if base_iou is not None and compare_iou is not None:
                iou_deltas.append(compare_iou - base_iou)
            base_bitrate = get_metric(base, "bitrate_bps")
            compare_bitrate = get_metric(compare, "bitrate_bps")
            if base_bitrate is not None and compare_bitrate is not None:
                bitrate_deltas.append(compare_bitrate - base_bitrate)

        if valid_qp_points == 0:
            md_lines.append(f"- {mode_label}: insufficient paired data versus baseline.")
            continue

        avg_f1_delta = average_or_none(f1_deltas)
        avg_iou_delta = average_or_none(iou_deltas)
        avg_bitrate_delta = average_or_none(bitrate_deltas)
        f1_text = f"{avg_f1_delta:+.3f}" if avg_f1_delta is not None else "n/a"
        iou_text = f"{avg_iou_delta:+.3f}" if avg_iou_delta is not None else "n/a"
        bitrate_text = f"{avg_bitrate_delta / 1_000_000:+.3f}" if avg_bitrate_delta is not None else "n/a"
        md_lines.append(
            f"- {mode_label}: F1 {f1_text}, mean-IoU {iou_text}, bitrate {bitrate_text} Mbps "
            f"(average compare-baseline over {valid_qp_points} QP point(s))."
        )
    md_lines.append("")

    # ---- Bitrate overview ----
    bitrates = {}
    for qp in ok_qps:
        for c in test_cases:
            r = rows_by_key.get((qp, c), {})
            br = r.get("bitrate_bps", 0)
            if br and br > 0:
                bitrates.setdefault(qp, []).append(br)

    if bitrates:
        md_lines.append("### Bitrate range")
        md_lines.append("")
        for qp in ok_qps:
            if qp in bitrates:
                vals = bitrates[qp]
                lo = min(vals) / 1_000_000
                hi = max(vals) / 1_000_000
                if abs(hi - lo) < 0.01:
                    md_lines.append(f"- QP {qp}: ~{lo:.2f} Mbps")
                else:
                    md_lines.append(f"- QP {qp}: {lo:.2f} - {hi:.2f} Mbps")
        md_lines.append("")
        md_lines.append(
            "Bitrates are measured from the actual H.265 output file produced "
            "by the NVIDIA hardware encoder operating in constant-QP mode. "
            "The variation within each QP is due to the selected single-mode "
            "processing option producing different source content "
            "for the encoder."
        )
        md_lines.append("")

    # ---- Methodology note ----
    md_lines.append("### Methodology")
    md_lines.append("")
    md_lines.append(
        "Each test case encoded the input video at a fixed QP using an "
        "NVIDIA hardware H.265 encoder. The encoder used COCO groundtruth "
        "bounding boxes as ROIs (rather than its own inference) to ensure "
        "identical ROI placement across all configurations. For each QP, the "
        "matrix includes one all-off baseline plus one case where exactly one "
        "mode is enabled. The encoded stream was sent over RTP to a receiver, "
        "which ran YOLO object detection on decoded frames and compared "
        "detections against the same groundtruth annotations."
    )
    md_lines.append("")

# Append the detailed per-QP tables and deltas after the executive summary.
md_lines.append("---")
md_lines.append("")
md_lines.extend(table_lines)

md_path.write_text("\n".join(md_lines))

print(f"Wrote {csv_path}")
print(f"Wrote {md_path}")
print(f"Wrote {delta_csv_path}")
PY
else
    echo "Skipping bbox aggregation because --mode=${RUN_MODE}"
fi

if [ "$RUN_SEMSEG" = "true" ]; then
OUTPUT_DIR_ENV="$OUTPUT_DIR" QP_VALUES_ENV="$QP_VALUES" python3 - <<'PY'
import json
import os
import configparser
import re
from pathlib import Path

output_dir = Path(os.environ["OUTPUT_DIR_ENV"])
qp_values = os.environ.get("QP_VALUES_ENV", "22 27 32 37 42 47").split()

semseg_cases = [
    "semseg_baseline_all_off",
    "semseg_mode_roi_packing",
    "semseg_mode_background_blur",
    "semseg_mode_qp_map",
    "semseg_mode_tet",
    "semseg_mode_luma_scaling",
    "semseg_mode_adaptive_luma_scaling",
]

summary_pattern = re.compile(
    r"SemSeg test mode summary for port \d+:\s+frames=(\d+)\s+points=(\d+)\s+mean_iou=([0-9.]+)"
)


def clean_value(raw):
    value = (raw or "").strip()
    for token in (" #", " ;", "\t#", "\t;"):
        idx = value.find(token)
        if idx != -1:
            value = value[:idx]
    return value.strip().strip('"').strip("'")


def parse_bool(raw):
    return clean_value(raw).lower() in {"1", "true", "yes", "on"}


def resolve_path(path_text, base_dir):
    path_text = clean_value(path_text)
    if not path_text:
        return ""
    candidate = Path(path_text)
    if candidate.is_absolute():
        return str(candidate)
    return str((Path(base_dir) / candidate).resolve())


def read_ini_value(config_path, section, key, default=""):
    if not config_path:
        return default
    path = Path(config_path)
    if not path.exists():
        return default
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    try:
        parser.read(path, encoding="utf-8")
    except Exception:
        return default
    for sec in parser.sections():
        if sec.lower() != section.lower():
            continue
        for opt, value in parser.items(sec):
            if opt.lower() == key.lower():
                return clean_value(value)
    return default


def parse_key_value_file(config_path):
    kv = {}
    path = Path(config_path)
    if not path.exists():
        return kv
    text = path.read_text(errors="ignore")
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or line.startswith(";") or line.startswith("["):
            continue
        match = re.match(r"([A-Za-z0-9_.-]+)\s*[:=]\s*(.+)$", line)
        if not match:
            continue
        key = match.group(1).strip().lower()
        if key in kv:
            continue
        kv[key] = clean_value(match.group(2))
    return kv


def normalize_network_type(value):
    raw = clean_value(value)
    lower = raw.lower()
    if raw == "2" or "segmentation" in lower or "semseg" in lower:
        return "segmentation"
    if raw == "0" or "detection" in lower:
        return "detection"
    if raw == "1" or "classifier" in lower:
        return "classification"
    return raw or "unknown"


def describe_model_usage(deepstream_config_file):
    info = {
        "deepstream_config_file": deepstream_config_file or "",
        "nvinfer_config_file": "",
        "model_engine_file": "",
        "onnx_file": "",
        "network_type": "unknown",
        "model_hint": "none",
        "model_artifact": "none",
    }
    if not deepstream_config_file:
        return info

    ds_path = Path(deepstream_config_file)
    if not ds_path.exists():
        return info

    ds_kv = parse_key_value_file(ds_path)
    nvinfer_rel = ds_kv.get("config-file-path", "")
    nvinfer_path = Path(resolve_path(nvinfer_rel, ds_path.parent)) if nvinfer_rel else ds_path
    info["nvinfer_config_file"] = str(nvinfer_path)

    nvinfer_kv = parse_key_value_file(nvinfer_path)
    nvinfer_dir = nvinfer_path.parent

    model_engine = resolve_path(nvinfer_kv.get("model-engine-file", ""), nvinfer_dir)
    onnx_file = resolve_path(nvinfer_kv.get("onnx-file", ""), nvinfer_dir)
    tlt_model = resolve_path(nvinfer_kv.get("tlt-encoded-model", ""), nvinfer_dir)
    caffemodel = resolve_path(nvinfer_kv.get("model-file", ""), nvinfer_dir)
    custom_network = resolve_path(nvinfer_kv.get("custom-network-config", ""), nvinfer_dir)

    artifact = model_engine or onnx_file or tlt_model or caffemodel or custom_network
    info["model_engine_file"] = model_engine
    info["onnx_file"] = onnx_file
    info["network_type"] = normalize_network_type(nvinfer_kv.get("network-type", ""))
    if artifact:
        info["model_artifact"] = artifact
        info["model_hint"] = Path(artifact).name
    else:
        info["model_hint"] = Path(ds_path).name
    return info


def resolve_existing_path(path_text, *base_dirs):
    path_text = clean_value(path_text)
    if not path_text:
        return ""

    candidate = Path(path_text)
    if candidate.is_absolute() and candidate.exists():
        return str(candidate)

    for base in base_dirs:
        base_text = clean_value(str(base)) if base else ""
        if not base_text:
            continue
        resolved = Path(resolve_path(path_text, base_text))
        if resolved.exists():
            return str(resolved)

    if candidate.is_absolute():
        return str(candidate)

    for base in base_dirs:
        base_text = clean_value(str(base)) if base else ""
        if not base_text:
            continue
        return str((Path(base_text) / path_text).resolve())
    return path_text


def is_placeholder(value):
    return clean_value(value).lower() in {"", "none", "unknown", "n/a"}


def normalize_model_summary_fields(row, prefix):
    engine_enabled = clean_value(row.get(f"{prefix}_inference_engine", "")).lower() == "true"
    hint_key = f"{prefix}_model_hint"
    artifact_key = f"{prefix}_model_artifact"
    network_key = f"{prefix}_network_type"

    if not engine_enabled:
        row[hint_key] = "n/a"
        row[artifact_key] = "n/a"
        row[network_key] = "n/a"
        return

    if is_placeholder(row.get(hint_key, "")):
        row[hint_key] = "n/a"
    if is_placeholder(row.get(artifact_key, "")):
        row[artifact_key] = "n/a"
    if is_placeholder(row.get(network_key, "")):
        row[network_key] = "n/a"


def load_case_metadata(case_dir):
    path = Path(case_dir) / "case_metadata.json"
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text())
    except Exception:
        return {}


def collect_case_runtime_info(case_dir):
    metadata = load_case_metadata(case_dir)
    semseg_enabled = parse_bool(metadata.get("semseg_enabled", "false"))
    luma_adaptation_enabled = parse_bool(metadata.get("luma_adaptation_enabled", "false"))
    adaptive_luma_adaptation_enabled = parse_bool(metadata.get("adaptive_luma_adaptation_enabled", "false"))
    luma_downscale_factor = clean_value(metadata.get("luma_downscale_factor", ""))
    if not luma_adaptation_enabled:
        luma_downscale_factor = "n/a"
    elif adaptive_luma_adaptation_enabled:
        luma_downscale_factor = "n/a"
    elif not luma_downscale_factor:
        luma_downscale_factor = "0.5"

    encoder_config = metadata.get("encoder_config_file", "")
    encoder_base_dir = metadata.get("encoder_config_base_dir", "")
    if "encoder_inference_enabled" in metadata:
        encoder_inference_enabled = parse_bool(metadata.get("encoder_inference_enabled", "false"))
    else:
        encoder_inference_enabled = parse_bool(read_ini_value(encoder_config, "ROI", "inference_engine", "false"))
    encoder_deepstream = resolve_existing_path(
        metadata.get("encoder_inference_config_file", ""),
        encoder_base_dir,
        Path(encoder_config).parent if encoder_config else "",
        output_dir,
    )
    if not encoder_deepstream:
        encoder_deepstream = resolve_existing_path(
            read_ini_value(encoder_config, "ROI", "deepstream_config_file", ""),
            encoder_base_dir,
            Path(encoder_config).parent if encoder_config else "",
            output_dir,
        )
    encoder_model = describe_model_usage(encoder_deepstream)

    rtp_config = metadata.get("rtp_config_file", "")
    rtp_base_dir = metadata.get("rtp_config_base_dir", "")
    if "rtp_inference_enabled" in metadata:
        rtp_inference_enabled = parse_bool(metadata.get("rtp_inference_enabled", "false"))
    else:
        rtp_inference_enabled = parse_bool(read_ini_value(rtp_config, "Inference", "inference_engine", "false"))
    rtp_deepstream = resolve_existing_path(
        metadata.get("rtp_inference_config_file", ""),
        rtp_base_dir,
        Path(rtp_config).parent if rtp_config else "",
        output_dir,
    )
    if not rtp_deepstream:
        rtp_deepstream = resolve_existing_path(
            read_ini_value(rtp_config, "Inference", "deepstream_config_file", ""),
            rtp_base_dir,
            Path(rtp_config).parent if rtp_config else "",
            output_dir,
        )
    rtp_model = describe_model_usage(rtp_deepstream)

    result = {
        "encoder_inference_engine": "true" if encoder_inference_enabled else "false",
        "encoder_deepstream_config_file": encoder_model.get("deepstream_config_file", ""),
        "encoder_nvinfer_config_file": encoder_model.get("nvinfer_config_file", ""),
        "encoder_model_engine_file": encoder_model.get("model_engine_file", ""),
        "encoder_onnx_file": encoder_model.get("onnx_file", ""),
        "encoder_model_artifact": encoder_model.get("model_artifact", "none"),
        "encoder_network_type": encoder_model.get("network_type", "unknown"),
        "encoder_model_hint": encoder_model.get("model_hint", "none"),
        "rtp_inference_engine": "true" if rtp_inference_enabled else "false",
        "rtp_deepstream_config_file": rtp_model.get("deepstream_config_file", ""),
        "rtp_nvinfer_config_file": rtp_model.get("nvinfer_config_file", ""),
        "rtp_model_engine_file": rtp_model.get("model_engine_file", ""),
        "rtp_onnx_file": rtp_model.get("onnx_file", ""),
        "rtp_model_artifact": rtp_model.get("model_artifact", "none"),
        "rtp_network_type": rtp_model.get("network_type", "unknown"),
        "rtp_model_hint": rtp_model.get("model_hint", "none"),
        "luma_adaptation_enabled": "true" if luma_adaptation_enabled else "false",
        "adaptive_luma_adaptation_enabled": "true" if adaptive_luma_adaptation_enabled else "false",
        "luma_downscale_factor": luma_downscale_factor,
        "semseg_config_resolution_source": metadata.get("semseg_config_resolution_source", "") or "n/a",
    }
    if semseg_enabled:
        normalize_model_summary_fields(result, "encoder")
        normalize_model_summary_fields(result, "rtp")
    return result


rows = []
for qp in qp_values:
    for case in semseg_cases:
        case_dir = output_dir / f"qp{qp}" / case
        json_path = case_dir / f"test_mode_semseg_{case}.json"
        log_path = case_dir / f"rtp_server_output_{case}.txt"
        bitrate_path = case_dir / "bitrate_bps.txt"

        bitrate_bps = 0
        if bitrate_path.exists():
            try:
                bitrate_bps = int(bitrate_path.read_text().strip() or "0")
            except ValueError:
                bitrate_bps = 0

        row = {
            "qp": qp,
            "case": case,
            "bitrate_bps": bitrate_bps,
            "status": "missing",
            "frames_with_groundtruth": "",
            "points_evaluated": "",
            "mean_iou": "",
            "capture_state": "",
            "capture_ratio": "",
            "capture_observed_frames": "",
            "capture_expected_frames": "",
        }

        if json_path.exists():
            try:
                payload = json.loads(json_path.read_text())
                row["status"] = "ok_json"
                row["frames_with_groundtruth"] = payload.get("frames_with_groundtruth", "")
                row["points_evaluated"] = payload.get("points_evaluated", "")
                row["mean_iou"] = payload.get("mean_iou", "")
                row["capture_state"] = payload.get("capture_state", "")
                row["capture_ratio"] = payload.get("capture_ratio", "")
                row["capture_observed_frames"] = payload.get("capture_observed_frames", "")
                row["capture_expected_frames"] = payload.get("capture_expected_frames", "")
                if str(row["capture_state"]).strip().lower() == "incomplete":
                    row["status"] = "degraded_capture_json"
                if int(row["frames_with_groundtruth"] or 0) == 0 and int(row["points_evaluated"] or 0) == 0:
                    row["status"] = "no_data_json"
            except Exception:
                row["status"] = "invalid_json"
        elif log_path.exists():
            log_text = log_path.read_text(errors="ignore")
            match = summary_pattern.search(log_text)
            if match:
                row["status"] = "ok_log"
                row["frames_with_groundtruth"] = int(match.group(1))
                row["points_evaluated"] = int(match.group(2))
                row["mean_iou"] = float(match.group(3))
                if int(row["frames_with_groundtruth"] or 0) == 0 and int(row["points_evaluated"] or 0) == 0:
                    row["status"] = "no_data_log"

        row.update(collect_case_runtime_info(case_dir))
        rows.append(row)

columns = [
    "qp",
    "case",
    "luma_adaptation_enabled",
    "adaptive_luma_adaptation_enabled",
    "luma_downscale_factor",
    "bitrate_bps",
    "status",
    "frames_with_groundtruth",
    "points_evaluated",
    "mean_iou",
    "capture_state",
    "capture_ratio",
    "capture_observed_frames",
    "capture_expected_frames",
    "encoder_inference_engine",
    "encoder_model_hint",
    "encoder_model_artifact",
    "encoder_network_type",
    "encoder_deepstream_config_file",
    "encoder_nvinfer_config_file",
    "encoder_model_engine_file",
    "encoder_onnx_file",
    "rtp_inference_engine",
    "rtp_model_hint",
    "rtp_model_artifact",
    "rtp_network_type",
    "rtp_deepstream_config_file",
    "rtp_nvinfer_config_file",
    "rtp_model_engine_file",
    "rtp_onnx_file",
    "semseg_config_resolution_source",
]

def fmt(value):
    if isinstance(value, float):
        return f"{value:.3f}"
    return "" if value is None else str(value)

csv_path = output_dir / "summary_semseg.csv"
csv_lines = [",".join(columns)]
for row in rows:
    csv_lines.append(",".join(fmt(row.get(column)) for column in columns))
csv_path.write_text("\n".join(csv_lines) + "\n")

md_path = output_dir / "summary_semseg.md"
md_lines = [
    "# SemSeg Comparison Summary",
    "",
    "## Runtime Config and Model Usage",
    "",
]
model_cols = [
    "case",
    "luma_adaptation_enabled",
    "adaptive_luma_adaptation_enabled",
    "luma_downscale_factor",
    "encoder_inference_engine",
    "rtp_inference_engine",
    "semseg_config_resolution_source",
]
has_reliable_model_details = False
for row in rows:
    if clean_value(row.get("encoder_model_hint", "")).lower() not in {"", "n/a", "none"}:
        has_reliable_model_details = True
    if clean_value(row.get("encoder_model_artifact", "")).lower() not in {"", "n/a", "none"}:
        has_reliable_model_details = True
    if clean_value(row.get("rtp_model_hint", "")).lower() not in {"", "n/a", "none"}:
        has_reliable_model_details = True
    if clean_value(row.get("rtp_model_artifact", "")).lower() not in {"", "n/a", "none"}:
        has_reliable_model_details = True
if has_reliable_model_details:
    model_cols = [
        "case",
        "luma_adaptation_enabled",
        "adaptive_luma_adaptation_enabled",
        "luma_downscale_factor",
        "encoder_inference_engine",
        "encoder_model_hint",
        "encoder_model_artifact",
        "rtp_inference_engine",
        "rtp_model_hint",
        "rtp_model_artifact",
        "rtp_network_type",
        "semseg_config_resolution_source",
    ]
md_lines.append("| " + " | ".join(model_cols) + " |")
md_lines.append("| " + " | ".join(["---"] * len(model_cols)) + " |")
model_rows_seen = set()
for row in rows:
    row_key = tuple(row.get(col, "") for col in model_cols)
    if row_key in model_rows_seen:
        continue
    model_rows_seen.add(row_key)
    md_lines.append("| " + " | ".join(fmt(row.get(col)) for col in model_cols) + " |")
if not has_reliable_model_details:
    md_lines.extend([
        "",
        "_Model artifact details are hidden because reliable per-case model metadata was unavailable._",
    ])
md_lines.extend([
    "",
    "## Per-Case Metrics",
    "",
    "| qp | case | status | mean_iou | points_evaluated | capture_ratio | capture_observed/expected | bitrate_bps |",
    "| --- | --- | --- | --- | --- | --- | --- | --- |",
])
for row in rows:
    md_lines.append(
        f"| {row['qp']} | {row['case']} | {fmt(row.get('status'))} | {fmt(row.get('mean_iou'))} | "
        f"{fmt(row.get('points_evaluated'))} | {fmt(row.get('capture_ratio'))} | "
        f"{fmt(row.get('capture_observed_frames'))}/{fmt(row.get('capture_expected_frames'))} | "
        f"{fmt(row.get('bitrate_bps'))} |"
    )
md_lines.append("")
md_path.write_text("\n".join(md_lines))

print(f"Wrote {csv_path}")
print(f"Wrote {md_path}")
PY
else
    echo "Skipping semseg aggregation because --mode=${RUN_MODE}"
fi

echo ""
echo "All runs completed. Results in: $OUTPUT_DIR"
echo "Total runtime: $(format_elapsed_runtime)"
