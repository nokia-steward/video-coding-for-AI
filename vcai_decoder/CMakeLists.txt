cmake_minimum_required(VERSION 3.13)

project(vcai_decoder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_search_module(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.20)
pkg_search_module(gstreamer_base REQUIRED IMPORTED_TARGET gstreamer-base-1.0>=1.20)
pkg_search_module(gstreamer_app REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.20)
pkg_search_module(gstreamer_video REQUIRED IMPORTED_TARGET gstreamer-video-1.0>=1.20)
pkg_search_module(gio REQUIRED IMPORTED_TARGET gio-2.0)

# Build libvcai locally (repo root when built via add_subdirectory(vcai_decoder))
set(LIBVCAI_SOURCE_PREFIX "${CMAKE_SOURCE_DIR}/libvcai")
include("${LIBVCAI_SOURCE_PREFIX}/libvcai_sources.cmake")
# Decoder does not use DeepStream; exclude nvosd_overlay to avoid gstnvdsmeta dependency
list(REMOVE_ITEM LIBVCAI_SOURCES_WITH_H265
    "${LIBVCAI_SOURCE_PREFIX}/src/nvosd_overlay.cpp")

add_library(decoder_vcai SHARED ${LIBVCAI_SOURCES_WITH_H265})

target_include_directories(decoder_vcai
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libvcai/include>
)

target_link_libraries(decoder_vcai
    PUBLIC
    PkgConfig::gstreamer
    PkgConfig::gstreamer_video
    PkgConfig::gio
)

add_library(gstvcai_decoder SHARED
    src/core/plugin.cpp
    src/core/sf_frame_identity.cpp
    src/core/sf_sei_sync_store.cpp
    src/meta/sf_sei_meta.cpp
    src/ops/sf_sei_payload_parse.cpp
    src/ops/sf_pri_reconstruct_ops.cpp
    src/ops/sf_eoi_luma_ops.cpp
    src/ops/sf_tet_tiles.cpp
    src/elements/sf_h265_sei_extract.cpp
    src/elements/sf_pri_reconstruct.cpp
    src/elements/sf_eoi_luma_backscale.cpp
    src/elements/sf_tet_apply.cpp
)

target_include_directories(gstvcai_decoder
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/meta
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ops
    ${CMAKE_CURRENT_SOURCE_DIR}/src/elements
)

target_link_libraries(gstvcai_decoder
    PRIVATE
    PkgConfig::gstreamer
    PkgConfig::gstreamer_base
    PkgConfig::gstreamer_video
    decoder_vcai
)

set_target_properties(gstvcai_decoder PROPERTIES
    OUTPUT_NAME gstvcai_decoder
)

option(BUILD_VCAI_DECODER_TESTS "Build vcai_decoder tests" ON)
option(BUILD_VCAI_DECODER_MINIMAL_APP "Build minimal vcai_decoder test app" ON)

if (BUILD_VCAI_DECODER_MINIMAL_APP)
    add_executable(sf_decoder_minimal_test_app
        test/minimal_test_app.cpp
    )

    target_link_libraries(sf_decoder_minimal_test_app
        PRIVATE
        PkgConfig::gstreamer
        decoder_vcai
    )

    set_target_properties(sf_decoder_minimal_test_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vcai_decoder"
    )
endif ()

if (BUILD_VCAI_DECODER_TESTS)
    enable_testing()

    add_executable(test_vcai_decoder
        test/test_vcai_decoder.cpp
        test/test_vcai_decoder_integration.cpp
        src/core/sf_frame_identity.cpp
        src/meta/sf_sei_meta.cpp
        src/ops/sf_sei_payload_parse.cpp
        src/core/sf_sei_sync_store.cpp
        src/ops/sf_pri_reconstruct_ops.cpp
        src/ops/sf_eoi_luma_ops.cpp
        src/ops/sf_tet_tiles.cpp
        src/elements/sf_h265_sei_extract.cpp
        src/elements/sf_pri_reconstruct.cpp
    )

    target_include_directories(test_vcai_decoder
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/meta
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ops
        ${CMAKE_CURRENT_SOURCE_DIR}/src/elements
    )

    target_link_libraries(test_vcai_decoder
        PRIVATE
        PkgConfig::gstreamer
        PkgConfig::gstreamer_base
        PkgConfig::gstreamer_app
        PkgConfig::gstreamer_video
        decoder_vcai
        m
    )

    # Align with test/CMakeLists.txt (test_bin) when built from ./test/build.sh.
    set_target_properties(test_vcai_decoder PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test_bin"
    )

    add_test(NAME vcai_decoder_tests COMMAND test_vcai_decoder)
endif ()
