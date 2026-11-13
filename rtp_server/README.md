# rtp_server

A GStreamer-based RTP streaming server that receives H.265 video streams, applies VCAI SEI processing via the
`vcai_decoder` plugin chain, and renders ROI overlays on decoded frames. It supports multiple simultaneous client
connections, RTP input and WebRTC transports, and optional video recording.

## Overview

The rtp_server acts as a receiver for H.265 streams produced by encoder_cli. It:

- Listens for client connections on a handshake port
- Dynamically allocates RTP ports for each client
- Receives and decodes H.265 video streams
- Extracts PRI/EOI SEI payloads via `sfh265seiextract`
- Applies decoded-frame processing with `sfprireconstruct`, `sfeoilumabackscale`, and `sftetapply`
- Renders ROI overlays and optional debug information
- Displays video in real-time
- Optionally saves streams to H.265 files
- Supports frame reconstruction from PRI data

Each client session uses **one** top-level `GstPipeline`: RTP or WebRTC input, H.265 depayload/parse, internal `queue`
elements, the VCAI decoder/mutation chain (`sfh265seiextract` through `sftetapply`), GPU conversion, and
display/file/RTP debug branches are all linked inside that single bin (there is no separate appsink/appsrc bridge
between split pipelines).

### Supported Features

- **Multiple clients**: Handle multiple simultaneous video streams
- **Transport modes**: RTP input (default) or WebRTC
- **PRI SEI extraction**: Parse custom ROI metadata from H.265 streams
- **EOI luma adaptation support**: Apply SEI-driven luma back-scaling after PRI reconstruction and before TET
- **ROI visualization**: Render bounding boxes on decoded frames
- **Debug overlays**: Bitrate and FPS display
- **Debug frame capture**: Save final reconstructed frames as PNGs (including display overlays)
- **Video recording**: Save streams to H.265 files
- **TET post-processing**: Tile-based enhancement via the `sftetapply` element in the decoder plugin (not app-side)

## Prerequisites

Before building rtp_server, ensure you have:

1. **System GStreamer 1.20+** development/runtime packages installed:
   ```bash
   # From project root
   ./scripts/setup/bootstrap.sh core
   ```

2. **NVIDIA DeepStream SDK 7.1** installed (for GPU-accelerated decoding and overlays)

3. **DeepStream YOLO export environment** (if using YOLO features):
   ```bash
   ./scripts/setup/bootstrap.sh ds-env
   ```

See `INSTALL.md` in the project root for detailed setup instructions.
For full `bootstrap.sh` setup command details, see [Host Setup Bootstrap Guide](../docs/part07/scripts-setup-readme.md).

## Building

```bash
./build.sh
```

To clean and rebuild:

```bash
./build.sh clean
```

To build with AddressSanitizer:

```bash
./build.sh asan
```

To build with debug symbols:

```bash
./build.sh debug
```

The build script verifies that system `gstreamer-1.0` is available via `pkg-config` and also builds the local
`gstvcai_decoder` plugin target used by the processing chain.

## Configuration

The application uses `rtp_server_config.ini` for configuration. Key sections:

- **[Server]**: Handshake port, transport type, output directory
- **[WebRTC]**: Signaling configuration, STUN/TURN servers
- **[Common]**: Background color, shared settings
- **[RTPOutput]**: H.265 RTP forwarding destination
- **[Jitter]**: Jitter buffer configuration
- **[RTP]**: RTP protocol settings
- **[Pipeline]**: GStreamer pipeline configuration
- **[Debug]**: Overlay toggles and debug frame capture (`save_frames`)
- **[TET]**: Tile enhancement post-processing
- **[Inference]**: Detection model/config inputs
- **[TestMode]**: Groundtruth evaluation and summary outputs

When `[Debug] save_frames=true`, PNGs are written to
`<output_directory>/debug_frames/port_<PORT>/` if `[Server] output_directory` is set;
otherwise they are written under the active config file directory:
`<config_directory>/debug_frames/port_<PORT>/`.
Saved PNGs are captured from the display path (post-OSD) by default, so border overlays
follow the display/inference configuration and are not drawn from PRI metadata in the
debug branch. AH0187 semseg battery runs use a private script gate to capture pre-OSD
clean frames instead.

