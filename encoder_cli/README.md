# encoder_cli

GStreamer-based H.265 encoder that can run detection (DeepStream + YOLO/PeopleNet), produce ROI-driven processing, inject SEI metadata, and output to RTP/WebRTC/display/file branches.

For scripts using Python `argparse` or GLib option parsing, `-h`/`--help` is available.

## Prerequisites

1. GStreamer development packages:
   ```bash
   sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
   ```
2. NVIDIA DeepStream SDK installed.
3. YOLO export environment from repo root:
   ```bash
   ./scripts/setup/bootstrap.sh ds-env
   ```
4. DeepStream-Yolo custom library (from this directory):
   ```bash
   ./scripts/setup_yolo.sh
   ```

For full `bootstrap.sh` setup command details, see [Host Setup Bootstrap Guide](../docs/part07/scripts-setup-readme.md).

## Build Script Reference

### `build.sh`

Builds `encoder_cli` (and optionally cleans or builds debug).

**Usage**
```bash
./build.sh [clean] [debug]
```

**Parameters**

- `clean` (optional): remove `../build/encoder_cli` before building.
- `debug` (optional): build with `-DCMAKE_BUILD_TYPE=Debug`.

**Notes**

- Arguments are order-independent (`./build.sh debug clean` works).
- The script checks for:
  - `../.env/ds_export`
  - `../yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo/libnvdsinfer_custom_impl_Yolo.so`

## Bridge Boundaries

`encoder_cli` now keeps appsink/appsrc bridge operations behind dedicated ingress and egress boundaries.

Ingress side:

- `src/bridge/core/ingress_bridge_core.*` for media-only `appsink` pull and timeout/EOS state mapping.
- `src/bridge/orchestration/ingress_bridge_orchestrator.*` for callback/polling queue handoff and ingress EOS policy.
- `src/bridge/app/ingress_app_adapter.h` for non-media ingress side effects (queue/counter updates and fail-fast stop signaling).
- `src/bridge/context/ingress_bridge_*` for per-instance ingress runtime/counter/probe state.
- lifecycle queue closes are routed through centralized idempotent helpers in `src/global_data.*` to keep restart behavior consistent.

Egress side:

- `src/bridge/core/encoded_bridge_core.*` for media-only `appsrc` push/EOS logic.
- `src/bridge/orchestration/encoded_bridge_orchestrator.*` for flow policy (`ok`, `recoverable_drop`, `fatal_stop`, `drain_complete`) and lifecycle orchestration.
- `src/bridge/orchestration/encoded_bridge_startup_contract.*` for one-time startup appsrc property/caps initialization and per-instance caps-lock establishment.
- `src/bridge/app/encoder_app_adapter.h` for non-media side effects (status counters and fail-fast stop signaling).
- `src/bridge/context/encoder_bridge_*` for per-instance egress endpoint wiring, startup markers, caps-lock runtime, and EOS state.
- strict contract behavior: post-lock `appsrc` caps mismatch is treated as fatal (no dynamic renegotiation path in push flow).

Worker lifecycle side:

- `src/application/worker_lifecycle_runtime.h` tracks per-instance worker lifecycle phase (`idle`, `running`,
  `stopping`, `drained`) and cycle generation.
- `src/application/thread_management.*` provides a single mode-agnostic worker bundle API for deterministic
  start/stop/join behavior across file/camera/udp paths.
- `global_data::request_worker_stop_once(...)` is the single stop contract used by orchestration and bridge fail-fast
  adapters to avoid duplicated stop/queue-close paths.
- bypass polling workers (`appsink` inference/bypass + merger consumer) now use explicit restart/join hooks for
  reconnect/reset cycles.

Memory contract side:

- `src/bridge/context/memory_policy_runtime.h` keeps per-instance memory-domain and mutability fallback counters.
- `src/processing/memory_contract_helper.*` centralizes memory-feature classification, writable-buffer fallback, and
  deep-copy fallback policy used by CPU-bound stages.
- ROI ingress PTS restoration uses controlled writable fallback (instead of implicit mutable assumptions) and records
  fallback counters.
- Raw/encoded CPU map paths use controlled deep-copy fallback before drop/fatal decisions, and counters are exported via
  memory/performance reporting.
