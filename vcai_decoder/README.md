# vcai_decoder

Standalone GStreamer plugin subproject for VCAI decoder-side elements.

Current elements:

- `sfh265seiextract` (extracts H.265 SEI payload types `215` EOI and `226` PRI)
- `sfprireconstruct` (reconstructs PRI regions onto a fresh decoded `video/x-raw` output buffer)
- `sfeoilumabackscale` (applies EOI luma back-scaling)
- `sftetapply` (applies TET tiles; `I420` execution path)

Applications that need format or memory conversion after the decoder (for example NVMM to system memory) should use
standard GStreamer elements such as `nvvideoconvert` or `videoconvert` in their pipeline — not this plugin.

`sfh265seiextract` attaches plugin-local metadata (`sf_sei_meta`) to each buffer when PRI/EOI payloads are found. This
keeps metadata handling self-contained and compatible with GStreamer.

**Dependencies:** The plugin links and uses `libvcai` (built locally when configuring from the repo root).
Used modules: `luma_adaptation_math` (EOI ratio + backscale multiplier), `area_copy` (`ensure_even_dimensions`,
`has_even_dimensions` for PRI rects), `reconstruct_frame` (PRI to-output when dimensions match), `rectangle`,
`packed_regions_info` (`read_packed_regions_info` is the only PRI bitstream decode path used by the plugin),
`bitstream` (EOI SEI RBSP reads), `h265_sps_parser` (`get_nal_unit_type` in SEI extract; SPS base QP for dynamic TET),
`gst_wrappers` (`is_valid_video_dimensions` in TET, EOI, PRI ops), `gst_raii` (buffer/sample RAII in PRI paths).

### PRI parsing and ROI trace

- **`sfprireconstruct`** parses each matched PRI payload once with **`sf::read_packed_regions_info`**. Cancel / drawable-region
  checks, target-picture size validation, ROI trace (`roi-trace-path`), and `reconstruct_frame` all use that structure.
- **`sf_parse_pri_payload`** (C summary: present / parse_ok / cancel_flag / drawable `region_count`) uses the same lib
  reader and the same drawable-region rules as reconstruction helpers.
- **`sf_pri_reconstruct_in_place` / `sf_pri_reconstruct_to_output`** take **`const sf::packed_regions_info &`** (pixel copy
  from PRI units + `log2_unit_size`, matching the lib model).

### Output buffer allocation

`GstBaseTransform` still supplies the output buffer; the plugin does not allocate the full reconstructed frame. The
libvcai helper **`sf::create_empty_buffer`** is available if a future `prepare_output_buffer` / custom allocator
path is added (that would be a separate caps/negotiation change).

### APIs intentionally not used (decoder)

- **`nal_utils`** (`contains_hevc_irap_vcl_nal`, `inject_sei_message`): deferred unless the decoder needs IRAP gating or
  bitstream rewrite.
- **`nvosd_overlay`:** not linked in the decoder build.
- **`sf::*` logging** from `logging.h`: decoder elements use GStreamer logging only (see above).

**Logging:** This plugin uses GStreamer debug macros (`GST_DEBUG_OBJECT`, `GST_WARNING_OBJECT`, etc.) instead of
`libvcai`'s `sf::*` logging. This is an intentional exception to the project logging rule for the decoder
subproject.

Split decode-stage elements synchronize with encoded SEI using an in-plugin sync store keyed by frame identity
(`stream-id + PTS`, with `DTS`/`offset` fallback). Recommended chain:

```bash
... ! sfh265seiextract ! <decoder> ! sfprireconstruct ! sfeoilumabackscale ! sftetapply ! ...
```

Key properties:

- `sfprireconstruct`: `strict-parsing`, `strict-reconstruction`, `background-color`, `roi-trace-path`
- `sfeoilumabackscale`: `enabled`, `strict-parsing`
- `sftetapply`: `enabled`, `tile-file`, `qp`, `dynamic-qp`, `strict-apply`

`sftetapply` consumes sync-store entries as the terminal decoded-stage consumer to keep bounded memory usage.

## Source Layout

Decoder plugin sources are organized by responsibility:

- `src/core/`: plugin registration and cross-element coordination helpers
- `src/meta/`: custom metadata definitions and ownership logic
- `src/ops/`: reusable parsing/reconstruction/luma/TET operation helpers
- `src/elements/`: GStreamer element implementations

## Build

From `vcai_decoder/`:

```bash
./build.sh
```

The subproject is configured against GStreamer via `pkg-config` with minimum version `1.20`.

## Tests

Run focused plugin regression tests:

```bash
bash test/run.sh
```

The suite covers:

- sync-store keying/eviction behavior (`PTS`, `DTS`, `offset`)
- PRI/EOI payload parse-path validation (absent, cancel, malformed)
- SEI metadata attach robustness (writable path + initially non-writable input path via integration pipeline)
- PRI reconstruction + EOI luma application ops
- regression for multi-resampling PRI parsing that can misplace ROI target coordinates (the "double redraw" class of issue)
- in-process `appsrc/appsink` integration checks for extraction sync-store publication and decoded reconstruction behavior,
  including far-separated source/target reconstruction that verifies source-region clearing on new-buffer output

## Minimal Test Application

Build plugin + minimal test app:

```bash
./build.sh
```

App binary path:

```bash
../build/vcai_decoder/vcai_decoder/sf_decoder_minimal_test_app
```

Run with helper wrapper (sets `--plugin-dir` automatically):

```bash
bash test/run_minimal_test_app.sh --input /path/to/input.h265
```

With TET enabled:

```bash
bash test/run_minimal_test_app.sh --input /path/to/input.h265 --tet-file /path/to/tiles.tet --qp 27
```

Optional sink override (default is `fakesink`):

```bash
bash test/run_minimal_test_app.sh --input /path/to/input.h265 --sink autovideosink
```

Decoder override (default is software decode `avdec_h265`):

```bash
bash test/run_minimal_test_app.sh --input /path/to/input.h265 --decoder decodebin
```

## Inspect

```bash
export GST_PLUGIN_PATH="$(pwd)/../build/vcai_decoder/vcai_decoder"
gst-inspect-1.0 sfh265seiextract
gst-inspect-1.0 sfprireconstruct
gst-inspect-1.0 sfeoilumabackscale
gst-inspect-1.0 sftetapply
```