## Test Mode

Test mode in `rtp_server` supports bounding-box evaluation.

### COCO Bounding Box Evaluation

The evaluator compares detection boxes to groundtruth boxes frame-by-frame
using class-aware IoU matching.

- Matches detections to groundtruth per frame using IoU and class IDs
- Computes TP/FP/FN, precision/recall/F1, mean IoU, and mAP@IoU-threshold
- Writes a final summary at disconnect (plus optional output files)

### How To Use

Configure test mode by setting a groundtruth file, then optionally tune IoU threshold and output files:

```bash
# COCO JSON bbox groundtruth
./run.sh rtp -- \
  --test-mode-groundtruth-file ../Traffic_2560x1600_30_val/annotations/Traffic_2560x1600_30_val.json \
  --test-mode-output-md /tmp/test_mode.md \
  --test-mode-output-json /tmp/test_mode.json

```

Configuration file options (in `[TestMode]`):

- `groundtruth_file`: Path to COCO-style groundtruth JSON
- `test_mode_iou_threshold`: IoU threshold for a match (default 0.5)
- `output_md`: Write final summary to a markdown file (optional)
- `output_json`: Write final summary to a JSON file (optional)

Command line overrides:

- `--test-mode-groundtruth-file`
- `--test-mode-output-md`
- `--test-mode-output-json`

For AH0187 semantic-segmentation battery runs, use
`scripts/run_tet_comparison.sh --mode semseg`. That path captures clean frames
from `rtp_server` and evaluates them with the AH0187 Detectron2 backend.

### Output Fields

**Summary fields**

- `port`: RTP port for the client session
- `iou_threshold`: IoU threshold used for matching (COCO mode)
- `has_groundtruth`: Whether any frames with groundtruth were evaluated
- `frames_with_groundtruth`: Number of frames that had groundtruth data
- `groundtruth_total`: Total number of groundtruth boxes/classes evaluated
- `detection_total`: Total number of detections evaluated
- `true_positive` (TP): Detections matched to groundtruth by IoU and class
- `false_positive` (FP): Detections that did not match any groundtruth
- `false_negative` (FN): Groundtruth objects/classes that were not matched by any detection
- `precision`: TP / (TP + FP)
- `recall`: TP / (TP + FN)
- `f1_score`: Harmonic mean of precision and recall
- `mean_iou`: Average IoU for matched TP pairs
- `map_50`: Mean Average Precision at the configured IoU threshold

Semseg summary fields are produced by `scripts/run_tet_comparison.sh --mode semseg`
using the AH0187 evaluator backend.

**Per-frame fields**

- `frame_index`: Frame number
- `groundtruth_count`: Groundtruth boxes/classes in the frame
- `detection_count`: Detections in the frame
- `true_positive`, `false_positive`, `false_negative`: Per-frame TP/FP/FN
- `precision`, `recall`, `f1_score`, `mean_iou`: Per-frame metrics
- `detection_classes`: Comma-separated detection class IDs (and names when available)
- `groundtruth_classes`: Comma-separated groundtruth category IDs (and names when available)

### BD-rate Evaluation

For multi-QP BD-rate evaluation (TET-on vs TET-off across multiple QP points), use
the dedicated script:

```bash
./scripts/tet_comparison_framework/run_tet_bdrate.sh --input <file> --groundtruth <json> --tile <tet>
```

This runs the encoder at each QP value (default: 22, 27, 32, 37, 42, 47) with TET
enabled and disabled, then aggregates the detection metrics and measured bitrates
into a CSV for BD-rate computation. See `scripts/README.md` for details.

## Running

See `SCRIPTS.md` for detailed documentation of all run scripts and their parameters.

### Quick Examples

```bash
# Start server with RTP input transport (config defaults)
./run.sh rtp

# Start server with WebRTC transport
./run.sh webrtc

# Start server with custom RTP output destination
./run.sh rtp 192.168.1.100:5006

# Start server with debug overlays
./run.sh rtp -- --debug-draw-bitrate --debug-draw-fps

# Save final reconstructed debug frames as PNG files
./run.sh rtp -- --debug-save-frames

# Test the server with a test client
./test_client.sh
```