- `global_data::reset_bridge_lifecycle_state()` resets memory contract runtime counters/expectations for deterministic
  restart cycles.
- `src/utils/memory_monitor.*` supports **multiple** PTS-queue and memory-fallback callbacks (one registration per
  `performance_monitor` / `global_data`); `get_memory_stats()` **aggregates** (sums) their values. Callbacks unregister
  in `~performance_monitor()` so sequential multi-instance use does not leak stale pointers.

Metadata contract side:

- `src/bridge/context/metadata_contract_runtime.h` tracks per-instance strict frame sequence state and counters
  (enqueued/dequeued bundles, mismatches, validation failures, fail-stops).
- `src/gst/input_types/frame_path_meta.*` now carries both frame path identity and strict contract sequence id; ingress
  no longer relies on PTS side-channel fallback maps.
- ROI/encoded/output metadata queues (`packed_regions_queue`, `display_regions_queue`, `debug_regions_queue`) carry
  sequence-tagged bundles via `metadata_frame_bundle`.
- `encoded_injection_bridge_orchestrator.*` enforces sequence-aligned bundle consumption per encoded frame and
  fail-fast stops on missing/out-of-order/invalid metadata contract state.
- display/debug output consumers validate sequence/order against encoded-output PTS->sequence registration before
  applying overlays; drift is handled as fail-fast stop per contract policy.

UDP input and output-branch probe runtime (per `global_data` instance):

- `src/global_data_instance_runtime.h` holds UDP packet/frame-probe counters, the UDP `frame_drop_manager`, and mutable
  display/debug branch state (metadata reorder buffers, border-dump stream, overlay throttling, warn-once flags).
- Helpers in `src/gst/input_types/udp_input.*` take `global_data&` (or `const global_data&` for read-only queries) so
  stats and connection state are not shared across instances or test fixtures.
- `global_data::reset_processing_state()` resets frame-probe counters, frame-drop manager state, and display/debug
  runtime so reconnect cycles do not leak probes or warnings from a prior client.
- Output pipeline diagnostic probes (`src/gst/output_pipeline/probes.cpp`) keep frame counters and random_access
  timestamp-repair state in `global_data::output_probe_runtime` (per-instance), cleared on `reset_processing_state()`.

Processing decomposition side:

- `src/bridge/core/random_access_bridge_core.*` and `src/bridge/orchestration/random_access_bridge_orchestrator.*` for random-access period/frame bridge processing.
- `src/bridge/core/encoded_injection_bridge_core.*` and `src/bridge/orchestration/encoded_injection_bridge_orchestrator.*` for encoded PRI/EOI injection decisions and SEI forwarding policy.
- `src/bridge/context/random_access_bridge_context.h` and `src/bridge/context/encoded_injection_bridge_context.h` for per-instance processing runtime wiring.

App adapter/controller side:

- `src/bridge/context/encoder_cli_app_host.h` defines the abstract host for bridge adapters; `global_data` implements it.
- `src/bridge/app/pipeline_app_adapter.h` (header-only) forwards processing-loop non-media side effects to the host.
- `src/bridge/context/encoded_stream_ports.h` groups `encoding_src` / `sei_src` handles for egress wiring.
- `src/gst/output_pipeline/app/output_branch_controller.*` owns output branch lifecycle orchestration (branch selection, element creation/configuration, signal setup, add/link order, and fakesink fallback).
- `src/gst/output_pipeline/api.cpp` is now a thin composition entrypoint delegating branch lifecycle flow to `output_branch_controller`.
- `src/gst/input_pipeline_encoding.cpp` holds `create_encoding_pipeline()`; see `docs/part03/encoder-cli-threading-and-migration.md` for threading/migration notes.

## Run Script Reference

### `run.sh`

Primary wrapper for running `encoder_cli` with standardized mode selection and pass-through app flags.

**Usage**
```bash
./run.sh <input_mode> <output_mode> [<input_arg>] [<output_arg>] [-- <encoder_cli_args...>]
./run.sh <input_mode> <output_mode> -o <output_arg> [-- <encoder_cli_args...>]
./run.sh -- --help
```

**Positional parameters**

- `<input_mode>` (required): `file`, `camera`, or `udp`.
- `<output_mode>` (required): `rtp` or `webrtc`.

**Input argument behavior**

