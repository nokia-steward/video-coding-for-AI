# Project Scripts

This directory contains shared build, setup, evaluation, and utility scripts for VCAI.

The sections below document each script in more detail, including all command line parameters it accepts.
For scripts using Python `argparse`, `-h`/`--help` is also available automatically.

## Host setup scripts

Host setup scripts live under **`scripts/setup/`** — see [Host dependency setup with `bootstrap.sh`](setup/README.md) and `./scripts/setup/bootstrap.sh`.

### Core setup (`bootstrap.sh core`)

Installs ONNX Runtime into `lib/onnxruntime`. Builds the DeepStream-Yolo parser only when DeepStream SDK headers are present; otherwise skips Yolo and succeeds. Optional codecs (`bootstrap.sh kvazaar` / `openhevc`) do not require DeepStream—run them separately when needed.
`./scripts/setup/bootstrap.sh core` orchestrates `scripts/setup/bin/setup_onnxruntime.sh` and
`scripts/setup/bin/setup_yolo.sh`.

**Parameters**

- None (bootstrap subcommand `core`).

**Example**
```bash
./scripts/setup/bootstrap.sh core
```

### `setup/bin/install_kvazaar.sh`

Builds and installs Kvazaar and gst-kvazaar system-wide (`/usr`). Used by `dev.def` %post and Docker builds; run from repo root so `patches/` is found.

**Parameters**

- None.

**Example**
```bash
./scripts/setup/bin/install_kvazaar.sh
```

### `setup/bin/install_openhevc.sh`

Builds and installs OpenHEVC (ffmpeg_update branch) and gst-openhevc system-wide (`/usr`). Optional in `dev.def` %post after kvazaar. Run from repo root so `patches/` is found. Optional patches: `patches/gstopenhevcviddec.c`; **`patches/openhevc_libavutil_cpu_sysctl.patch`** (required on Linux/glibc 2.32+ — `sys/sysctl.h` removed; applied automatically). CMake uses **`-DENABLE_EXECUTABLE=OFF`** so only `LibOpenHevcWrapper` is built (upstream `hevc_sdl2` mixes SDL1 headers with SDL2 and fails on typical Linux installs). **`patches/libopenhevc.pc.in`** is installed as `libopenhevc.pc` after OpenHEVC install because upstream does not ship it and gst-openhevc requires `dependency('libopenhevc')`. **`libopenhevc/*.h`** is copied into `include/libopenhevc/` for `<libopenhevc/openhevc.h>`. **`patches/openhevc_cmake_append_openhevc_c.patch`** appends `libopenhevc/openhevc.c` to `LibOpenHevcWrapper` so `oh_init`/`oh_decode`/etc. link. After appending `openhevc.c`, the install script **strips** `openhevc_version` and `openhevc_configuration` from `openhevc.c` via Python (same symbols already in `openHevcWrapper.c`; the old unified patch was malformed).

**Example**
```bash
./scripts/setup/bin/install_openhevc.sh
# Verify: gst-inspect-1.0 openhevcdec
```

## DeepStream / YOLO Setup

### DeepStream YOLO export environment (`bootstrap.sh ds-env`)

Creates `.env/ds_export` and installs model-export dependencies (PyTorch, ultralytics, ONNX tools).
`./scripts/setup/bootstrap.sh ds-env` runs `setup_ds_export_env.sh` with `NON_INTERACTIVE=1`.

**Parameters**

- None (bootstrap subcommand `ds-env`).

**Environment variables**

- When calling `scripts/setup/bin/setup_ds_export_env.sh` directly: `NON_INTERACTIVE` (optional, default `0`); bootstrap sets `NON_INTERACTIVE=1`.

**Example**
```bash
./scripts/setup/bootstrap.sh ds-env
```

## YOLO Scripts (`scripts/yolo/`)

### `setup/bin/setup_yolo.sh` (canonical)

Clones/updates `yolo/DeepStream-Yolo`, applies local parser patch if present, and builds the custom parser library.
Delegates to `scripts/yolo/setup_yolo.sh`.

**Parameters**

- None.

**Example**
```bash
./scripts/setup/bin/setup_yolo.sh
```

### `yolo/convert_yolo_model.sh`

Exports a YOLO `.pt` model to a DeepStream-compatible ONNX with static dimensions.

**Usage**
```bash
./scripts/yolo/convert_yolo_model.sh <width> <height> <output_directory> [version]
```

