# VCAI Test Suite

This directory contains GoogleTest-based tests for `encoder_cli`, `rtp_server`, and shared `libvcai` components.

## Current Test Targets

The CMake setup builds these test executables:

- `test_encoder`
  - covers `encoder_cli` tests
  - also includes many shared `libvcai` unit tests
- `test_rtp_server`
  - covers `rtp_server` tests
  - also includes additional shared `libvcai` unit tests
- `test_vcai_decoder`
  - covers `vcai_decoder` tests (built under `test_build/test_bin/`)

There is no standalone `test_libvcai` executable; libvcai tests are distributed across `test_encoder` and `test_rtp_server`.

## Directory Layout

```text
test/
├── build.sh                     # Test build entrypoint (incremental by default)
├── run.sh                       # Test orchestration entrypoint (auto-build + run all components)
├── run_test_common.sh           # Shared test harness (env setup, binary run, result parsing)
├── run_encoder_cli.sh           # Runs test_encoder
├── run_rtp_server.sh            # Runs test_rtp_server
├── run_vcai_decoder.sh   # Builds + runs test_vcai_decoder
├── CMakeLists.txt               # Test target definitions
├── README.md
├── libvcai/              # Shared/core test sources
├── encoder_cli/                 # encoder_cli test sources
├── rtp_server/                  # rtp_server test sources
├── common/                      # Shared fixtures/helpers/test data
├── test_build/                  # Generated build directory (created by build.sh)
└── test_output/                 # Generated logs/XML outputs (created by run scripts)
```

## Recommended Usage

Run from project root:

```bash
./test/build.sh
./test/run.sh
```

Or from `test/`:

```bash
./build.sh
./run.sh
```

### Supported Commands

Build script:

```bash
./build.sh                 # Incremental configure/build (default Debug)
./build.sh clean           # Clean test_build, then configure/build
./build.sh release         # Incremental configure/build with Release type
```

Run script:

```bash
./run.sh run      # Ensure tests are built (incremental) and run all component tests (default)
./run.sh logs     # List recent logs across all component test binaries
./run.sh help     # Show usage
```

Per-component runners (each accepts `run` (default), `logs`, `help`):

```bash
./run_encoder_cli.sh run
./run_rtp_server.sh run
./run_vcai_decoder.sh run
```

## What `build.sh` Does

1. Validates required tools (`cmake`) and basic dependencies.
2. Optionally removes `test/test_build/` when `clean` is provided.
3. Configures CMake with:
   - `-DBUILD_TESTS=ON`
   - `-DBUILD_ENCODER_CLI=ON`
   - `-DBUILD_RTP_SERVER=ON`
   - Debug build type by default (or Release via `release`)
4. Builds:
   - `test_encoder`
   - `test_rtp_server`

## What `run.sh` Does

1. Invokes `./build.sh` to ensure binaries are up to date (incremental build).
2. Orchestrates the per-component runners in order: `run_encoder_cli.sh`, `run_rtp_server.sh`, `run_vcai_decoder.sh`.
3. Aggregates results and reports an overall pass/fail summary.
4. Each runner writes output under `test/test_output/<binary>/`:
   - `test_run_<timestamp>.log`
   - `test_results.xml` (when emitted by binary run)

## Manual Build/Run

If you need to run manually:

```bash
mkdir -p test/test_build
cd test/test_build
cmake -S ../.. -DBUILD_TESTS=ON -DBUILD_ENCODER_CLI=ON -DBUILD_RTP_SERVER=ON -DCMAKE_BUILD_TYPE=Debug
make test_encoder -j"$(nproc)"
make test_rtp_server -j"$(nproc)"
make test_vcai_decoder -j"$(nproc)"

cd test_bin
./test_encoder
./test_rtp_server

cd ../vcai_decoder
./test_vcai_decoder
```

## Current Test Source Inventory

### Packed Regions Information (PRI) SEI tests

- **`libvcai/test_packed_regions_info.cpp`** — Spec-oriented tests for the PRI SEI message (VSEI 8.32): syntax/semantics, value ranges, inferred values, and round-trip via the public read/write API.
- **`encoder_cli/test_packed_regions_info_utils.cpp`** — Tests for the encoder helper `create_packed_regions_info` (behavior and output shape), plus a round-trip test that the helper's output is valid PRI.

### `libvcai` test sources (16)

