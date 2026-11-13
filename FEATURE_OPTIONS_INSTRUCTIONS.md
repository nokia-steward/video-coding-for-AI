# Feature Option Instructions (encoder_cli + rtp_server)

This document explains how to use and combine these options:

- **encoder_cli**: ROI packing, background blur, QP map (`qp_map_delta`), luma adaptation
- **rtp_server**: TET on/off

It focuses on **compatibility and usage** only. Quality and bitrate depend on content, encoder QP, and pipeline; validate on your own streams.

## Contents

- [Compatibility matrix](#1-compatibility-matrix)
- [encoder_cli options](#2-encoder_cli-options)
  - [ROI packing](#21-roi-packing)
  - [Background blur](#22-background-blur)
  - [QP map (qp_map_delta)](#23-qp-map-qp_map_delta)
  - [Luma adaptation](#24-luma-adaptation)
- [rtp_server TET](#3-rtp_server-tet)
- [Recommended combinations](#4-recommended-combinations)
- [Quick checklist](#5-quick-checklist)
- [Related documentation](#related-documentation)

## 1) Compatibility matrix

| Combination | Supported? | Notes |
|---|---|---|
| ROI packing + background mode `off` | Yes | Standard packed-ROI path. |
| ROI packing + `background_blur` | No | Background mode is non-off, so packing must be disabled. |
| ROI packing + `qp_map_delta` | No | Background mode is non-off, so packing must be disabled. |
| `background_blur` + `qp_map_delta` | No | They are mutually exclusive modes of `background_mode`. |
| Luma adaptation + ROI packing | Yes | Luma is orthogonal; validator does not tie luma to `pack_rois`. Use with `background_mode=off` if you need packing. |
| Luma adaptation + `background_blur` / `qp_map_delta` | Yes | Per README, luma-adaptation EOI can combine with object-based EOI on IRAP when both are effective for that frame. |
| Any encoder mode + TET ON | Yes | TET is receiver-side post-processing in `rtp_server`. |
| TET enabled without valid tile file | Effectively No | TET processor is not initialized and TET is skipped. |

**Implementation details**

- In `encoder_cli`, any non-`off` background mode disables packing (`pack_rois` must not stay true; config validation enforces this).
- **`qp_map_delta` effective mode** depends on runtime:
  - Platform gate: `background_mode=qp_map_delta` is supported only on **x86 desktop systems**. On non-x86 systems, startup fails with a validation error.
  - On x86 systems, probing (`encoder_cli/src/nvenc/qp_map_support.cpp`) requires active encoder **nvh265enc** and encoder properties that look like QP-map controls. If probing fails, effective mode becomes **`background_blur`** with a logged reason.
  - The **direct NvEncodeAPI** path (`input_pipeline.cpp`) can select `qp_map_delta` without the GStreamer encoder probe; if that path is unavailable, fallback to **`background_blur`** also occurs.
- If you request `qp_map_delta` but see blur-like behavior, check logs for fallback warnings.

## 2) encoder_cli options

File-mode examples use a placeholder path. For `run.sh file`, the filename should include resolution and framerate when possible, e.g. `name_WIDTHxHEIGHT_FPS.yuv` (see `encoder_cli/run.sh`).

### 2.1 ROI packing

#### Command line

```bash
cd encoder_cli
./run.sh file rtp /path/to/video_1920x1080_30.yuv -- \
  --roi-pack-rois true \
  --roi-background-mode off
```

Related flag:

- `--roi-packer-type low_delay|random_access`

#### Configuration file

Edit `encoder_cli/encoder_config.ini`:

```ini
[ROI]
pack_rois = true
background_mode = off
packer_type = low_delay
```

#### Trade-offs (generic)

- **Pros**: Emphasizes ROI regions in the bitstream; often improves subjective ROI quality at a given bitrate budget.
- **Cons**: Can increase bitrate versus unpacked encoding; behavior varies by content and QP.

### 2.2 Background blur

#### Command line

```bash
cd encoder_cli
./run.sh file rtp /path/to/video_1920x1080_30.yuv -- \
  --roi-background-mode background_blur \
  --roi-sigma 2.5 \
  --roi-kernel-size 0
```

#### Configuration file

```ini
[ROI]
background_mode = background_blur
sigma = 2.5
kernel_size = 0
```

Non-off background mode disables packing automatically.

#### Trade-offs (generic)

- **Pros**: Tends to reduce bitrate by removing detail outside ROIs.
- **Cons**: Removes context outside ROIs, which can hurt detectors that use background; validate on your pipeline.

### 2.3 QP map (`qp_map_delta`)

#### Command line

```bash
cd encoder_cli
./run.sh file rtp /path/to/video_1920x1080_30.yuv -- \
  --roi-background-mode qp_map_delta \
  --roi-qp-map-background-delta 10
```

#### Configuration file

```ini
[ROI]
background_mode = qp_map_delta
qp_map_background_delta = 10
```

**Runtime requirements (summary)**

- Supported only on x86 desktop systems; non-x86 startup fails with a validation error when `background_mode=qp_map_delta`.
- Requires a supported encoder path (e.g. `nvh265enc` with QP-map-like properties, or direct NvEncodeAPI backend when selected).
- On x86 systems, unsupported runtime encoder paths fall back to **`background_blur`**; reason is logged.
- Non-off background mode disables packing, same as blur.

#### Trade-offs (generic)

- **Pros**: Can lower bitrate while keeping pixels outside ROIs unblurred compared to blur mode (when actually in qp_map_delta).
- **Cons**: Platform and encoder dependent; silent fallback to blur if support is missing—always confirm logs.

### 2.4 Luma adaptation

Encoder-side luma scaling on the Y plane before encode, plus optional EOI signaling for luma adaptation on IRAP frames when scaling succeeds for that frame.

#### Command line

```bash
cd encoder_cli
./run.sh file rtp /path/to/video_1920x1080_30.yuv -- \
  --enable-luma-adaptation true \
  --luma-downscale-factor 0.5
```

`--luma-downscale-factor` must be in **(0.0, 1.0]** (validated at startup).

#### Configuration file

```ini
[ROI]
enable_luma_adaptation = true
luma_downscale_factor = 0.5
```

#### Behavior

- Downscaling applies to the luma plane before encoding.
- **Per-frame**: luma-adaptation EOI is emitted only when sender-side downscaling succeeds for that frame; otherwise that frame is encoded without luma-adaptation EOI (see `encoder_cli/README.md` option interaction notes).
- Can coexist with object-based EOI flows (`background_blur` / effective `qp_map_delta`) when both are effective on the same IRAP frame.

#### Troubleshooting

- If you expect luma EOI but do not see it, check logs for downscale failure warnings (e.g. encoder backend or callback path rolling back pending luma state).

## 3) rtp_server TET

TET is applied after PRI processing. Effect depends on tile file, QP selection, and stream—verify end-to-end for your deployment.

### 3.1 TET ON

#### Command line

```bash
cd rtp_server
./run.sh rtp -- \
  --tet-enable=true \
  --tet-tile-file tiles/yuv_32x32_checkerboard.tet \
  --tet-qp 27 \
  --tet-dynamic-qp
```

#### Configuration file

```ini
[TET]
enabled = true
tile_file = tiles/yuv_32x32_checkerboard.tet
qp = 27
dynamic_qp = true
```

### 3.2 TET OFF

**Configuration file (recommended)**

```ini
[TET]
enabled = false
```

**CLI**

- `--tet-enable=true` forces TET ON.
- `--tet-enable=false` forces TET OFF.
- If `--tet-enable` is omitted, `[TET] enabled` from the active config is used.

Because TET is receiver-side, it can be used with streams produced with ROI packing, background blur, or QP map (including fallback blur), and with luma adaptation, as long as the receiver pipeline supports the combined stream.

## 4) Recommended combinations (generic)

- **Packed ROI encoding**: `pack_rois=true` and `background_mode=off`.
- **Bitrate reduction**: Try `qp_map_delta` when logs confirm it is active; otherwise use `background_blur` or adjust encoder QP—validate on your content.
- **TET**: Enable when `tile_file` is valid and QP selection matches your streams; use `dynamic_qp=true` when QP varies in the stream.

## 5) Quick checklist

- Non-off `background_mode` (`background_blur` or `qp_map_delta`) → do not expect ROI packing to remain active.
- `qp_map_delta` on non-x86 → startup fails with validation error (switch to `background_blur` or `off`).
- `qp_map_delta` on x86 but inactive at runtime → check logs for fallback to `background_blur` and the stated reason.
- TET inactive → `[TET] enabled=true` or `--tet-enable=true`, valid `tile_file`, optionally `dynamic_qp=true` for mixed-QP streams.
- Luma adaptation → `enable_luma_adaptation` / `--enable-luma-adaptation`; `luma_downscale_factor` in (0, 1]; if EOI is required per frame, confirm no downscale failures in logs.

## Related documentation

- **Full encoder CLI table and option interactions**: `encoder_cli/README.md`
- **TET and run script**: `rtp_server/README.md`, `rtp_server/run.sh` (help text)
- **Scripts**: `encoder_cli/SCRIPTS_README.md`, `encoder_cli/scripts/README.md`