**Parameters**

- `<width>` (required): target video width before YOLO rounding.
- `<height>` (required): target video height before YOLO rounding.
- `<output_directory>` (required): directory where model files are written.
- `[version]` (optional, default `8n`): YOLO version token like `8n`, `8s`, `11m`; numeric part must be in `5..11`.

**Notes**

- Width/height are rounded up to multiples of 32 for YOLO input dimensions.

**Example**
```bash
./scripts/yolo/convert_yolo_model.sh 1920 1080 ./yolo 11m
```

### `yolo/generate_yolo_config.sh`

Generates `yolo_infer_config_<ver>.yml` and `yolo_config_<ver>.yml`.

**Usage**
```bash
./scripts/yolo/generate_yolo_config.sh [video_width] [video_height] [yolo_version]
```

**Parameters**

- `[video_width]` (optional, default `832`): source stream width.
- `[video_height]` (optional, default `480`): source stream height.
- `[yolo_version]` (optional, default `8n`): version suffix used in model/config filenames.

**Notes**

- Internal YOLO infer dimensions are rounded to multiples of 32.

**Example**
```bash
./scripts/yolo/generate_yolo_config.sh 1920 1080 11m
```

### `yolo/check_yolo_ready.sh`

Checks presence of required YOLO assets (custom parser `.so`, labels, ONNX, YAML configs).

**Usage**
```bash
./scripts/yolo/check_yolo_ready.sh <video_width> <video_height> [yolo_version]
```

**Parameters**

- `<video_width>` (required): stream width.
- `<video_height>` (required): stream height.
- `[yolo_version]` (optional, default `8n`): model/config version suffix.

**Example**
```bash
./scripts/yolo/check_yolo_ready.sh 1920 1080 11m
```

### `yolo/common_paths.sh`

Shared helper library sourced by YOLO scripts (path resolution, CUDA/DeepStream detection).

**Parameters**

- No standalone CLI. This file is intended to be sourced, not executed directly.

## Video Utilities

### `flip_video_horizontal.sh`

Flips an input video horizontally with ffmpeg.

**Usage**
```bash
./scripts/flip_video_horizontal.sh <input_video_file> [output_file]
```

**Parameters**

- `<input_video_file>` (required): source video path.
- `[output_file]` (optional): output path; defaults to `<name>_flipped.<ext>`.

### `remove_audio.sh`

Removes all audio tracks from a video while copying video stream.

**Usage**
```bash
./scripts/remove_audio.sh <input_video_file> [output_file]
```

**Parameters**

- `<input_video_file>` (required): source video path.
- `[output_file]` (optional): output path; defaults to `<name>_no_audio.<ext>`.

### `play_yuv.sh`

Plays raw `yuv420p` video via ffplay.

**Usage**
```bash
./scripts/play_yuv.sh <input_yuv_file>
./scripts/play_yuv.sh <input_yuv_file> <width>x<height> <framerate>
```

**Parameters**

- `<input_yuv_file>` (required): YUV file path.
- `<width>x<height>` (optional): explicit resolution; required when filename does not encode metadata.
- `<framerate>` (optional): explicit fps; required when filename does not encode metadata.

**Notes**

- If only the input path is provided, filename must match `<name>_<width>x<height>_<fps>.yuv`.

## Streaming Utilities

### `loop_h265_udp.sh`

Loops a video input and streams H.265 RTP over UDP to one or more destinations. (Shell wrapper with inline Python.)

**Usage**
```bash
./scripts/loop_h265_udp.sh [options]
```

**Parameters**

- `--host`, `-H` (optional, repeatable): destination `IP` or `IP:PORT`. If omitted, defaults to localhost using `--port`.
- `--port`, `-p` (optional, default `12300`): default destination port for hosts without explicit port, and for default localhost destination.
- `--fps`, `-f` (optional): force output fps.
- `--input`, `-i` (optional, default `videos/simulation.mkv`): input file path.

**Notes**

- For raw `.yuv` input, filename must contain `<width>x<height>_<fps>` metadata.
- Output is always encoded with `libx265` for deterministic RTP behavior.
- GOP cadence is hardcoded to a short interval (`keyint=min-keyint=30`, ~1s at 30 fps).
- Encoder forces `repeat-headers=1`, `aud=1`, and `annexb=1` to improve decoder join/recovery behavior.