- `test_bitstream.cpp`
- `test_packed_regions_info.cpp`
- `test_nal_utils.cpp`
- `test_thread_safe_queue.cpp`
- `test_area_copy.cpp`
- `test_area_copy_safe.cpp`
- `test_bitrate_calculation.cpp`
- `test_yuv420_even_dimensions.cpp`
- `test_safe_correction.cpp`
- `test_pipeline_fps_monitor.cpp`
- `test_h265_sps_parser.cpp`
- `test_communication_protocol.cpp`
- `test_signaling_message_codec.cpp`
- `test_text_drawer_char_mapping.cpp`
- `test_text_drawer_render.cpp`
- `test_background_blur.cpp`

### `encoder_cli` test sources (47)

- `test_roi_parser.cpp`
- `test_rectangle_manager.cpp`
- `test_packed_regions_info_utils.cpp`
- `test_roi_manager.cpp`
- `test_roi_stabilization.cpp`
- `test_low_delay_packer_weighted.cpp`
- `test_ctu_alignment.cpp`
- `test_low_delay_packer_ctu.cpp`
- `test_low_delay_merging_thresholds.cpp`
- `test_random_access_packer.cpp`
- `test_roi_detection_random_access_mapping.cpp`
- `test_rectangle_splitter.cpp`
- `test_frame_processing_utils.cpp`
- `test_rtp_utils.cpp`
- `test_pipeline_utils.cpp`
- `test_input_pipelines.cpp`
- `test_processing_functions.cpp`
- `test_udp_config_validation.cpp`
- `test_output_directory_config.cpp`
- `test_roi_pack_rois_cli.cpp`
- `test_detection_mode_factory.cpp`
- `test_packing_mode_factory.cpp`
- `test_processing_mode_factory.cpp`
- `test_input_mode_factory.cpp`
- `test_output_branch_factory.cpp`
- `test_mode_combinations.cpp`
- `test_mode_selection_edge_cases.cpp`
- `test_detection_modes.cpp`
- `test_gpu_elements.cpp`
- `test_packing_modes.cpp`
- `test_processing_modes.cpp`
- `test_packing_processing_regression.cpp`
- `test_roi_detection_packing.cpp`
- `test_roi_detection_processing_helpers.cpp`
- `test_roi_detection_processing_api.cpp`
- `test_low_delay_inference_temporal_regression.cpp`
- `test_integration_file_input_basic.cpp`
- `test_integration_file_input_static_roi.cpp`
- `test_integration_file_input_roi_data.cpp`
- `test_integration_udp_input_basic.cpp`
- `test_integration_udp_input_reconnection.cpp`
- `test_integration_low_delay_packer.cpp`
- `test_integration_output_branches.cpp`
- `test_integration_error_handling.cpp`
- `test_integration_background_blur.cpp`
- `test_integration_passthrough_mode.cpp`
- `test_integration_low_delay_overlap_regression.cpp`

### `rtp_server` test sources (subset)

- `test_rtp_config.cpp`
- `test_rtp_server_config.cpp`
- `test_transport_factory.cpp`
- `test_frame_buffer_utils.cpp`
- `test_rtp_statistics.cpp`
- `test_command_line.cpp`
- `test_configuration_manager.cpp`
- `test_pipeline_utils.cpp`
- `test_gpu_elements.cpp`
- `test_test_mode.cpp`

## Shared Test Support

`test/common/` contains reusable test utilities and fixtures, including:

- integration helpers (`integration_test_helpers.*`)
- GStreamer environment validation (`test_gstreamer_environment.cpp`)
- fixture headers (`gst_test_fixture*.h`)
- config and ROI test data under `test/common/test_data/`

## Dependencies and Environment Notes

- GoogleTest is fetched automatically in CMake (`FetchContent`).
- `test_encoder` and `test_rtp_server` can configure and link without DeepStream. Sources and tests that include nvds headers are omitted; remaining tests compile against stubs where production files need those APIs.
- With DeepStream installed, the skipped nvds-backed sources and tests are compiled as usual.
- The runner configures plugin/runtime paths for VCAI decoder, DeepStream, and optional openhevc.

## Known Behavior

- Exit code `133` during GStreamer deinitialization is treated specially by the runner as a known shutdown issue.
- This handling is intended to avoid false negatives when tests have already produced valid GoogleTest output.