## Architecture

```
[RTP Client] ──RTP Input/WebRTC──> [Handshake] ──> [Port Allocation]
                                                    │
                                                    ▼
[H.265 Stream] ──RTP──> [Receive Pipeline] ──> [PRI/EOI Extraction] ──> [Decode] ──> [Overlay] ──> [Display]
                                                                                      │
                                                                                      ▼
                                                                               [File Output]
```

### Pipeline Details

1. **Handshake**: Client connects, server allocates RTP port
2. **Receive Pipeline**: RTP input source (`udpsrc`) → `rtpjitterbuffer` → `rtph265depay`
3. **SEI extraction**: `sfh265seiextract` extracts PRI/EOI payloads from the encoded stream
4. **Decode**: H.265 decoder — default `nvh265dec` (NVIDIA); optional `openhevcdec` (software, gst-openhevc)
5. **Post-decode processing**: `sfprireconstruct` -> `sfeoilumabackscale` -> `sftetapply`
6. **Overlay**: ROI bounding boxes, bitrate/FPS display via DeepStream OSD
7. **Output**: Display sink and optional file recording

## Command Line Options

Run with `--help` for all available options:

```bash
./build/rtp_server/rtp_server/rtp_server --help
```

Key options:

| Option | Description |
|--------|-------------|
| `--config`, `-c` | Path to configuration file |
| `--server-handshake-port` | Handshake port (overrides config) |
| `--server-transport-type` | Transport type: `rtp` or `webrtc` |
| `--server-output-directory` | Directory to save H.265 recordings |
| `--debug-draw-bitrate` | Enable bitrate overlay on frames |
| `--debug-draw-fps` | Enable FPS overlay on frames |
| `--debug-save-frames` | Save final reconstructed frames as PNG files |
| `--rtp-output-host` | RTP output destination host |
| `--rtp-output-port` | RTP output destination port |
| `--rtp-output-payload-type` | RTP payload type for RTP output forwarding (0-127) |
| `--test-mode-output-md` | Write final test mode summary to markdown file |
| `--test-mode-output-json` | Write final test mode summary to json file |

### WebRTC Options

| Option | Description |
|--------|-------------|
| `--webrtc-signaling-port` | Signaling server port (default: 8080) |
| `--webrtc-signaling-ssl` | Enable SSL for signaling (WSS) |
| `--webrtc-stun-server` | STUN server URL |
| `--webrtc-turn-server` | TURN server URL |
| `--webrtc-turn-username` | TURN username |
| `--webrtc-turn-password` | TURN password |

### TET Options

| Option | Description |
|--------|-------------|
| `--tet-enable=true\|false` | Enable or disable TET post-processing |
| `--tet-tile-file` | Path to TET tile file (.tet) |
| `--tet-qp` | Fallback QP value (0-51) |

### Decoder Options

| Option | Description |
|--------|-------------|
| `--decoder-element` | GStreamer decoder factory name (default `nvh265dec`; `openhevcdec` for software decode). Must be registered; overrides `[Decoder] element` in INI. |

## Testing

Use the included test client to verify server operation:

```bash
# Stream test pattern
./test_client.sh

# Stream with custom settings
./test_client.sh --pattern smpte --width 1280 --height 720

# Stream H.265 file
./test_client.sh video.h265

# Connect to remote server
./test_client.sh --server 192.168.1.100
```

## Utility Scripts

| Script | Description |
|--------|-------------|
| `play_h265.sh` | Play recorded H.265 files with various players |
| `test_client.sh` | Test client for streaming to the server |

## Directory Structure

```
rtp_server/
├── src/
│   ├── main.cpp              # Application entry point
│   ├── core/                 # Core server logic
│   ├── pipeline/             # GStreamer pipeline management
│   ├── transport/            # RTP input and WebRTC transport
│   ├── client/               # Client connection handling
│   ├── gst/                  # GStreamer utilities
│   └── utils/                # Helper utilities
├── scripts/
│   ├── common_rtp_server_setup.sh  # Shared setup library for run scripts
│   └── ...                   # YOLO wrappers, CUDA, GStreamer utilities
├── tiles/                    # TET tile files
└── rtp_server_config.ini     # Main configuration
```