**Examples**
```bash
./scripts/loop_h265_udp.sh
./scripts/loop_h265_udp.sh -H 192.168.1.100 -H 192.168.1.101:12301 -i video.h265
```

### `view_h265_udp.sh`

Receives H.265 RTP and displays it with ffplay. (Shell wrapper with inline Python.)

**Usage**
```bash
./scripts/view_h265_udp.sh [options]
```

**Parameters**

- `--port`, `-p` (optional, default `12300`): UDP port to listen on.
- `--framerate`, `-f` (optional): playback fps override.

**Examples**
```bash
./scripts/view_h265_udp.sh --port 12300
./scripts/view_h265_udp.sh -p 12300 -f 30
```

### `receive_h265_udp.sh`

Receives H.265 RTP and records to a file via ffmpeg. (Shell wrapper with inline Python.)

**Parameters**

- No CLI parser currently; behavior is controlled by top-of-file constants:
  - `UDP_PORT` (default `12300`)
  - `PAYLOAD_TYPE` (default `107`)
  - `OUTPUT_FILE` (default `videos/simulation.mkv`)
  - `SDP_FILENAME` (default `temp_stream.sdp`)

**Example**
```bash
./scripts/receive_h265_udp.sh
```

## Codebase Statistics

### `codebase_statistics_report.sh`

Generates a repository statistics report and writes it to a markdown file. (Shell wrapper with inline Python; run from repo root or any directory inside the git tree.)

**Parameters**

- `--output` (optional, default `reports/codebase_statistics_report.md`): output markdown file path.
- `--stdout` (optional flag): also print the generated report to stdout after writing the file.

**Examples**
```bash
./scripts/codebase_statistics_report.sh
./scripts/codebase_statistics_report.sh --output reports/custom_report.md
./scripts/codebase_statistics_report.sh --stdout
```

## Apptainer / Container Scripts

### `enter_dev.sh`

Starts an interactive bash shell using `scripts/lib/dev_wrapper.sh` as rcfile.

**Parameters**

- None.

**Example**
```bash
./scripts/enter_dev.sh
```

### `lib/dev_wrapper.sh`

Location: `scripts/lib/dev_wrapper.sh`

Shell rc wrapper that sources user `.bashrc` and `scripts/setup/lib/dev_functions_lib.sh`.

**Parameters**

- No standalone CLI options. Intended as an rcfile (`bash --rcfile ./scripts/lib/dev_wrapper.sh -i`).

### `setup/lib/dev_functions_lib.sh`

Defines convenience shell functions (`setup_dependencies`, `build_all`, `run_encoder*`, `run_rtp_server`, etc.). Sourced by `lib/dev_wrapper.sh` only.

**Parameters**

- No standalone script CLI; this file is sourced and its functions accept their own arguments.

## TET Evaluation Scripts

TET comparison support scripts now live under `scripts/tet_comparison_framework/`.
The user-facing entrypoint remains `scripts/run_tet_comparison.sh`, which forwards
to the framework implementation.

### `run_tet_comparison.sh`

Runs multi-case TET comparison across one or more QP values and writes summary CSV/Markdown.
By default (`--mode both`) each QP runs:

- seven bbox cases in a single-mode matrix:
  all-off baseline, ROI packing only, background blur only, `qp_map_delta` only,
  TET only, static luma scaling only (`--luma-downscale-factor 0.5 --enable-adaptive-luma-adaptation false`),
  and adaptive luma scaling only (`--enable-adaptive-luma-adaptation true`)
- seven semseg cases with the same one-mode-only matrix
  (encoder ROI groundtruth stays enabled for all semseg cases)

Use `--mode bbox`, `--mode semseg`, or `--mode both` to control which matrix is executed.
The script also mirrors full stdout/stderr to `<outdir>/run_tet_comparison.log`.

#### Test types executed by `run_tet_comparison.sh`

1) Bounding-box (bbox) single-mode matrix (`--mode bbox` or `--mode both`):

- `baseline_all_off`: all TET/ROI background modes off.
- `mode_roi_packing`: ROI packing only.
- `mode_background_blur`: background blur only.
- `mode_qp_map`: `qp_map_delta` only.
- `mode_tet`: TET only.
- `mode_luma_scaling`: static luma adaptation only (`--luma-downscale-factor 0.5 --enable-adaptive-luma-adaptation false`).
- `mode_adaptive_luma_scaling`: adaptive luma adaptation only (`--enable-adaptive-luma-adaptation true`).
- Metrics are collected from `rtp_server` test-mode outputs (`precision`, `recall`, `f1`, `map_50`, `mean_iou`).

