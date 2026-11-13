# libvcai source map

The library is built from **[`libvcai/libvcai_sources.cmake`](../../libvcai/libvcai_sources.cmake)**: two variables, **`LIBVCAI_SOURCES_COMMON`** and **`LIBVCAI_SOURCES_WITH_H265`** (adds `h265_sps_parser.cpp`).

## Grouped areas (from cmake source list)

| Area | Paths under `libvcai/src/` |
|------|-----------------------------------|
| **area_copy** | [**area_copy/**](../../libvcai/src/area_copy/) — debug_category, api, video_format_utils, yuv420/, nv12/, reconstruction/ |
| **Bitstream / SEI** | [**bitstream.cpp**](../../libvcai/src/bitstream.cpp), [**nal_utils.cpp**](../../libvcai/src/nal_utils.cpp), [**packed_regions_info.cpp**](../../libvcai/src/packed_regions_info.cpp), [**encoder_optimization_info.cpp**](../../libvcai/src/encoder_optimization_info.cpp), [**luma_adaptation_math.cpp**](../../libvcai/src/luma_adaptation_math.cpp) |
| **background_blur** | [**background_blur.cpp**](../../libvcai/src/background_blur.cpp), [**kernel.cpp**](../../libvcai/src/background_blur/kernel.cpp), [**roi_scaling.cpp**](../../libvcai/src/background_blur/roi_scaling.cpp), format_nv12/yuv420 |
| **communication** | [**communication/**](../../libvcai/src/communication/) — protocol, transport, transport_udp/webrtc, signaling_*, manager |
| **text_drawer** | [**text_drawer/**](../../libvcai/src/text_drawer/) + [**text_drawer.cpp**](../../libvcai/src/text_drawer.cpp) |
| **Overlays / FPS** | [`nvosd_overlay.cpp`](../../libvcai/src/nvosd_overlay.cpp), [`pipeline_fps_monitor.cpp`](../../libvcai/src/pipeline_fps_monitor.cpp) |
| **H265 optional** | [`h265_sps_parser.cpp`](../../libvcai/src/h265_sps_parser.cpp) (SPS/PPS parsing for base QP) |

Consumers set `LIBVCAI_SOURCE_PREFIX` then `include(libvcai_sources.cmake)` and add the variable to their target.

## CMake excerpt (full list)

--8<-- "libvcai/libvcai_sources.cmake:1:64"
