# rtp_server `scripts/` Reference

This document describes every script in `rtp_server/scripts`, including wrapper scripts and delegated parameters.

## Script Types

- **Sourced library:** `common_rtp_server_setup.sh`
- **Standalone utility:** `gst_inspect.sh`, `convert_pickle_to_binary.py`
- **Wrappers to shared YOLO scripts:** `convert_yolo_model.sh`, `generate_yolo_config.sh`, `set_cuda_ver.sh` (YOLO setup/check: `scripts/setup/bin/setup_yolo.sh`, `scripts/setup/bin/check_yolo_ready.sh` from repo root)

---

## `common_rtp_server_setup.sh` (Sourced Library)

Shared setup library sourced by `../run.sh`. This file is not intended to be executed directly.

```bash
source scripts/common_rtp_server_setup.sh
```

### Global Variables

- `MODEL_NAME`: detected model family (`yolo`, `peoplenet`, or `unknown`)
- `VCAI_PATH`: resolved repository root
- `HELP_MODE`: when `true`, skips YOLO/model setup
- `RTP_SERVER_CONFIG_FILE`: resolved config file path

### Functions

- `get_vcai_path()`: resolves the VCAI root directory
- `detect_config_file()`: resolves config path from `RTP_SERVER_CONFIG_FILE`, CLI `--config`, or default `rtp_server_config.ini`
- `create_coco_labels_file(labels_file)`: writes COCO labels when missing
- `check_and_setup_yolo(width, height)`: validates YOLO dependencies and creates missing artifacts
- `setup_environment()`: exports plugin/runtime paths (VCAI decoder, optional openhevc, DeepStream/CUDA) and clears GStreamer cache
- `setup_model_config(width, height)`: detects model family and generates model-specific config as needed
- `prepare_execution()`: runs `./build.sh` (unless `NO_BUILD=true`) and determines model type
- `find_rtp_server_binary()`: searches known build output paths for the server binary
- `execute_rtp_server(args...)`: executes `rtp_server` with named-pipe logging to `rtp_server_output.txt`

### Behavior Notes

- `check_and_setup_yolo` and `setup_model_config` require numeric width/height to perform YOLO generation steps.
- `execute_rtp_server` uses a FIFO + `tee` to avoid SIGPIPE issues during Ctrl+C shutdown.

---

## `gst_inspect.sh`

Runs `gst-inspect-1.0` with the same plugin/runtime environment used by `rtp_server`.

```bash
./scripts/gst_inspect.sh [gst_inspect_args...]
```

### Parameters

- Any argument is forwarded to `gst-inspect-1.0` unchanged.
- Common usage is either no arguments (list everything) or a plugin name.

### Examples

```bash
./scripts/gst_inspect.sh
./scripts/gst_inspect.sh h265parse
./scripts/gst_inspect.sh --print-all
```

---

## YOLO Wrapper Scripts

Use `scripts/setup/bin/setup_yolo.sh` and `scripts/setup/bin/check_yolo_ready.sh` from repo root for parser build and readiness. Remaining wrappers below `exec` shared implementations in `../../scripts/yolo/`.

## `convert_yolo_model.sh`

Converts/exports a YOLO model for the requested resolution (rounded to multiples of 32 by the delegated script).

```bash
./scripts/convert_yolo_model.sh <video_width> <video_height> <output_dir> [yolo_version]
```

### Parameters

- `video_width` (required): input width in pixels
- `video_height` (required): input height in pixels
- `output_dir` (required): destination directory for generated model files
- `yolo_version` (optional): YOLO variant suffix (for example `8n`, `8s`, `11n`; default in delegated script: `8n`)

### Delegates To

- `../../scripts/yolo/convert_yolo_model.sh`

---

## `generate_yolo_config.sh`

Generates a YOLO DeepStream config file for the requested resolution/model version.

```bash
./scripts/generate_yolo_config.sh [video_width] [video_height] [yolo_version]
```

### Parameters

- `video_width` (optional): input width in pixels (default in delegated script: `832`)
- `video_height` (optional): input height in pixels (default in delegated script: `480`)
- `yolo_version` (optional): YOLO variant suffix (default in delegated script: `8n`)

### Delegates To

- `../../scripts/yolo/generate_yolo_config.sh`

---

## `check_yolo_ready` (canonical path)

Run from repo root:

```bash
./scripts/setup/bin/check_yolo_ready.sh <video_width> <video_height> [yolo_version]
```

Delegates to `scripts/yolo/check_yolo_ready.sh`.

---

## `set_cuda_ver.sh`

Compatibility wrapper around shared CUDA path/version helpers. Can be sourced or executed.

```bash
# Source mode (for functions and exported vars)
source ./scripts/set_cuda_ver.sh

# Execute mode (prints detected CUDA details)
./scripts/set_cuda_ver.sh
```

### Functions

- `detect_and_set_cuda_version()`: compatibility wrapper that calls shared CUDA detection/export logic
- `get_cuda_library_paths([cuda_version])`: prints candidate CUDA library directories that exist

### Exported Variables

- `CUDA_VER`
- `CUDA_ARCH_BIN`

### Delegates To

- Sources `../../scripts/yolo/common_paths.sh`

---

## `convert_pickle_to_binary.py`

Converts pickle tile data into `.tet` binary format expected by TET processing.

```bash
python3 scripts/convert_pickle_to_binary.py <input_pickle> <output_tet>
```

### Parameters

- `input_pickle` (required): path to source `.pkl` file
- `output_tet` (required): output `.tet` file path

### Notes

- Supports pickle payloads stored as:
  - dictionary with `add_checkerboard`, or
  - dictionary keyed by supported QPs (`22,27,32,37,42,47`), or
  - list/tuple of 6 QP tiles.
- Automatically detects normalization mode (`NORMALIZED`, `CENTERED`, `RAW_DIFF`) from value range.