2) Semantic-segmentation (semseg) single-mode matrix (`--mode semseg` or `--mode both`):

- `semseg_baseline_all_off`
- `semseg_mode_roi_packing`
- `semseg_mode_background_blur`
- `semseg_mode_qp_map`
- `semseg_mode_tet`
- `semseg_mode_luma_scaling`
- `semseg_mode_adaptive_luma_scaling`
- Frames are captured from `rtp_server` debug output and evaluated by the AH0187 backend (`evaluate_semseg_sparse.py`), producing per-case `test_mode_semseg_<case>.json/.md`.
- Capture completeness is evaluated (`capture_ratio`, `capture_state`) and enforced by `SEMSEG_INCOMPLETE_CAPTURE_POLICY` (`degrade` or `fail`).

3) QP sweep test matrix:

- Every enabled case above is repeated for each QP in `--qp-values` (default: `22 27 32 37 42 47`).
- Planned case count scales as:
  - bbox only: `7 * number_of_qps`
  - semseg only: `7 * number_of_qps`
  - both: `14 * number_of_qps`

4) Dataset scope modes:

- Single-dataset mode: runs the full matrix once on one dataset/input.
- Sequence mode: if `--dataset-dir` contains numeric sequence folders (`057`, `058`, ...), the script runs one full matrix per sequence and stores outputs under per-sequence subdirectories.

5) Runtime/robustness validation per case:

- Readiness and lifecycle checks: stale-process cleanup, port-availability checks, server-ready wait, and controlled shutdown.
- Encoder timeout checks with separate limits for `qp_map_delta` vs legacy modes, plus no-marker timeout guards.
- Bitrate measurement checks using multiple fallback methods; `strict` policy aborts on measurement failure, `tolerant` writes `bitrate_bps=0` and continues.

**Usage**
```bash
./scripts/run_tet_comparison.sh [options]
```

**Options**

- `--input PATH`: input YUV file path (optional if auto-generation is possible).
- `--groundtruth PATH`: COCO groundtruth JSON path (required in single-dataset mode).
- `--dataset-dir PATH`: PandaSet root for auto-generation and sequence mode detection.
- `--dataset PATH`: alias for `--dataset-dir`.
- `--tile PATH`: tile `.tet` file (default: `rtp_server/tiles/yuv_32x32_checkerboard.tet`).
- `--mode MODE`: `bbox|semseg|both` (default `both`).
- `--semseg-groundtruth PATH`: sparse semseg groundtruth JSON path (auto-generated if missing).
- `--outdir PATH`: output directory (default: auto-generated `tet_test_results_<dataset>_<timestamp>`).
- `--qp-values "LIST"`: space-separated QPs (default: `"22 27 32 37 42 47"`).
- `--sleep SECONDS`: delay before encoder start (default `5`).
- `--shutdown SECONDS`: server shutdown timeout (default `30`).
- `--result-wait SECONDS`: wait for JSON summary (default `180`).
- `--bitrate-policy strict|tolerant`: bitrate failure handling (default `strict`).
- `--encoder-timeout SECONDS`: max shutdown duration for `qp_map_delta` cases once shutdown starts (default `10`).
- `--encoder-timeout-legacy SECONDS`: max shutdown duration for non-`qp_map_delta` cases once shutdown starts (default `45`).
- `--save-frames`: enable debug frame saving.
- `-h`, `--help`: print help.

**Environment variables**

#### General run control
- `QP_VALUES`: default QP list (overridden by `--qp-values`).
- `BITRATE_POLICY`: default bitrate policy (overridden by `--bitrate-policy`).
- `RUN_MODE`: default mode (`bbox|semseg|both`) unless overridden by `--mode`.
- `SKIP_BUILDS=true`: internal optimization for recursive sequence-mode calls.

#### `qp_map_delta` tuning and shutdown limits
- `QP_MAP_BACKGROUND_DELTA`: background QP delta for `qp_map_delta` mode (default `10`).
- `ENCODER_TIMEOUT`: max `encoder_cli` shutdown duration for `qp_map_delta` cases in seconds (default `10`).
- `ENCODER_TIMEOUT_LEGACY`: max `encoder_cli` shutdown duration for non-`qp_map_delta` cases in seconds (default `45`).

