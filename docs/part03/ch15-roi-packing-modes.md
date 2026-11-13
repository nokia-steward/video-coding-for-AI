# Chapter 15 — ROI packing: low_delay vs random_access

ROI packing rearranges detected ROIs into a compact layout and drives **PRI** for SEI (see **PRI and EOI SEI messages**). This page explains **how** the two packer types differ and **why** GOP and display settings change. Usage examples remain in **Part IX**.

## Packer types

| Type | Idea | GOP (encoder) | Frame timing |
|------|------|---------------|--------------|
| **low_delay** | Accumulate ROIs over N frames, then pack with minimal latency | Fixed **4** (low-delay GOP) | Streaming-friendly |
| **random_access** | Buffer by **intraperiod** then process the whole period | **Intra period** from config (random access points) | Batched frames per period |

Configured via `packer_type` in `[ROI]` or `--roi-packer-type`. `pack_rois` must be true and `background_mode` must be **off**.

## Encoder GOP wiring

[`encoder_cli/src/gst/input_pipeline.cpp`](../../encoder_cli/src/gst/input_pipeline.cpp) sets encoder GOP from `packer_type`:

- **random_access** — `gop_size = get_intra_period(data.params)` so IDR/IRAP spacing matches the packer’s intraperiod buffering.
- **low_delay** — `gop_size = 4` for minimal delay.

The correct property name depends on the encoder element (`iframeinterval`, `intra-period`, or `gop-size`) via the descriptor table in [`encoder_cli/src/gst/gpu_elements.cpp`](../../encoder_cli/src/gst/gpu_elements.cpp).

## Random access thread

`random_access_frame_processing` runs in a dedicated loop: it waits for **complete intraperiods** from the random_access packer and processes all frames in each period in order. That avoids feeding the encoder with half-finished periods.

The main ROI thread delegates to the packer when `get_active_packing_mode` is random_access and `process_frame` returns true — see [`encoder_cli/src/processing/roi_detection_processing/api.cpp`](../../encoder_cli/src/processing/roi_detection_processing/api.cpp) (~149–158).

## Display branch behavior

When `packer_type == random_access`, frames can arrive in **batches**. [`encoder_cli/src/gst/output_pipeline/output_branches/display/display_branch.cpp`](../../encoder_cli/src/gst/output_pipeline/output_branches/display/display_branch.cpp) adjusts:

- **Parser** — `disable-passthrough` is **false** for random_access so batches are not dropped at the parser.
- **Queue / sink** — leaky mode, buffer caps, and **sync** are tuned with packer_type in log messages (~750–920) so display keeps up without stalling.

Low_delay keeps passthrough disabled where appropriate for steady single-frame flow.

## Packing algorithm surface

[`encoder_cli/src/processing/roi_detection_packing.h`](../../encoder_cli/src/processing/roi_detection_packing.h) defines:

- **`packing_result`** — `packed_rectangles`, `original_rois` (for SEI), `original_to_packed_mapping`, `is_predicted` per original ROI.
- **`perform_detection_packing`** — runs packing for a frame given accumulated detections and split rectangles.

Optional **trace logging** (`packer_logging` / tracker_dump) records stages and copy rectangles for debugging (`dump_packing_trace_stage`, `dump_copy_rectangles_trace`).

## Config snippet (packer + accumulation)

Committed reference — `[ROI]` packer and accumulation:

```ini
--8<-- "encoder_cli/encoder_config.ini:267:327"
```

Note: low_delay accumulation count is `num_frames` in this file (range 1–10; comments recommend 3).

## Related

- **Part IX** — ROI packing CLI/ini examples and trade-offs.
- **Chapter 14** — thread dispatch.
- **PRI and EOI SEI messages** — where packed/original ROIs become SEI.
