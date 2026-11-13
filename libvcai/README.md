# libvcai

`libvcai` is the shared C++20 core used by `encoder_cli` and `rtp_server`.
It provides H.265 SEI utilities, frame/ROI processing, communication primitives, and GStreamer/DeepStream helpers.

## Current Build Model

`libvcai` is source-driven and reused by multiple subprojects through:

- `libvcai/libvcai_sources.cmake`
  - `LIBVCAI_SOURCES_COMMON`
  - `LIBVCAI_SOURCES_WITH_H265` (adds `h265_sps_parser.cpp`)

There is no standalone `libvcai/CMakeLists.txt` target in this directory.
Instead, subprojects build local shared-library targets from these source sets:

- `encoder_cli_vcai` (`encoder_cli/CMakeLists.txt`)
- `rtp_server_vcai` (`rtp_server/CMakeLists.txt`)
- `test_vcai` and `test_rtp_server_vcai` (`test/CMakeLists.txt`)

## What The Library Provides

### SEI and Bitstream Core

- `packed_regions_info.h`: H.274 packed-regions SEI model + read/write helpers
- `encoder_optimization_info.h`: H.274 encoder-optimization SEI model + read/write helpers
- `nal_utils.h`: SEI NAL creation and insertion into H.265 access units
- `bitstream.h`: bit-level writer/reader and Exp-Golomb helpers
- `h265_sps_parser.h`: SPS/PPS parsing for base QP extraction

### Frame and ROI Processing

- `rectangle.h`: rectangle geometry helpers (intersection, IoU, containment, etc.)
- `area_copy.h`: ROI packing/reconstruction, safe copy result handling, format conversion helpers
- `background_blur.h`: ROI-preserving background blur for `NV12`/`I420`/`YUV420`
- `text_drawer.h`: bitmap text rendering on mapped/unmapped YUV frames
- `yuv_color.h`: shared overlay color enum
- `nvosd_overlay.h`: DeepStream display-meta helpers for rect/text overlays

### Communication Stack

- `communication/protocol.h`: binary protocol message types + serialization/deserialization
- `communication/transport.h`: transport abstraction and factory API
- `communication/transport_udp.h`: UDP transport implementation hooks
- `communication/transport_webrtc.h`: WebRTC transport implementation hooks/config
- `communication/signaling.h`: signaling client/server APIs for SDP/ICE exchange
- `communication/manager.h`: high-level handshake/heartbeat/disconnect orchestration

### Runtime Utilities

- `gst_raii.h`: RAII deleters and pointer aliases for GStreamer/GLib resources
- `gst_wrappers.h`: safer wrappers and utility structs around common GStreamer operations
- `logging.h`: `sf::*` wrappers for categorized GStreamer logging
- `bitrate_calculation.h`: average/rolling bitrate utilities
- `pipeline_fps_monitor.h`: per-pipeline rolling FPS monitor
- `thread_safe_queue.h`: template producer/consumer queue
- `error.h`: lightweight error-check helpers
- `string_utils.h`: string trim helper

## Directory Layout

```text
libvcai/
├── include/
│   ├── area_copy.h
│   ├── background_blur.h
│   ├── bitstream.h
│   ├── bitrate_calculation.h
│   ├── encoder_optimization_info.h
│   ├── gst_raii.h
│   ├── gst_wrappers.h
│   ├── h265_sps_parser.h
│   ├── logging.h
│   ├── nal_utils.h
│   ├── nvosd_overlay.h
│   ├── packed_regions_info.h
│   ├── pipeline_fps_monitor.h
│   ├── rectangle.h
│   ├── text_drawer.h
│   ├── thread_safe_queue.h
│   ├── yuv_color.h
│   ├── error.h
│   ├── string_utils.h
│   └── communication/
│       ├── manager.h
│       ├── protocol.h
│       ├── signaling.h
│       ├── transport.h
│       ├── transport_udp.h
│       └── transport_webrtc.h
├── src/
│   ├── area_copy/...
│   ├── background_blur/...
│   ├── communication/...
│   ├── text_drawer/...
│   ├── packed_regions_info.cpp
│   ├── encoder_optimization_info.cpp
│   ├── nal_utils.cpp
│   ├── bitstream.cpp
│   ├── h265_sps_parser.cpp
│   ├── nvosd_overlay.cpp
│   ├── gst_wrappers.cpp
│   ├── bitrate_calculation.cpp
│   ├── pipeline_fps_monitor.cpp
│   └── ...
├── libvcai_sources.cmake
└── run_static_analysis.sh
```

## API Usage Examples

### PRI SEI serialization + NAL injection

```cpp
#include "packed_regions_info.h"
#include "nal_utils.h"

sf::packed_regions_info pri{};
pri.cancel_flag = false;
pri.persistence_flag = true;
pri.log2_unit_size = 4;

const std::vector<uint8_t> sei_nal = sf::create_sei_nal_unit(pri);
const std::vector<uint8_t> with_sei = sf::inject_sei_message(frame_data, frame_size, sei_nal);
```

### Communication manager (UDP)

```cpp
#include "communication/manager.h"

auto manager = sf::communication::create_communication_manager(sf::communication::transport_type::udp);
sf::communication::transport_config cfg{"127.0.0.1", 5000, std::chrono::milliseconds{2000}};

if (sf::communication::manager_initialize(manager.get(), cfg) == sf::communication::transport_error::success) {
    auto hs = sf::communication::manager_perform_handshake(manager.get());
    if (hs.error == sf::communication::transport_error::success && hs.result.has_value()) {
        const uint16_t port = hs.result->assigned_port;
        const uint32_t session = hs.result->session_id;
        (void)port;
        (void)session;
    }
}
```

## Building

Build from repository root depending on the consumer target you are working on:

```bash
# encoder_cli + its local libvcai target
cmake -S . -B build/encoder_cli -DBUILD_ENCODER_CLI=ON
cmake --build build/encoder_cli

# rtp_server + its local libvcai target
cmake -S . -B build/rtp_server -DBUILD_RTP_SERVER=ON
cmake --build build/rtp_server

# tests (current test binaries)
cmake -S . -B test/test_build -DBUILD_TESTS=ON -DBUILD_ENCODER_CLI=ON -DBUILD_RTP_SERVER=ON
cmake --build test/test_build
```

## Testing

`libvcai` tests live in `test/libvcai/` and run through the central test runner:

```bash
cd test
./build.sh      # Optional explicit incremental build step
./run.sh
```

`./run.sh` invokes `./build.sh` automatically to ensure test binaries are current.

Current libvcai test files are integrated into `test_encoder` and/or `test_rtp_server` (there is no separate
`test_libvcai` executable).

## Static Analysis

Use the library-specific static-analysis helper:

```bash
cd libvcai
./run_static_analysis.sh [analysis_dir] [timestamp]
```

The script runs Clang Static Analyzer + Cppcheck and writes reports under `../static_analysis_results` by default.

## Dependencies (Effective)

The exact dependency floor is determined by the consuming target:

- C++20 compiler
- GLib/GIO
- GStreamer development packages
  - `encoder_cli` path uses `gstreamer-1.0 >= 1.20`
  - `rtp_server` path uses `gstreamer-1.0 >= 1.26`
- DeepStream headers/libs for DeepStream-backed paths (for example `nvosd_overlay` usage in runtime applications)

## Debug Category Initialization Helpers

These functions are available for modules that define dedicated debug categories and should be called after `gst_init()`:

- `init_nal_utils_debug_category()`
- `init_area_copy_debug_category()`
- `init_background_blur_debug_category()`
- `init_bitrate_calculation_debug_category()`