#### Semseg runtime selection
- `AH0187_SEMSEG_PYTHON`: optional Python interpreter path for semseg evaluation (must provide `detectron2`, `opencv-python`, `numpy`).
- `AH0187_SEMSEG_CONDA_ENV`: conda env name to probe for semseg evaluation runtime (default `pandaset`).
- `AH0187_SEMSEG_CONDA_BIN`: optional absolute path to `conda` when not on `PATH`.

#### Semseg auto-setup
- `AH0187_AUTO_SETUP`: when `true` (default), auto-creates `.env/semseg` and installs evaluator dependencies if no working runtime is found.
- `AH0187_TORCH_INDEX_URL`: optional pip index URL used during auto-setup for `torch` and `torchvision`.

#### Semseg completeness thresholds
- `SEMSEG_FRAME_DRAIN_TIMEOUT`: max seconds to wait for AH0187 debug-frame drain before stopping `rtp_server` (default `90`).
- `SEMSEG_MIN_CAPTURE_RATIO`: minimum captured/expected frame ratio for semseg completeness (default `0.95`).
- `SEMSEG_INCOMPLETE_CAPTURE_POLICY`: `degrade|fail` policy for semseg cases below the capture ratio threshold (default `degrade`).

`qp_map_delta` availability is gated during summary generation using a machine-readable
contract emitted by `encoder_cli` as a single stdout line prefixed with
`CONTRACT_JSON:`. Required keys are `schema`, `requested_background_mode`,
`effective_background_mode`, and `direct_qp_map_backend` (schema:
`encoder_backend_contract/v1`).

If direct NvEncodeAPI backend was not actually used for all `qp_map_delta` runs,
those rows are excluded and `summary.md` contains a note. For older logs that do
not include the contract line, the script falls back to legacy marker heuristics
and annotates that fallback in `summary.md`.

`run_tet_comparison.sh` forces display output off in both apps during test runs:

- `encoder_cli`: per-case config sets `enable_display_branch = false`
- `rtp_server`: per-case config sets `enable_display_sink = false` (headless `fakesink`)

If `encoder_cli` times out or exits non-zero in any case, the script stops `rtp_server` and aborts
the full run immediately.

#### Result files

- bbox aggregation: `summary.csv`, `summary.md`, `summary_deltas.csv`
- semseg aggregation: `summary_semseg.csv`, `summary_semseg.md`

#### Luma scaling reporting

- Case metadata records `luma_adaptation_enabled`, `adaptive_luma_adaptation_enabled`, and `luma_downscale_factor`.
- These fields are included in runtime/model sections of `summary.md` and `summary_semseg.md`,
  and as columns in `summary.csv` and `summary_semseg.csv`.

Semseg mode in `run_tet_comparison.sh` now uses the AH0187 machine-task network
backend directly:

- captures clean reconstructed frames from `rtp_server` in script-only mode
- evaluates those frames with Detectron2 Panoptic FPN R-101-FPN 3x
- writes `test_mode_semseg_<case>.json/.md` per case, then aggregates them
- records frame-capture completeness fields in per-case semseg JSON and marks
  aggregate status as degraded when capture is below threshold

Semseg runtime/model metadata in `summary_semseg.csv/.md` is derived from
per-case `case_metadata.json` plus resolved config files. For semseg runs where
model artifacts are intentionally not active (for example, receiver inference is
disabled), metadata fields are normalized to `n/a`. If reliable model artifact
details cannot be resolved, `summary_semseg.md` automatically hides noisy model
detail columns and keeps only meaningful runtime/backend fields.

#### Semseg groundtruth conversion automation

- If `pandaset`/`numpy` are missing, `run_tet_comparison.sh` auto-creates a local venv at `.env/pandaset`,
  installs required packages, and retries conversion.

**Example**
```bash
./scripts/run_tet_comparison.sh --qp-values "27 32 37 42" --input /path/to/video.yuv --groundtruth /path/to/gt.json
```

### `run_tet_bdrate.sh`

Runs TET off/on at multiple QP points and creates `bdrate_input.csv`.
Location: `scripts/tet_comparison_framework/run_tet_bdrate.sh`

**Usage**
```bash
./scripts/tet_comparison_framework/run_tet_bdrate.sh [options]
```

**Options**

