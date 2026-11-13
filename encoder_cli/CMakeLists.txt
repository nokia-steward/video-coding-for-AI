# Build libvcai locally (use project root - same as test/)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(LIBVCAI_SOURCE "${PROJECT_ROOT}/libvcai")
include(CheckIncludeFileCXX)

# Ensure libvcai is reachable for source files that use relative includes
# (e.g. ../../libvcai from roi/, ../../../../libvcai from roi/subdir/)
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libvcai")
    file(CREATE_LINK "${LIBVCAI_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/libvcai" SYMBOLIC)
endif()

set(LIBVCAI_SOURCE_PREFIX "${LIBVCAI_SOURCE}")
include("${LIBVCAI_SOURCE}/libvcai_sources.cmake")

add_library(encoder_cli_vcai SHARED ${LIBVCAI_SOURCES_COMMON})

target_include_directories(encoder_cli_vcai
    PUBLIC
    $<BUILD_INTERFACE:${LIBVCAI_SOURCE}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(encoder_cli_vcai
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
    target_include_directories(encoder_cli_vcai
        PRIVATE
        ${DEEPSTREAM_PATH}/sources/includes
    )
else ()
    message(WARNING "DeepStream not found in standard locations")
endif ()

# Add DeepStream-Yolo library path (PROJECT_ROOT set above)
set(DEEPSTREAM_YOLO_PATH "${PROJECT_ROOT}/yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo")
link_directories(${DEEPSTREAM_YOLO_PATH})

# Set consistent output directory for the executable
set(ENCODER_CLI_BIN_DIR "${CMAKE_BINARY_DIR}/bin")
file(MAKE_DIRECTORY ${ENCODER_CLI_BIN_DIR})

add_executable(encoder_cli
    src/main.cpp
    src/cli_params.cpp
    src/configuration_manager.cpp
    src/encoder_config_loader.cpp
    src/encoder_config_parser.cpp
    src/encoder_config_validator.cpp
    src/encoder_config_defaults.cpp
    src/encoder_enums.cpp
    src/debug_category_init.cpp
    src/pipeline/state_management.cpp
    src/pipeline/startup.cpp
    src/application/application_runner.cpp
    src/application/lifecycle_facade.cpp
    src/application/udp_mode.cpp
    src/application/file_mode.cpp
    src/application/webrtc_connection.cpp
    src/application/thread_management.cpp
    src/application/app_periodic_gst_tasks.cpp
    src/application/input_modes/input_mode_factory.cpp
    src/application/input_modes/file/file_input_mode.cpp
    src/application/input_modes/camera/camera_input_mode.cpp
    src/application/input_modes/udp/udp_input_mode.cpp
    src/gst/gst_callbacks.cpp
    src/gst/gst_pipeline.cpp
    src/gst/gpu_elements.cpp
    src/gst/input_pipeline.cpp
    src/gst/input_pipeline_encoding.cpp
    src/gst/input_types/common_input.cpp
    src/gst/input_types/input_ingress_facade.cpp
    src/gst/input_types/file_input.cpp
    src/gst/input_types/camera_input.cpp
    src/gst/input_types/udp_input.cpp
    src/gst/input_types/udp_stats.cpp
    src/gst/input_types/udp_frame_tracking.cpp
    src/gst/input_types/frame_drop_manager.cpp
    src/gst/input_types/roi_input/roi_input_debug.cpp
    src/gst/input_types/roi_input/roi_input_deepstream_factory.cpp
    src/gst/input_types/roi_input/roi_input_deepstream_link.cpp
    src/gst/input_types/roi_input/roi_input_pts_probes.cpp
    src/gst/input_types/roi_input/roi_input_branch_factories.cpp
    src/gst/input_types/roi_input/roi_input_pipelines.cpp
    src/gst/input_types/frame_merger.cpp
    src/gst/input_types/bypass_path.cpp
    src/gst/input_types/appsink_polling.cpp
    src/gst/input_types/frame_router.cpp
    src/gst/input_types/last_inference_roi.cpp
    src/gst/input_types/frame_path_meta.cpp
    src/gst/input_types/rtp_timestamp_overlay.cpp
    src/gst/output_pipeline.cpp
    src/gst/output_pipeline/api.cpp
    src/gst/output_pipeline/app/output_branch_controller.cpp
    src/gst/output_pipeline/rtp_branch.cpp
    src/gst/output_pipeline/paths.cpp
    src/gst/output_pipeline/queues.cpp
    src/gst/output_pipeline/probes.cpp
    src/gst/output_pipeline/output_roi_sequence_correlation.cpp
    src/gst/output_pipeline/output_branches/branch_factory.cpp
    src/gst/output_pipeline/output_branches/output_branch_facade.cpp
    src/gst/output_pipeline/output_branches/display/display_branch.cpp
    src/gst/output_pipeline/output_branches/display/fps_overlay.cpp
    src/gst/output_pipeline/output_branches/display/overlay_renderer.cpp
    src/gst/output_pipeline/output_branches/file_h265/file_h265_branch.cpp
    src/gst/output_pipeline/output_branches/file_mp4/file_mp4_branch.cpp
    src/gst/output_pipeline/output_branches/debug/debug_branch.cpp
    src/gst/output_pipeline/output_branches/udp/udp_branch.cpp
    src/gst/output_pipeline/output_branches/webrtc/webrtc_branch.cpp
    src/gst/rtp_utils.cpp
    src/gst/rtp_heartbeat.cpp
    src/gst/pipeline_utils.cpp
    src/input_analyzer/input_analyzer.cpp
    src/processing/encoded_frame_processing.cpp
    src/processing/encoder_submission_processing.cpp
    src/processing/branch_fps_tracker.cpp
    src/processing/pipeline_message_processing.cpp
    src/utils/packed_regions_info_utils.cpp
    src/utils/rectangle_manager.cpp
    src/global_data.cpp
    src/global_data_instance_runtime.cpp
    src/bridge/core/encoded_bridge_core.cpp
    src/bridge/core/encoded_injection_bridge_core.cpp
    src/bridge/core/ingress_bridge_core.cpp
    src/bridge/core/random_access_bridge_core.cpp
    src/bridge/orchestration/encoded_bridge_orchestrator.cpp
    src/bridge/orchestration/encoded_bridge_startup_contract.cpp
    src/bridge/orchestration/encoded_injection_bridge_orchestrator.cpp
    src/bridge/orchestration/ingress_bridge_orchestrator.cpp
    src/bridge/orchestration/random_access_bridge_orchestrator.cpp
    src/roi/roi_parser.cpp
    src/roi/static_roi_data.cpp
    src/roi/groundtruth_roi_data.cpp
    src/processing/frame_processing_utils.cpp
    src/processing/memory_contract_helper.cpp
    src/processing/roi_detection_processing.cpp
    src/processing/roi_detection_processing/api.cpp
    src/processing/roi_detection_processing/helpers.cpp
    src/processing/roi_detection_processing/detection_modes/detection_factory.cpp
    src/processing/roi_detection_processing/detection_modes/inference/inference_detection.cpp
    src/processing/roi_detection_processing/detection_modes/groundtruth/groundtruth_detection.cpp
    src/processing/roi_detection_processing/detection_modes/static/static_detection.cpp
    src/processing/roi_detection_processing/detection_modes/passthrough/passthrough_detection.cpp
    src/processing/roi_detection_processing/packing_modes/packing_factory.cpp
    src/processing/roi_detection_processing/packing_modes/low_delay/low_delay_packing.cpp
    src/processing/roi_detection_processing/packing_modes/random_access/random_access_packing.cpp
    src/processing/roi_detection_processing/processing_modes/processing_factory.cpp
    src/processing/roi_detection_processing/processing_modes/packing/packing_processing.cpp
    src/processing/roi_detection_processing/processing_modes/background_blur/background_blur_processing.cpp
    src/processing/roi_detection_processing/processing_modes/qp_map_delta/qp_map_delta_processing.cpp
    src/processing/roi_detection_utils.cpp
    src/processing/roi_detection_extraction.cpp
    src/processing/roi_detection_packing.cpp
    src/processing/roi_detection_random_access.cpp
    src/reporting/simple_status_reporter.cpp
    src/reporting/performance_monitor.cpp
    src/roi/roi_manager.cpp
    src/roi/roi_manager/api.cpp
    src/roi/roi_manager/tracking.cpp
    src/roi/roi_manager/prediction.cpp
    src/roi/roi_manager/stabilization.cpp
    src/roi/rectangle_splitter.cpp
    src/roi/rectangle_splitter/api.cpp
    src/roi/rectangle_splitter/utils.cpp
    src/roi/rectangle_splitter/validation.cpp
    src/roi/rectangle_splitter/splitting.cpp
    src/roi/rectangle_splitter/extraction.cpp
    src/roi/ctu_alignment.cpp
    src/roi/low_delay_packer.cpp
    src/roi/low_delay_packer/api.cpp
    src/roi/low_delay_packer/core.cpp
    src/roi/low_delay_packer/ctu_alignment.cpp
    src/roi/low_delay_packer/packing.cpp
    src/roi/low_delay_packer/utils.cpp
    src/roi/low_delay_packer/ordering.cpp
    src/roi/low_delay_packer/history.cpp
    src/roi/low_delay_packer/merging.cpp
    src/roi/low_delay_packer/positioning.cpp
    src/roi/low_delay_packer/conflicts.cpp
    src/roi/low_delay_packer/mapping.cpp
    src/roi/pri_packer_view.cpp
    src/roi/roi_tracker_view.cpp
    src/roi/random_access_packer.cpp
    src/roi/random_access_packer/api.cpp
    src/roi/random_access_packer/history.cpp
    src/roi/random_access_packer/merging.cpp
    src/roi/random_access_packer/positioning.cpp
    src/roi/random_access_packer/packing.cpp
    src/roi/random_access_packer/period.cpp
    src/nvenc/qp_map_support.cpp
    src/encoder_backend/gst_encoder_backend.cpp
    src/encoder_backend/encoder_backend_view.cpp
    src/encoder_backend/nvenc_api_backend.cpp
    src/utils/config_parser.cpp
    src/utils/frame_saver.cpp
    src/utils/gpu_monitor.cpp
    src/utils/memory_monitor.cpp
)

# Detect architecture for decoder selection
if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(USE_ARM64_DECODER ON)
    message(STATUS "Detected ARM64 architecture - will use nvv4l2decoder")
    add_compile_definitions(USE_ARM64_DECODER=1)
else ()
    set(USE_ARM64_DECODER OFF)
    message(STATUS "Detected non-ARM64 architecture - will use nvh265dec")
    add_compile_definitions(USE_ARM64_DECODER=0)
endif ()

# Detect architecture for encoder selection
if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(USE_ARM64_ENCODER ON)
    add_compile_definitions(USE_ARM64_ENCODER=1)
else ()
    set(USE_ARM64_ENCODER OFF)
    add_compile_definitions(USE_ARM64_ENCODER=0)
endif ()

# Set the output directory to ensure consistent binary location
set_target_properties(encoder_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${ENCODER_CLI_BIN_DIR}
)

# Find CUDA libraries using the standard symlink
# /usr/local/cuda is typically a symlink to the current CUDA version
find_library(CUDART_LIBRARY cudart
    HINTS
    /usr/local/cuda/lib64
    /opt/cuda/lib64
    /usr/lib/x86_64-linux-gnu
)
if (NOT CUDART_LIBRARY)
    message(FATAL_ERROR "cudart library not found")
endif ()

# Find CUDA BLAS library (used by DeepStream-Yolo)
find_library(CUBLAS_LIBRARY cublas
    HINTS
    /usr/local/cuda/lib64
    /opt/cuda/lib64
    /usr/lib/x86_64-linux-gnu
)
if (NOT CUBLAS_LIBRARY)
    message(FATAL_ERROR "cublas library not found")
endif ()

# Find NVIDIA Management Library (NVML) for GPU monitoring (optional)
find_library(NVML_LIBRARY nvidia-ml
    HINTS
    /usr/lib/x86_64-linux-gnu
    /usr/lib64
    /usr/lib
)
if (NVML_LIBRARY)
    message(STATUS "NVML library found: ${NVML_LIBRARY}")
else ()
    message(STATUS "NVML library not found - GPU monitoring will be disabled")
endif ()

find_package(PkgConfig REQUIRED)
pkg_check_modules(gtk3 REQUIRED IMPORTED_TARGET gtkmm-3.0)
pkg_search_module(gio REQUIRED IMPORTED_TARGET gio-2.0)

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

pkg_check_modules(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.20)
pkg_check_modules(gstreamer-sdp REQUIRED IMPORTED_TARGET gstreamer-sdp-1.0>=1.20)
pkg_check_modules(gstreamer-app REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.20)
pkg_check_modules(gstreamer-video REQUIRED IMPORTED_TARGET gstreamer-video-1.0>=1.20)
# WebRTC support is optional - webrtcbin is part of gstreamer-plugins-bad
# We'll link against gstreamer which should include webrtcbin if available

target_include_directories(encoder_cli
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/gst
    ${CMAKE_CURRENT_SOURCE_DIR}/gst/input_types
    ${CMAKE_CURRENT_SOURCE_DIR}/processing
    ${CMAKE_CURRENT_SOURCE_DIR}/roi
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

# Add DeepStream includes if found
if (DEEPSTREAM_PATH)
    target_include_directories(encoder_cli
        PRIVATE
        ${DEEPSTREAM_PATH}/sources/includes
    )
endif ()

target_link_libraries(encoder_cli
    PRIVATE
    encoder_cli_vcai
    PkgConfig::gtk3
    PkgConfig::gstreamer
    PkgConfig::gstreamer-sdp
    PkgConfig::gstreamer-app
    PkgConfig::gstreamer-video
    ${JSONCPP_TARGET}
    nvdsgst_meta
    nvds_meta
    nvds_yml_parser
    nvdsgst_helper
    nvdsinfer_custom_impl_Yolo
    ${CUDART_LIBRARY}
    ${CUBLAS_LIBRARY}
    ${CMAKE_DL_LIBS}
)

# Link NVML if available (optional dependency for GPU monitoring)
if (NVML_LIBRARY)
    target_link_libraries(encoder_cli PRIVATE ${NVML_LIBRARY})
    target_compile_definitions(encoder_cli PRIVATE HAVE_NVML=1)
else ()
    target_compile_definitions(encoder_cli PRIVATE HAVE_NVML=0)
endif ()

# Optional ONNX Runtime for adaptive luma input analyzer (when not found, stub is used)
option(ENABLE_ONNXRUNTIME "Use ONNX Runtime for adaptive luma input analyzer" ON)
if (ENABLE_ONNXRUNTIME)
    find_package(onnxruntime QUIET)
    if (onnxruntime_FOUND)
        target_link_libraries(encoder_cli PRIVATE onnxruntime::onnxruntime)
        target_compile_definitions(encoder_cli PRIVATE HAVE_ONNXRUNTIME=1)
        message(STATUS "ONNX Runtime found - adaptive luma analyzer will use ONNX inference")
    else ()
        message(STATUS "ONNX Runtime not found - adaptive luma analyzer will use stub "
                       "(set CMAKE_PREFIX_PATH to ONNX Runtime install to enable)")
    endif ()
else ()
    message(STATUS "ONNX Runtime disabled (ENABLE_ONNXRUNTIME=OFF) - adaptive luma analyzer will use stub")
endif ()

target_compile_options(encoder_cli PRIVATE ${PROJECT_WARNING_FLAGS})

# Detect system nvEncodeAPI headers (ffnvcodec package or direct install)
check_include_file_cxx("ffnvcodec/nvEncodeAPI.h" HAVE_FFNV_CODEC_NVENCODEAPI_H)
check_include_file_cxx("nvEncodeAPI.h" HAVE_NVENCODEAPI_H)
if (HAVE_FFNV_CODEC_NVENCODEAPI_H OR HAVE_NVENCODEAPI_H)
    target_compile_definitions(encoder_cli PRIVATE HAVE_NVENCODEAPI_HEADERS=1)
    message(STATUS "Found system nvEncodeAPI headers - direct NvEncodeAPI backend enabled")
else ()
    target_compile_definitions(encoder_cli PRIVATE HAVE_NVENCODEAPI_HEADERS=0)
    message(STATUS "System nvEncodeAPI headers not found - direct NvEncodeAPI backend will auto-fallback")
endif ()

# AddressSanitizer support (can be enabled with -DENABLE_ASAN=ON)
option(ENABLE_ASAN "Enable AddressSanitizer for encoder_cli" OFF)
if (ENABLE_ASAN)
    message(STATUS "AddressSanitizer enabled for encoder_cli")
    target_compile_options(encoder_cli PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(encoder_cli PRIVATE -fsanitize=address)
    # Set environment variable with CUDA-compatible options
    # Note: detect_leaks=0 is required for CUDA compatibility as CUDA manages its own memory
    # protect_shadow_gap=0 helps avoid conflicts with CUDA memory regions
    set_target_properties(encoder_cli PROPERTIES
        ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0:halt_on_error=0:abort_on_error=0:print_stats=1:check_initialization_order=0:strict_init_order=0:fast_unwind_on_malloc=0:protect_shadow_gap=0:alloc_dealloc_mismatch=0"
    )
endif ()