- `file`: optional `<input_arg>` is video path.
  - If provided, filename must match `NAME_WIDTHxHEIGHT[_FPS].ext`.
  - Width/height/fps are parsed from filename.
- `camera`: optional `<input_arg>` is camera device path (example `/dev/video0`).
- `udp`: no `<input_arg>` (UDP settings come from config / pass-through app args).

**Output argument behavior**

- Optional `<output_arg>` for both `rtp` and `webrtc`:
  - host (`192.168.1.10`)
  - host:port (`192.168.1.10:5000`)
  - for `webrtc` only, signaling URL (`ws://127.0.0.1:8080`)
- Use `-o <output_arg>` when you want to pass output without an input arg (disambiguation).

**Pass-through behavior (`--`)**

- Any arguments after `--` are forwarded directly to `encoder_cli`.
- Example: `./run.sh file rtp video_1920x1080_30.yuv -- --encoder-qp 37 --debug-all`

**Wrapper behavior summary**

- Always routes through `scripts/common_encoder_setup.sh` for environment/model prep.
- Builds by default (`./build.sh`) before execution.
- Writes runtime output to `encoder_output.txt`.
- Automatically sets:
  - `--common-input-type`
  - transport selection (`--communication-transport-type`)
  - parsed file dimensions for `file` mode
  - optional host/port/signaling arguments derived from wrapper args

**Examples**
```bash
# File input, RTP output
./run.sh file rtp /path/to/video_1920x1080_30.yuv

# Camera input, RTP output
./run.sh camera rtp /dev/video0

# UDP input, WebRTC output
./run.sh udp webrtc

# WebRTC signaling URL through -o disambiguation
./run.sh file webrtc /path/to/video_1920x1080_30.yuv -o ws://127.0.0.1:8080

# Forward encoder binary options after --
./run.sh file rtp /path/to/video_1920x1080_30.yuv -- --encoder-qp 32 --roi-pack-rois true
```

## Utility Script Reference

### `run_static_analysis.sh`

Runs static analysis and writes text/XML/HTML outputs.

**Usage**
```bash
./run_static_analysis.sh [ANALYSIS_DIR] [TIMESTAMP]
```

**Parameters**

- `ANALYSIS_DIR` (optional, default `../static_analysis_results`): output directory.
- `TIMESTAMP` (optional, default current datetime): suffix for output filenames.
- `-h`, `--help`: show usage help.

### `show_webcam.sh`

Launches a fixed `gst-launch-1.0` pipeline for `/dev/video2`.

**Parameters**

- None.

## Configuration

`run.sh` and the binary rely on `encoder_config.ini` (unless overridden via binary `--config` pass-through).

Key areas in config:

- `[Common]` input defaults and dimensions
- `[Video]` source file and format settings
- `[ROI]` detection/packing controls
- `[Communication]` RTP/WebRTC host and port defaults
- other specialized sections for detection/tracking/debug

## encoder_cli Binary CLI Reference (Exhaustive)

Binary help:
```bash
./build/encoder_cli/encoder_cli --help
```

### Common / Config

| Option | Value | Description |
|---|---|---|
| `--config`, `-c` | path (required) | Configuration file path. |
| `--common-input-type` | `file` \| `camera` \| `udp` | Input type override. |
| `--output-directory` | path | Root directory for saved debug/original frames. |

### Video / Camera / UDP Input

| Option | Value | Description |
|---|---|---|
| `--video-file-name` | path | Input video file path. |
| `--video-format` | format string | Raw video format (example `I420`, `NV12`). |
| `--width` | int | Video width in pixels. |
| `--height` | int | Video height in pixels. |
| `--framerate` | int | Video framerate in fps. |
| `--background-color` | int | Background color value for empty buffers. |
| `--camera-device` | path | Camera device path (example `/dev/video0`). |
| `--udp-bind-address` | address | UDP bind address (example `0.0.0.0`). |
| `--udp-port` | int | UDP source port. |
| `--udp-encoding` | string | RTP encoding name (`H264`/`H265`). |
| `--udp-payload-type` | int | RTP payload type. |
| `--udp-clock-rate` | int | RTP clock rate. |
| `--udp-latency-ms` | int | UDP jitterbuffer latency (ms). |

### ROI / Detection / Tracking / Stabilization

