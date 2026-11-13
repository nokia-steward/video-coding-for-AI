cmake_minimum_required(VERSION 3.13)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build libvcai locally
set(LIBVCAI_SOURCE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/libvcai")
include("${LIBVCAI_SOURCE_PREFIX}/libvcai_sources.cmake")

add_library(rtp_server_vcai SHARED ${LIBVCAI_SOURCES_WITH_H265})


target_include_directories(rtp_server_vcai
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/libvcai/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(rtp_server_vcai
    PUBLIC
    PkgConfig::gstreamer
    PkgConfig::gstreamer-sdp
    PkgConfig::gstreamer-app
    PkgConfig::gstreamer-video
    PkgConfig::gio
    PRIVATE
    PkgConfig::gtk3
)

# Find DeepStream installation dynamically
set(DEEPSTREAM_PATHS
    "/opt/nvidia/deepstream/deepstream-7.1"
    "/opt/nvidia/deepstream/deepstream-7.0"
    "/opt/nvidia/deepstream/deepstream-6.4"
    "/opt/nvidia/deepstream/deepstream-6.3"
    "/opt/nvidia/deepstream/deepstream-6.2"
    "/opt/nvidia/deepstream/deepstream-6.1"
    "/opt/nvidia/deepstream/deepstream-6.0"
)

set(DEEPSTREAM_PATH "")
foreach (path ${DEEPSTREAM_PATHS})
    if (EXISTS "${path}/lib")
        set(DEEPSTREAM_PATH "${path}")
        break()
    endif ()
endforeach ()

if (DEEPSTREAM_PATH)
    message(STATUS "Found DeepStream: ${DEEPSTREAM_PATH}")
    link_directories(${DEEPSTREAM_PATH}/lib)
    target_include_directories(rtp_server_vcai
        PRIVATE
        ${DEEPSTREAM_PATH}/sources/includes
    )
else ()
    message(WARNING "DeepStream not found in standard locations")
endif ()

# Add DeepStream-Yolo library path
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(DEEPSTREAM_YOLO_PATH "${PROJECT_ROOT}/yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo")
link_directories(${DEEPSTREAM_YOLO_PATH})

add_executable(rtp_server)

target_sources(rtp_server
    PRIVATE
    src/main.cpp
    src/core/server.cpp
    src/core/command_line.cpp
    src/core/rtp_server_config.cpp
    src/core/configuration_manager.cpp
    src/core/inference_engine_startup.cpp
    src/transport/transport_factory.cpp
    src/transport/rtp_input/rtp_input_transport.cpp
    src/transport/webrtc/webrtc_transport.cpp
    src/transport/webrtc/webrtc_signaling.cpp
    src/pipeline/pipeline.cpp
    src/pipeline/pipeline/api.cpp
    src/pipeline/pipeline/callbacks.cpp
    src/pipeline/pipeline/creation.cpp
    src/pipeline/pipeline/shutdown.cpp
    src/pipeline/pipeline/linking.cpp
    src/pipeline/pipeline/probes.cpp
    src/pipeline/pipeline/statistics.cpp
    src/pipeline/pipeline/utils.cpp
    src/pipeline/app/app_adapter.cpp
    src/pipeline/app/output_controller.cpp
    src/gst/gpu_elements.cpp
    src/pipeline/output/branch_factory.cpp
    src/pipeline/output/display/display_branch.cpp
    src/pipeline/output/display/deepstream_config.cpp
    src/pipeline/output/display/label_utils.cpp
    src/pipeline/output/display/overlay_probes.cpp
    src/pipeline/output/display/test_mode.cpp
    src/pipeline/output/display/test_mode_groundtruth.cpp
    src/pipeline/output/display/test_mode_evaluation.cpp
    src/pipeline/output/display/test_mode_summary.cpp
    src/pipeline/output/branch_utils.cpp
    src/pipeline/output/debug_frames/debug_frames_branch.cpp
    src/pipeline/output/file/file_branch.cpp
    src/pipeline/output/rtp_output/rtp_output_branch.cpp
    src/pipeline/frame_buffer_utils.cpp
    src/pipeline/decoded_frame_memory.cpp
    src/pipeline/decoded_frame_pipeline.cpp
    src/client/client_manager.cpp
    src/client/client_session.cpp
    src/client/session_monitor.cpp
    src/utils/rtp_statistics.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(gtk3 REQUIRED IMPORTED_TARGET gtkmm-3.0)
pkg_search_module(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.20)
pkg_search_module(gstreamer-sdp REQUIRED IMPORTED_TARGET gstreamer-sdp-1.0>=1.20)
pkg_search_module(gstreamer-app REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.20)
pkg_search_module(gstreamer-video REQUIRED IMPORTED_TARGET gstreamer-video-1.0>=1.20)
pkg_search_module(gio REQUIRED IMPORTED_TARGET gio-2.0)
pkg_search_module(gmodule REQUIRED IMPORTED_TARGET gmodule-2.0)

# Find jsoncpp for JSON parsing with fallbacks when pkg-config is unavailable
set(JSONCPP_TARGET "")
pkg_check_modules(jsoncpp IMPORTED_TARGET jsoncpp)
if (TARGET PkgConfig::jsoncpp)
    set(JSONCPP_TARGET PkgConfig::jsoncpp)
else ()
    find_package(JsonCpp CONFIG QUIET)
    if (TARGET JsonCpp::JsonCpp)
        set(JSONCPP_TARGET JsonCpp::JsonCpp)
    else ()
        include(FetchContent)
        FetchContent_Declare(JsonCpp
            GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
            GIT_TAG 1.9.5
        )
        FetchContent_MakeAvailable(JsonCpp)
        if (TARGET JsonCpp::JsonCpp)
            set(JSONCPP_TARGET JsonCpp::JsonCpp)
        elseif (TARGET jsoncpp_lib)
            add_library(JsonCpp::JsonCpp ALIAS jsoncpp_lib)
            set(JSONCPP_TARGET JsonCpp::JsonCpp)
        else ()
            message(FATAL_ERROR "jsoncpp could not be located or fetched automatically.")
        endif ()
    endif ()
endif ()

if (NOT JSONCPP_TARGET)
    message(FATAL_ERROR "jsoncpp dependency is missing.")
endif ()

target_include_directories(rtp_server
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libvcai/include
)

# Add DeepStream includes if found
if (DEEPSTREAM_PATH)
    target_include_directories(rtp_server
        PRIVATE
        ${DEEPSTREAM_PATH}/sources/includes
    )
endif ()

target_link_libraries(rtp_server
    PRIVATE
    rtp_server_vcai
    PkgConfig::gtk3
    PkgConfig::gstreamer
    PkgConfig::gstreamer-sdp
    PkgConfig::gstreamer-app
    PkgConfig::gstreamer-video
    PkgConfig::gmodule
    ${JSONCPP_TARGET}
    pthread
    nvdsgst_meta
    nvds_meta
    nvds_yml_parser
    nvdsgst_helper
    nvdsinfer_custom_impl_Yolo
)

target_compile_options(rtp_server PRIVATE ${PROJECT_WARNING_FLAGS})

# AddressSanitizer support (can be enabled with -DENABLE_ASAN=ON)
option(ENABLE_ASAN "Enable AddressSanitizer for rtp_server" OFF)
if (ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled for rtp_server")
    target_compile_options(rtp_server PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(rtp_server PRIVATE -fsanitize=address)
    # Set environment variable to get leak reports on exit
    set_target_properties(rtp_server PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:abort_on_error=0:print_stats=1:check_initialization_order=1:strict_init_order=0:fast_unwind_on_malloc=0"
    )
endif ()
