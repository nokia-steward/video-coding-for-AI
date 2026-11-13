# encoder_cli Script Library and Utilities

This directory contains:

- non-standalone shell libraries sourced by `encoder_cli/run.sh`
- standalone wrappers that delegate to shared repo scripts under `scripts/yolo/`
- standalone utility/config-generation scripts

YOLO setup entrypoint: `../../scripts/setup/bin/setup_yolo.sh` (run from repo root).

All scripts in this folder are documented below with their CLI behavior.

## Script Types

| Script | Type |
|---|---|
| `common_encoder_setup.sh` | sourced library (non-standalone) |
| `input_handlers.sh` | sourced library (legacy, non-standalone) |
| `convert_yolo_model.sh` | standalone wrapper (delegates) |
| `generate_yolo_config.sh` | standalone wrapper (delegates) |
| `set_cuda_ver.sh` | sourced library + standalone utility |
| `generate_peoplenet_config.sh` | standalone utility |
| `find_roi_overlap_frames.py` | standalone utility |

## Sourced Libraries (Non-Standalone)

### `common_encoder_setup.sh`

Shared runtime helper library used by `encoder_cli/run.sh`.

**Standalone usage**

- Not intended to be executed directly.
- Intended to be sourced:
  ```bash
  source ./scripts/common_encoder_setup.sh
  ```

**Primary functions**

- `detect_config_file`: resolve config file path from env/args/default.
- `setup_environment`: configure GStreamer/CUDA/DeepStream environment variables.
- `check_and_setup_yolo`: ensure YOLO model/artifacts/config readiness.
- `setup_model_config`: generate/update model config (YOLO/PeopleNet flows).
- `prepare_execution`: optional build + model selection prep.
- `execute_encoder_cli`: invoke `encoder_cli` and tee output log.
- `find_encoder_cli_binary`: locate compiled binary.
- `show_common_help`: helper text for run scripts.

**Notable behavior**

- `execute_encoder_cli` injects `--config encoder_config.ini` before forwarded args.
- `prepare_execution` respects `NO_BUILD=true` to skip `./build.sh`.
- Uses/sets globals including `VCAI_PATH`, `MODEL_NAME`, `ENCODER_OUTPUT_FILE`, `HELP_MODE`, `ENCODER_CONFIG_FILE`.

### `input_handlers.sh` (legacy)

Legacy parser helpers for older run-script variants; currently not used by `encoder_cli/run.sh`.

**Standalone usage**

- Not intended to be executed directly.
- Intended to be sourced for shell functions.

**Functions providing legacy CLI contracts**

- `parse_common_flags`
- `parse_video_file`
- `parse_camera_params`
- `parse_rtp_params`
- `parse_camera_rtp_params`
- `parse_video_rtp_params`
- `parse_udp_params`
- `parse_udp_rtp_params`
- `parse_prod_params`

**Legacy usage signatures emitted by this library**

- `<video_file> [-- <additional_args...>]`
- `[camera_device] [width] [height] [framerate] [-- <additional_args...>]`
- `<video_file> [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]`
- `[bind_address] [port] [width] [height] [framerate] [-- <additional_args...>]`
- `[bind_address] [port] [width] [height] [framerate] [rtp_host] [rtp_port] [--webrtc] [-- <additional_args...>]`
- `[width] [height]`

## Wrapper Scripts (Delegated Implementations)

These wrappers execute shared scripts under the repo-level `scripts/yolo/`. For YOLO parser build only, use `scripts/setup/bin/setup_yolo.sh` from repo root.

### `convert_yolo_model.sh`

Export YOLO `.pt` model to DeepStream-compatible ONNX with static dimensions.

**Wrapper target**

- `../../scripts/yolo/convert_yolo_model.sh`

**Usage**
```bash
./scripts/convert_yolo_model.sh <width> <height> <output_directory> [version]
```

**Parameters (delegated, full)**

- `<width>` (required): target video width before YOLO dimension rounding.
- `<height>` (required): target video height before YOLO dimension rounding.
- `<output_directory>` (required): output directory for exported ONNX/model artifacts.
- `[version]` (optional, default `8n`): YOLO version token like `8n`, `8s`, `11m`.

**Validation/behavior notes**

- Requires 3 or 4 arguments.
- Version numeric component must be between 5 and 11.
- YOLO model dimensions are rounded up to multiples of 32.
- Requires `.env/ds_export` and DeepStream-Yolo export utilities.

**Examples**
```bash
./scripts/convert_yolo_model.sh 640 480 ../yolo
./scripts/convert_yolo_model.sh 1920 1080 ../yolo 11m
```