| Option | Value | Description |
|---|---|---|
| `--deepstream-config-file` | path | DeepStream config file path. |
| `--roi-labels` | csv string | Comma-separated object labels. |
| `--roi-data-file` | path | Precomputed ROI data file path. |
| `--roi-static-json-file` | path | Static ROI JSON file path. |
| `--roi-groundtruth-file` | path | COCO groundtruth JSON path for per-frame ROI source. |
| `--roi-inference-engine` | flag | Enable dynamic inference-based ROI detection. |
| `--roi-pack-rois` | `true` \| `false` | Enable/disable ROI packing (explicit boolean string). |
| `--roi-packer-type` | `low_delay` \| `random_access` | Select packer type. |
| `--roi-num-frames` | int | Number of frames to accumulate for low-delay packer. |
| `--roi-fixed-margin-px` | int | Fixed per-side ROI margin in pixels. |
| `--roi-margin-mode` | `off` \| `content` \| `placement` | ROI margin behavior mode. |
| `--roi-temporal-resampling` | int | Temporal resampling factor. |
| `--roi-background-mode` | `off` \| `background_blur` \| `qp_map_delta` | Select ROI background processing mode. |
| `--roi-qp-map-background-delta` | int | Background CTB QP delta used by `qp_map_delta` (default `10`). |
| `--enable-luma-adaptation` | `true` \| `false` | Enable encoder-side luma scaling and EOI luma signaling. |
| `--luma-downscale-factor` | float | Static luma downscale factor `(0.0, 1.0]` when adaptive luma is off. |
| `--enable-adaptive-luma-adaptation` | `true` \| `false` | Use ONNX input analyzer to predict (d, u) per frame/keyframe. |
| `--input-analyzer-model-path` | path | Path to adaptive luma model (relative to encoder_cli folder). Use `.onnx` or `.pt`; `run.sh` converts `.pt` to `.onnx` when needed. |
| `--adaptive-luma-analyzer-on-keyframes-only` | `true` \| `false` | Run analyzer only on keyframes (default `true`). |
| `--roi-sigma` | float | Gaussian blur sigma. |
| `--roi-kernel-size` | int | Gaussian kernel size (`0` = auto). |
| `--tracker-config-file` | path | Tracker config path. |
| `--tracker-type` | `IOU` \| `NvSORT` \| `NvDCF` \| `NvDeepSORT` | Tracker implementation. |
| `--tracker-max-shadow-tracking-age` | int | Max shadow tracking age. |
| `--tracker-enable-reassoc` | flag | Enable trajectory re-association. |
| `--tracker-trajectory-projection-length` | int | Trajectory projection length. |
| `--tracker-max-tracklet-matching-time-range` | int | Tracklet matching search range. |
| `--detection-base-confidence-threshold` | float | Base confidence threshold (0.0-1.0). |
| `--detection-maximum-missing-frames` | int | Max missing-frame tolerance for detection handling. |
| `--detection-enable-prediction` | flag | Enable predicted ROI fallback. |
| `--stabilization-enabled` | `true` \| `false` | Enable/disable stabilization (explicit boolean string). |
| `--stabilization-mode` | `none` \| `ema` \| `threshold` \| `hybrid` | Stabilization mode. |
| `--stabilization-alpha` | float | Position smoothing factor. |
| `--stabilization-alpha-size` | float | Size smoothing factor. |
| `--stabilization-min-position-delta` | int | Minimum position delta to update. |
| `--stabilization-min-size-delta` | int | Minimum size delta to update. |
| `--stabilization-velocity-adaptive-factor` | float | Velocity influence for adaptive alpha. |

### Encoder / Debug / Logging