- `--input PATH`: input YUV path (optional if PandaSet auto-generation is used).
- `--groundtruth PATH`: COCO groundtruth path (default is script-defined path under `Traffic_2560x1600_30_val`).
- `--dataset-dir PATH`: PandaSet root used for input auto-generation.
- `--tile PATH`: tile `.tet` path (default: `rtp_server/tiles/yuv_32x32_checkerboard.tet`).
- `--outdir PATH`: result directory (default: `tet_bdrate_results_<timestamp>`).
- `--qp-values "LIST"`: space-separated QPs (default: `"22 27 32 37 42 47"`).
- `--sleep SECONDS`: delay before encoder start (default `5`).
- `--shutdown SECONDS`: server shutdown timeout (default `30`).
- `--result-wait SECONDS`: wait for JSON summary (default `180`).
- `-h`, `--help`: print help.

**Environment variables**

- `QP_VALUES`: default QP list (overridden by `--qp-values`).

**Example**
```bash
./scripts/tet_comparison_framework/run_tet_bdrate.sh --qp-values "27 32 37 42" --input /path/to/video.yuv --groundtruth /path/to/gt.json
```

### `compute_bdrate.py`

Computes BD-rate from the CSV produced by `run_tet_bdrate.sh`.
Location: `scripts/tet_comparison_framework/tools/compute_bdrate.py`

**Usage**
```bash
python3 scripts/tet_comparison_framework/tools/compute_bdrate.py <csv_file> [options]
```

**Parameters**

- `<csv_file>` (required): CSV containing `qp,tet,bitrate_bps,<metric>`.
- `--metric` (optional, default `f1_score`): one of `precision`, `recall`, `f1_score`, `mean_iou`.
- `--verbose` (optional): print per-QP points used for the curves.

**Example**
```bash
python3 scripts/tet_comparison_framework/tools/compute_bdrate.py results/bdrate_input.csv --metric mean_iou --verbose
```

### `populate_vcmrs_excel.py`

Fills VCMRS `.xlsm` template(s) from TET summary CSV results.
Location: `scripts/tet_comparison_framework/tools/populate_vcmrs_excel.py`

**Usage**
```bash
python3 scripts/tet_comparison_framework/tools/populate_vcmrs_excel.py --results-dir <dir> --excel-template <template.xlsm> [options]
```

**Parameters**

- `--results-dir` (required): results root containing either `summary.csv` or per-sequence `<seq>/summary.csv`.
- `--excel-template` (required): source `.xlsm` template path.
- `--sheet` (optional, default `SFU-HW_RA_Inner`): worksheet name to populate.
- `--sequence` (optional): sequence filter (numeric-normalized, for example `057 == 57`).
- `--output-dir` (optional): output directory (default is `--results-dir`).

**Notes**

- Generates one workbook per non-anchor case present in results (single-mode matrix), including `mode_qp_map` when available.
- In multi-sequence mode, only cases common to all selected sequences are emitted.

### `convert_pandaset_to_coco.py`

Converts PandaSet cuboids to strict 5-class COCO JSON.
Location: `scripts/tet_comparison_framework/tools/convert_pandaset_to_coco.py`

**Usage**
```bash
python3 scripts/tet_comparison_framework/tools/convert_pandaset_to_coco.py --dataset-dir <PandaSet_root> [options]
```

**Parameters**

- `--dataset-dir` (required): PandaSet dataset root.
- `--camera` (optional, default `front_camera`): camera under `dataset_dir/camera/`.
- `--yuv` (optional): explicit YUV path for frame-count validation.
- `--output` (optional): output JSON path (default auto-generated under `annotations/`).
- `--min-bbox-width` (optional, default `2.0`): minimum accepted bbox width.
- `--min-bbox-height` (optional, default `2.0`): minimum accepted bbox height.
- `--min-bbox-area` (optional, default `16.0`): minimum accepted bbox area.
- `--overwrite` (optional flag): overwrite output if it exists.
- `--report-json` (optional): diagnostics JSON output path.

### `convert_pandaset_semseg_pkl_to_json.py`

Converts PandaSet semseg `.pkl` data into sparse per-frame semseg JSON for
`rtp_server` semseg test mode.
Location: `scripts/tet_comparison_framework/tools/convert_pandaset_semseg_pkl_to_json.py`

**Usage**
```bash
python3 scripts/tet_comparison_framework/tools/convert_pandaset_semseg_pkl_to_json.py --dataset-dir <sequence_dir> --output <json>
```

**Parameters**

