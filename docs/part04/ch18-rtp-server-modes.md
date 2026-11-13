# Chapter 18 — rtp_server modes in detail

This chapter explains **all configurable modes** and how they interact. For CLI tables and examples, see **Chapter 14**; for config keys and sections, see the **Config reference** (rtp_server_config.ini).

## Transport mode

`[Server] transport_type` (CLI: `--server-transport-type`). Values: **rtp** | **webrtc**.

| Value | Behavior |
|-------|----------|
| **rtp** | Binary handshake on `handshake_port`; server allocates a dynamic RTP port per client. Client sends H.265 over RTP to that port. |
| **webrtc** | WebRTC transport; requires `[WebRTC]` config (signaling, optional STUN/TURN). |

Selection is implemented in [`rtp_server/src/transport/transport_factory.cpp`](../../rtp_server/src/transport/transport_factory.cpp): `create_rtp_input_transport_context()` for **rtp**, `create_webrtc_transport_context(config)` for **webrtc**. WebRTC requires a non-null config.

## WebRTC signaling

When `transport_type = webrtc`, `[WebRTC] signaling_mode` applies. Loaded in [`rtp_server/src/core/rtp_server_config.cpp`](../../rtp_server/src/core/rtp_server_config.cpp) (`load_webrtc_signaling_mode`); invalid values throw at startup.

| Value | Behavior |
|-------|----------|
| **embedded** | Server hosts the signaling server on `signaling_port` (WebSocket; optional SSL via `signaling_use_ssl`). Simpler deployment. |
| **external** | Client connects to an external signaling server; set `signaling_url` (e.g. `ws://127.0.0.1:8080`). Leave empty when using embedded. |

STUN/TURN URLs and credentials are optional; used when set for NAT traversal.

## TET (Tile Enhancement Tiles)

`[TET] enabled`, `tile_file`, `qp`, `dynamic_qp`. CLI: `--tet-enable`, `--tet-tile-file`, `--tet-qp`.

When **enabled**, TET runs **after** PRI reconstruction and optional EOI luma back-scaling, **before** OSD. Pipeline order: decode → reconstruction → luma → **TET** → overlay → display/file. It applies tile-based post-processing from the configured `.tet` tile file; `qp` is the fallback QP when metadata is unavailable; `dynamic_qp` enables QP extraction from the stream (SPS/PPS). When **disabled**, that stage is a pass-through.

See **Config reference — TET and rest** and **Chapter 25** for the TET comparison framework and BD-rate scripts.

## Test mode (groundtruth evaluation)

`[TestMode] groundtruth_file`, `iou_threshold`, `output_md`, `output_json`. CLI: `--test-mode-groundtruth-file`, `--test-mode-output-md`, `--test-mode-output-json`.

**Active when** `groundtruth_file` is set (COCO-style JSON). The evaluator compares display-branch detections to groundtruth per frame using IoU and class ID; computes TP/FP/FN, precision/recall/F1, mean IoU, mAP; writes a summary at disconnect (and optionally to the configured MD/JSON files). Implemented under [`rtp_server/src/pipeline/output/display/`](../../rtp_server/src/pipeline/output/display/) (`test_mode_*`).

Full field list and script usage: **Chapter 14** and **Chapter 16**.

## RTP output branch

`[RTPOutput] enabled`, `host`, `port`. When **enabled**, the server adds an **additional pipeline branch** that re-encodes decoded video and RTP-forwards it to `host:port`. Use case: forwarding to another system or recorder. Default: **disabled** (display and optional file only).

## File output

`[Server] output_directory`. When **set**, H.265 recordings are saved per client (auto-generated filenames in that directory). When **empty or unset**, no file branch is created (display-only).

## Debug

`[Debug] draw_bitrate`, `draw_fps`, `save_frames`. CLI: `--debug-draw-bitrate`, `--debug-draw-fps`, `--debug-save-frames`.

- **draw_bitrate** / **draw_fps**: Overlays on the display branch.
- **save_frames**: Writes PNGs to `debug_frames/port_<PORT>/`. Path: if `output_directory` is set, under that directory; otherwise under the config file directory. Frames are captured from the display path after OSD by default.

See **Config reference — RTPOutput / Debug**.

## Decoder

`[Decoder] element`. CLI: `--decoder-element` (overrides config).

| Typical value | Use |
|---------------|-----|
| **nvh265dec** | Default; GPU-accelerated (NVIDIA). |
| **openhevcdec** | Software decoder (gst-openhevc); ensure plugin path is discoverable via `GST_PLUGIN_PATH`. |

Only **registered** GStreamer element names are accepted; invalid names cause startup failure.

## Inference

`[Inference] inference_engine`, `deepstream_config_file`, `labels`.

When **inference_engine** is true, the **display branch** runs DeepStream inference (nvinfer) using the configured YAML; overlays (borders, etc.) can use inference results. **labels** is an optional comma-separated filter (e.g. `person,car`). Disabled = no inference in the display path.

## Monitoring

`[Monitoring]`: **enable_packet_loss_check**, **enable_jitter_check**, **enable_silence_check**, plus thresholds (**max_packet_loss_rate**, **max_jitter_ms**, **max_silence_ms**).

Each check is **only active** when its enable flag is true. When active, the server reports warnings when the incoming RTP stream exceeds the configured threshold (packet loss rate, jitter, or silence duration). Default: all checks disabled.

See **Config reference — WebRTC / Monitoring**.

## Related

- **Chapter 14** — overview, test mode fields, CLI tables.
- **Chapter 16** — test mode and TET/BD-rate scripts.
- **Chapter 17** — pipeline stages and TET/Test mode placement.
- **Chapter 25** — TET comparison framework.
- **Config reference** — full rtp_server_config.ini sections.