### `generate_yolo_config.sh`

Generate YOLO nvinfer + pipeline YAML files.

**Wrapper target**

- `../../scripts/yolo/generate_yolo_config.sh`

**Usage**
```bash
./scripts/generate_yolo_config.sh [video_width] [video_height] [yolo_version]
```

**Parameters (delegated, full)**

- `[video_width]` (optional, default `832`): source stream width.
- `[video_height]` (optional, default `480`): source stream height.
- `[yolo_version]` (optional, default `8n`): version suffix used in generated file names.

**Behavior notes**

- Internal YOLO infer dimensions are rounded up to multiples of 32.
- Generates:
  - `yolo/yolo_infer_config_<version>.yml`
  - `yolo/yolo_config_<version>.yml`

**Examples**
```bash
./scripts/generate_yolo_config.sh
./scripts/generate_yolo_config.sh 1920 1080 11m
```

## Standalone Utilities

### `set_cuda_ver.sh`

CUDA detection helper that is both sourceable and directly runnable.

**Source usage (library mode)**
```bash
source ./scripts/set_cuda_ver.sh
detect_and_set_cuda_version
get_cuda_library_paths "$CUDA_VER"
```

**Direct usage**
```bash
./scripts/set_cuda_ver.sh
```

**Parameters**

- No CLI parameters.

**Functions**

- `detect_and_set_cuda_version`: detects CUDA and exports compatibility vars.
- `get_cuda_library_paths [cuda_version]`: prints best-effort candidate library paths.

**Exports**

- `CUDA_VER`
- `CUDA_ARCH_BIN`

### `generate_peoplenet_config.sh`

Generate PeopleNet config files with dynamic absolute paths and compatible dimensions.

**Usage**
```bash
./scripts/generate_peoplenet_config.sh [width] [height]
```

**Parameters**

- `[width]` (optional, default `832`): source video width.
- `[height]` (optional, default `480`): source video height.

**Behavior notes**

- Computes PeopleNet-compatible dims (multiple of 16, minimum `480x272`).
- Validates expected model/label/calibration files and warns if missing.
- Generates in `<repo_root>/peoplenet/`:
  - `peoplenet_config.yml`
  - `peoplenet_pgie_config.yml`

### `find_roi_overlap_frames.py`

Analyze ROI JSONL dumps for overlap frames and border-vs-copy mismatches.

**Usage**
```bash
python3 encoder_cli/scripts/find_roi_overlap_frames.py [options]
```

**Options**

- `--mode {auto,display,packing,compare}` (default `auto`):
  - `auto` / `display` / `packing`: overlap detection modes
  - `compare`: border-vs-copy mismatch mode
- `--input PATH` (default `encoder_cli/tracker_dump/display_border_rectangles.jsonl`):
  overlap-mode input JSONL.
- `--display-input PATH` (default `encoder_cli/tracker_dump/display_border_rectangles.jsonl`):
  compare-mode display-border input.
- `--copy-input PATH` (default `encoder_cli/tracker_dump/roi_packing_trace.jsonl`):
  compare-mode copy-stage input.
- `--ignore-timestamp` (default enabled): ignore timestamp ROI in compare mode.
- `--include-timestamp`: include timestamp ROI in compare mode.
- `--compare-raw-destination` (default disabled): compare against raw destination rectangles instead of effective post-clamp rectangles.
- `--min-overlap-area INT` (default `1`): minimum overlap area for overlap modes.
- `--max-report INT` (default `50`): max detailed frames in output.
- `--write-frames PATH`: optional file output (one frame per line).
- `--write-ranges PATH`: optional file output (`start,end,count` per contiguous range).

**Examples**
```bash
# Auto overlap scan
python3 encoder_cli/scripts/find_roi_overlap_frames.py --input encoder_cli/tracker_dump/display_border_rectangles.jsonl

# Packing-only overlap scan with stricter overlap threshold
python3 encoder_cli/scripts/find_roi_overlap_frames.py --mode packing --min-overlap-area 64

# Compare mode between display and copy traces
python3 encoder_cli/scripts/find_roi_overlap_frames.py \
  --mode compare \
  --display-input encoder_cli/tracker_dump/display_border_rectangles.jsonl \
  --copy-input encoder_cli/tracker_dump/roi_packing_trace.jsonl \
  --write-ranges reports/mismatch_ranges.txt
```

## Related Documentation

- `encoder_cli/README.md`
- `encoder_cli/SCRIPTS_README.md`
- shared `scripts/README.md`