- `--dataset-dir` (required): PandaSet sequence directory (e.g. `.../PandaSet/057`).
- `--output` (required): output sparse semseg JSON path.
- `--camera` (optional, default `front_camera`): camera used for projection.
- `--image-width` (optional, default `1920`): output bounds width.
- `--image-height` (optional, default `1080`): output bounds height.

**Notes**

- The converter requires `numpy` and `pandaset` Python modules.
- `run_tet_comparison.sh` retries conversion using an auto-managed local venv when these modules are missing.

### `yolo/semseg/generate_semseg_config.sh`

Generates DeepStream semseg infer+pipeline YAML files under `yolo/semseg`.

**Usage**
```bash
./scripts/yolo/semseg/generate_semseg_config.sh \
  --video-width 1920 \
  --video-height 1080 \
  --profile default \
  --engine-file /path/to/semseg.engine
```

**Parameters**

- `--video-width` (optional, default `640`): stream width.
- `--video-height` (optional, default `480`): stream height.
- `--infer-width` (optional): inference input width (defaults to `--video-width`).
- `--infer-height` (optional): inference input height (defaults to `--video-height`).
- `--profile` (optional, default `default`): generated file suffix.
- `--onnx-file` (optional): semseg ONNX model path.
- `--tlt-encoded-model` (optional): TAO ETLT model path.
- `--tlt-model-key` (optional, default `tlt_encode`): TAO ETLT key.
- `--engine-file` (optional): semseg TensorRT engine path.
- `--labels-file` (optional): semseg labels path (defaults to generated VCM labels file).
- `--net-scale-factor` / `--offsets` / `--model-color-format` (optional): passthrough pre-processing settings.
- `--num-classes` (optional): explicit `num-detected-classes` override.

### `yolo/semseg/convert_semseg_model.sh`

Fetches a default semseg model and labels into `yolo/semseg`, similar to YOLO model handling.
Default model is `citysemsegformer` (TAO ETLT + Cityscapes labels), with optional legacy
`peoplesemsegnet_shuffle`.

**Usage**
```bash
./scripts/yolo/semseg/convert_semseg_model.sh 1920 1080 ./yolo/semseg
```

**Parameters**

- `<width>` (required): target video width (for logging/compatibility).
- `<height>` (required): target video height (for logging/compatibility).
- `<output_directory>` (required): where semseg assets are stored.
- `[model_name]` (optional, default `citysemsegformer`): semseg model to fetch.
  Supported values: `citysemsegformer`, `peoplesemsegnet_shuffle`.

### `generate_pandaset_front_camera_yuv.py`

Builds deterministic YUV420p from PandaSet front camera JPEG frames.
Location: `scripts/tet_comparison_framework/tools/generate_pandaset_front_camera_yuv.py`

**Usage**
```bash
python3 scripts/tet_comparison_framework/tools/generate_pandaset_front_camera_yuv.py --dataset-dir <PandaSet_root> [options]
```

**Parameters**

- `--dataset-dir` (required): PandaSet dataset root.
- `--camera-dir` (optional, default `camera/front_camera`): camera dir relative to dataset root.
- `--output` (optional): explicit output `.yuv` path.
- `--fps` (optional, default `0`): override fps; `0` means infer from timestamps or fallback to `30`.

## Static Analysis / Environment Checks

### `lib/common_static_analysis.sh`

Location: `scripts/lib/common_static_analysis.sh`

Shared static-analysis function library used by project-specific `run_static_analysis.sh` scripts.

Key enforcement behavior:

- Runs shared additional checks and project-level checks as part of each subproject static-analysis script.
- Treats ownership-contract violations as blocking failures:
  - `return ... .release();`
  - factory-like raw-pointer ownership APIs (`create_*`, `make_*`, `alloc_*`, `clone_*`)
  - owning raw allocations assigned to raw pointers
- Runs `clang-tidy` ownership diagnostics (`cppcoreguidelines-owning-memory`) when
  `compile_commands.json` is available.
- Allows raw-pointer ownership transfer only with explicit inline exemption marker and reason:
  `RAII_EXEMPT: <why raw ownership is unavoidable here>`.

**Parameters**

- No standalone CLI. This file is intended to be sourced by other scripts.

### `setup/bin/doctor_packages.sh`

Read-only system package status checker for specific dependency packages (formerly `check_packages.sh`).

**Parameters**

- None.

**Example**
```bash
./scripts/setup/bin/doctor_packages.sh
```