| Option | Value | Description |
|---|---|---|
| `--encoder-qp` | int `0..51` | Constant QP override (disables CBR-style control path). |
| `--encoder-element` | string | GStreamer encoder factory name (e.g. `nvh265enc`, `nvv4l2h265enc`, `kvazaarenc` or alias `kvazaar`). Kvazaar uses system-memory I420; the plugin must be in `lib/kvazaar` (run `./scripts/setup/bootstrap.sh kvazaar`). `encoder_cli` uses the standalone `lib/kvazaar` install exposed via an isolated plugin directory. Unsupported names exit at startup. Overrides `[Encoder] element` in INI. |
| `--debug-all` | flag | Enable all debug toggles and branches. |
| `--debug-draw-borders` | flag | Draw ROI borders. |
| `--debug-draw-rtp-timestamp` | flag | Overlay RTP timestamp. |
| `--debug-draw-fps` | flag | Overlay FPS. |
| `--debug-draw-bitrate` | flag | Overlay bitrate. |
| `--debug-draw-bitrate-large` | flag | Large red bitrate overlay. |
| `--debug-save-frames` | flag | Save per-frame PNG outputs. |
| `--debug-enable-display-branch` | flag | Enable display branch. |
| `--debug-enable-mp4-branch` | flag | Enable MP4 branch. |
| `--debug-enable-h265-branch` | flag | Enable raw H.265 branch. |
| `--enable-status-reporter` | flag | Enable periodic status reporter. |
| `--logging-packer` | flag | Enable detailed ROI packer logs. |

### Communication / WebRTC

| Option | Value | Description |
|---|---|---|
| `--communication-transport-type` | `rtp` \| `webrtc` | Output transport mode. |
| `--communication-host` | host | Destination/signaling host (mode-dependent). |
| `--communication-port` | int | Destination/signaling port (mode-dependent). |
| `--communication-timeout-ms` | int | Communication timeout in ms. |
| `--webrtc-signaling-url` | URL | WebRTC signaling endpoint (example `ws://host:port`). |
| `--webrtc-stun-server` | URL | STUN server URL. |
| `--webrtc-turn-server` | URL | TURN server URL. |
| `--webrtc-turn-username` | string | TURN username. |
| `--webrtc-turn-password` | string | TURN password. |
| `--webrtc-ice-timeout-ms` | int | ICE timeout in ms. |
| `--webrtc-enable-data-channels` | flag | Enable WebRTC data channels. |

## Option Interaction Notes

- `--config/-c` is required by the binary. `run.sh` supplies `--config encoder_config.ini` automatically.
- If you pass another `--config` via `run.sh ... -- --config <file>`, that pass-through argument is appended after the wrapper default and is used by parser order.
- `run.sh` is mode-first: choose `<input_mode> <output_mode>`, then pass binary overrides after `--`.
- `qp_map_delta` is supported only on x86 desktop systems; requesting it on non-x86 exits with a startup validation error.
- On x86 desktop systems, `qp_map_delta` can still fall back to `background_blur` when runtime support cannot be confirmed.
- `--enable-luma-adaptation` signaling is per-frame and conditional: luma-adaptation EOI is emitted only when sender-side
  luma downscaling succeeds for that frame.
- Luma-adaptation EOI can be combined with object-based EOI (for example in `background_blur` / `qp_map_delta` flows)
  when both are effective for the same IRAP frame.
- **Adaptive luma**: when `enable_adaptive_luma_adaptation` is true, the encoder loads the ONNX model at startup (path from `input_analyzer_model_path`). If loading fails, the application exits. A PyTorch `.pt` model can be auto-converted to ONNX by `run.sh` when the `.onnx` file is missing or older than the `.pt`; that conversion requires Python with **torch** and **torchvision** (`pip install torch torchvision`). See `encoder_cli/scripts/export_input_analyzer_onnx.py` and `aidoc/input_analyzer_example.py`. When the analyzer fails for a single frame mid-run, the previous frame's (d, u) is reused. **Build**: the encoder is built with ONNX Runtime inference only if CMake finds ONNX Runtime (e.g. set `CMAKE_PREFIX_PATH` to the install prefix of an [ONNX Runtime release](https://github.com/microsoft/onnxruntime/releases)); otherwise a stub is linked and adaptive luma will not run inference. Use `-DENABLE_ONNXRUNTIME=OFF` to disable the lookup. To enable ONNX inference for the input analyzer, install ONNX Runtime and set `CMAKE_PREFIX_PATH`; see [Installing ONNX Runtime](docs/part03/install-onnx-runtime.md) (or the manual).
- Explicit boolean-string options require `true`/`false` values:
  - `--roi-pack-rois`
  - `--stabilization-enabled`

## Related Docs

- `encoder_cli/SCRIPTS_README.md`
- `encoder_cli/scripts/README.md`
- root `INSTALL.md`
