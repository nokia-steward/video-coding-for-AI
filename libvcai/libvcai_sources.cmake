# Shared source lists for libvcai consumers.
#
# Expected input:
#   LIBVCAI_SOURCE_PREFIX - absolute or relative path to the libvcai directory.
# Output variables:
#   LIBVCAI_SOURCES_COMMON
#   LIBVCAI_SOURCES_WITH_H265

if (NOT DEFINED LIBVCAI_SOURCE_PREFIX)
    message(FATAL_ERROR "LIBVCAI_SOURCE_PREFIX must be set before including libvcai_sources.cmake")
endif ()

set(LIBVCAI_SOURCES_COMMON
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/debug_category.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/api.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/video_format_utils.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/utils/even_dimensions.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/yuv420/plane_operations.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/yuv420/processing.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/yuv420/reconstruction.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/nv12/plane_operations.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/nv12/validation.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/nv12/frame_setup.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/nv12/y_plane_copy.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/nv12/uv_plane_copy.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/nv12/processing.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/nv12/reconstruction.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/reconstruction/buffer_init.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/reconstruction/pri_extraction.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/reconstruction/api.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/area_copy/buffer_creation.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/bitstream.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/nal_utils.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/encoder_optimization_info.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/luma_adaptation_math.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/packed_regions_info.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/background_blur.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/background_blur/kernel.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/background_blur/roi_scaling.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/background_blur/format_nv12.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/background_blur/format_yuv420.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/communication/protocol.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/communication/transport.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/communication/transport_udp.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/communication/transport_webrtc.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/communication/signaling_message_codec.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/communication/signaling_client.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/communication/signaling_server.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/communication/manager.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/bitrate_calculation.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/text_drawer/glyph_patterns.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/text_drawer/char_mapping.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/text_drawer/render_i420.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/text_drawer/render_nv12.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/text_drawer.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/nvosd_overlay.cpp
    ${LIBVCAI_SOURCE_PREFIX}/src/pipeline_fps_monitor.cpp
)

set(LIBVCAI_SOURCES_WITH_H265
    ${LIBVCAI_SOURCES_COMMON}
    ${LIBVCAI_SOURCE_PREFIX}/src/h265_sps_parser.cpp
)
