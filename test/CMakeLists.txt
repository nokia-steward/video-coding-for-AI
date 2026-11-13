set(LIBVCAI_SOURCE_PREFIX "${CMAKE_SOURCE_DIR}/libvcai")
include("${LIBVCAI_SOURCE_PREFIX}/libvcai_sources.cmake")
include(CheckIncludeFileCXX)

set(DEEPSTREAM_PATHS
    "/opt/nvidia/deepstream/deepstream-7.1"
    "/opt/nvidia/deepstream/deepstream-7.0"
    "/opt/nvidia/deepstream/deepstream-6.4"
    "/opt/nvidia/deepstream/deepstream-6.3"
    "/opt/nvidia/deepstream/deepstream-6.2"
    "/opt/nvidia/deepstream/deepstream-6.1"
    "/opt/nvidia/deepstream/deepstream-6.0"
    "/opt/nvidia/deepstream/deepstream"
)

set(DEEPSTREAM_PATH "")
foreach (path ${DEEPSTREAM_PATHS})
    if (EXISTS "${path}/lib")
        set(DEEPSTREAM_PATH "${path}")
        break()
    endif ()
endforeach ()

if (DEEPSTREAM_PATH)
    message(STATUS "Found DeepStream for tests: ${DEEPSTREAM_PATH}")
    link_directories(${DEEPSTREAM_PATH}/lib)
    add_compile_definitions(HAVE_DEEPSTREAM)
else ()
    message(WARNING "DeepStream not found - DeepStream-backed tests and sources are skipped")
endif ()

set(TEST_VCAI_SOURCES ${LIBVCAI_SOURCES_WITH_H265})
if (NOT DEEPSTREAM_PATH)
    list(REMOVE_ITEM TEST_VCAI_SOURCES "${LIBVCAI_SOURCE_PREFIX}/src/nvosd_overlay.cpp")
endif ()

# Build libvcai locally
add_library(test_vcai SHARED ${TEST_VCAI_SOURCES})


    target_include_directories(test_vcai
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libvcai/include>
        $<INSTALL_INTERFACE:include>
    )

target_link_libraries(test_vcai
    PUBLIC
    PkgConfig::gstreamer
    PkgConfig::gstreamer-sdp
    PkgConfig::gstreamer-app
    PkgConfig::gstreamer-video
    PkgConfig::gio
    PRIVATE
    PkgConfig::gtk3
)

if (DEEPSTREAM_PATH)
    target_include_directories(test_vcai PUBLIC
        ${DEEPSTREAM_PATH}/sources/includes
    )
    target_compile_definitions(test_vcai PRIVATE HAVE_DEEPSTREAM)
endif ()

# Add Google Test dependency
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://codeload.github.com/google/googletest/zip/refs/tags/v1.14.0
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

find_package(PkgConfig REQUIRED)
pkg_check_modules(gtk3 REQUIRED IMPORTED_TARGET gtkmm-3.0)
pkg_search_module(gstreamer REQUIRED IMPORTED_TARGET gstreamer-1.0>=1.20)
pkg_search_module(gstreamer-sdp REQUIRED IMPORTED_TARGET gstreamer-sdp-1.0>=1.20)
pkg_search_module(gstreamer-app REQUIRED IMPORTED_TARGET gstreamer-app-1.0>=1.20)
pkg_search_module(gstreamer-video REQUIRED IMPORTED_TARGET gstreamer-video-1.0>=1.20)
pkg_search_module(gio REQUIRED IMPORTED_TARGET gio-2.0)
pkg_search_module(gmodule REQUIRED IMPORTED_TARGET gmodule-2.0)

# Set test output directories
set(TEST_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_bin)
set(TEST_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_lib)
set(TEST_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test_lib)

# Create test directories
file(MAKE_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY})
file(MAKE_DIRECTORY ${TEST_LIBRARY_OUTPUT_DIRECTORY})

if(BUILD_ENCODER_CLI)
    check_include_file_cxx("ffnvcodec/nvEncodeAPI.h" TEST_HAVE_FFNV_CODEC_NVENCODEAPI_H)
    check_include_file_cxx("nvEncodeAPI.h" TEST_HAVE_NVENCODEAPI_H)
    if (TEST_HAVE_FFNV_CODEC_NVENCODEAPI_H OR TEST_HAVE_NVENCODEAPI_H)
        set(TEST_HAVE_NVENCODEAPI_HEADERS 1)
    else ()
        set(TEST_HAVE_NVENCODEAPI_HEADERS 0)
    endif ()

    # Find jsoncpp for JSON parsing (same as encoder_cli)
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
            endif ()
        endif ()
    endif ()

    add_executable(
        test_encoder
        libvcai/test_packed_regions_info.cpp
        encoder_cli/test_roi_parser.cpp
        libvcai/test_bitstream.cpp
        libvcai/test_nal_utils.cpp
        libvcai/test_communication_protocol.cpp
        libvcai/test_signaling_message_codec.cpp
        libvcai/test_signaling_server.cpp
        libvcai/test_text_drawer_char_mapping.cpp
        libvcai/test_text_drawer_render.cpp
        libvcai/test_background_blur.cpp
        libvcai/test_background_blur_roi_scaling.cpp
        libvcai/test_transport_webrtc.cpp
        libvcai/test_communication_manager.cpp
        libvcai/test_glyph_patterns.cpp
        libvcai/test_encoder_optimization_info.cpp
        libvcai/test_luma_adaptation_math.cpp
        libvcai/test_nvosd_overlay.cpp
        libvcai/test_area_copy_safe.cpp
        libvcai/test_pipeline_fps_monitor.cpp
        encoder_cli/test_rectangle_manager.cpp
        encoder_cli/test_packed_regions_info_utils.cpp
        libvcai/test_bitrate_calculation.cpp
        libvcai/test_yuv420_even_dimensions.cpp
        libvcai/test_safe_correction.cpp
        encoder_cli/test_roi_manager.cpp
        encoder_cli/test_roi_tracking.cpp
        encoder_cli/test_roi_stabilization.cpp
        encoder_cli/test_low_delay_packer_weighted.cpp
        encoder_cli/test_low_delay_internal_algorithms.cpp
        encoder_cli/test_low_delay_ordering.cpp
        encoder_cli/test_ctu_alignment.cpp
        encoder_cli/test_low_delay_packer_ctu.cpp
        encoder_cli/test_low_delay_merging_thresholds.cpp
        encoder_cli/test_random_access_packer.cpp
        encoder_cli/test_pri_packer_view.cpp
        encoder_cli/test_roi_tracker_view.cpp
        encoder_cli/test_random_access_period.cpp
        encoder_cli/test_roi_detection_random_access_mapping.cpp
        encoder_cli/test_rectangle_splitter.cpp
        encoder_cli/test_frame_processing_utils.cpp
        encoder_cli/test_frame_drop_manager.cpp
        encoder_cli/test_frame_merger.cpp
        encoder_cli/test_rtp_utils.cpp
        encoder_cli/test_pipeline_utils.cpp
        encoder_cli/test_input_pipelines.cpp
        encoder_cli/test_roi_input_pipeline.cpp
        encoder_cli/test_common_input.cpp
        encoder_cli/test_processing_functions.cpp
        encoder_cli/test_pipeline_message_processing.cpp
        encoder_cli/test_global_data_lifecycle.cpp
        common/test_gstreamer_environment.cpp
        common/integration_test_helpers.cpp
        encoder_cli/test_integration_file_input_basic.cpp
        encoder_cli/test_integration_file_input_static_roi.cpp
        encoder_cli/test_integration_file_input_roi_data.cpp
        encoder_cli/test_integration_udp_input_basic.cpp
        encoder_cli/test_integration_udp_input_reconnection.cpp
        encoder_cli/test_integration_low_delay_packer.cpp
        encoder_cli/test_integration_output_branches.cpp
        encoder_cli/test_integration_error_handling.cpp
        encoder_cli/test_udp_config_validation.cpp
        encoder_cli/test_output_directory_config.cpp
        encoder_cli/test_roi_pack_rois_cli.cpp
        encoder_cli/test_cli_params_parsing.cpp
        encoder_cli/test_application_runner.cpp
        encoder_cli/test_camera_mode_and_bypass_path.cpp
        encoder_cli/test_encoder_config_loader.cpp
        encoder_cli/test_encoder_config_defaults.cpp
        encoder_cli/test_encoder_config_validator.cpp
        encoder_cli/test_encoder_keyframe_period.cpp
        encoder_cli/test_configuration_manager_overrides.cpp
        encoder_cli/test_qp_map_support.cpp
        encoder_cli/test_encoder_backend_selection.cpp
        encoder_cli/test_encoder_backend_view.cpp
        encoder_cli/test_qp_map_delta_processing.cpp
        encoder_cli/test_performance_monitor.cpp
        encoder_cli/test_runtime_monitoring_and_enums.cpp
        encoder_cli/test_runtime_routing_and_callbacks.cpp
        encoder_cli/test_encoded_bridge_core.cpp
        encoder_cli/test_encoded_bridge_orchestrator.cpp
        encoder_cli/test_bridge_lifecycle_contract.cpp
        encoder_cli/test_encoder_submission_queue.cpp
        encoder_cli/test_ingress_bridge_core.cpp
        encoder_cli/test_ingress_bridge_orchestrator.cpp
        encoder_cli/test_random_access_bridge_core.cpp
        encoder_cli/test_random_access_bridge_orchestrator.cpp
        encoder_cli/test_encoded_injection_bridge_core.cpp
        encoder_cli/test_encoded_injection_bridge_orchestrator.cpp
        encoder_cli/test_memory_contract_helper.cpp
        encoder_cli/test_rtp_timestamp_overlay.cpp
        encoder_cli/test_detection_mode_factory.cpp
        encoder_cli/test_packing_mode_factory.cpp
        encoder_cli/test_processing_mode_factory.cpp
        encoder_cli/test_input_mode_factory.cpp
        encoder_cli/test_file_input_mode.cpp
        encoder_cli/test_udp_input_mode.cpp
        encoder_cli/test_udp_mode.cpp
        encoder_cli/test_pipeline_startup.cpp
        encoder_cli/test_pipeline_state_management.cpp
        encoder_cli/test_rtp_heartbeat.cpp
        encoder_cli/test_output_branch_factory.cpp
        encoder_cli/test_output_branch_facade.cpp
        encoder_cli/test_output_branch_controller.cpp
        encoder_cli/test_output_roi_sequence_correlation.cpp
        encoder_cli/test_output_branch_file_and_fps.cpp
        encoder_cli/test_output_branches_file_network.cpp
        encoder_cli/test_frame_path_meta_and_output_paths.cpp
        encoder_cli/test_file_and_camera_mode_runtime.cpp
        encoder_cli/test_worker_gst_tasks_lifecycle.cpp
        encoder_cli/test_roi_detection_utils_and_groundtruth_detection.cpp
        encoder_cli/test_output_branch_rtp_webrtc.cpp
        encoder_cli/test_output_branch_debug.cpp
        encoder_cli/test_output_pipeline_probes.cpp
        encoder_cli/test_pipeline_app_adapter.cpp
        encoder_cli/test_simple_status_reporter.cpp
        encoder_cli/test_get_luma_factors.cpp
        encoder_cli/test_input_analyzer_api.cpp
        encoder_cli/test_mode_combinations.cpp
        encoder_cli/test_mode_selection_edge_cases.cpp
        encoder_cli/test_detection_modes.cpp
        encoder_cli/test_gpu_elements.cpp
        encoder_cli/test_packing_modes.cpp
        encoder_cli/test_processing_modes.cpp
        encoder_cli/test_packing_processing_regression.cpp
        encoder_cli/test_roi_detection_packing.cpp
        encoder_cli/test_roi_detection_processing_helpers.cpp
        encoder_cli/test_low_delay_inference_temporal_regression.cpp
        encoder_cli/test_integration_background_blur.cpp
        encoder_cli/test_integration_passthrough_mode.cpp
        encoder_cli/test_roi_detection_processing_api.cpp
        encoder_cli/test_integration_low_delay_overlap_regression.cpp
        ../encoder_cli/src/roi/roi_parser.cpp
        ../encoder_cli/src/utils/rectangle_manager.cpp
        ../encoder_cli/src/utils/packed_regions_info_utils.cpp
        ../encoder_cli/src/roi/roi_manager.cpp
        ../encoder_cli/src/roi/roi_manager/api.cpp
        ../encoder_cli/src/roi/roi_manager/tracking.cpp
        ../encoder_cli/src/roi/roi_manager/prediction.cpp
        ../encoder_cli/src/roi/roi_manager/stabilization.cpp
        ../encoder_cli/src/roi/roi_tracker_view.cpp
        ../encoder_cli/src/roi/ctu_alignment.cpp
        ../encoder_cli/src/roi/low_delay_packer.cpp
        ../encoder_cli/src/roi/low_delay_packer/api.cpp
        ../encoder_cli/src/roi/low_delay_packer/utils.cpp
        ../encoder_cli/src/roi/low_delay_packer/core.cpp
        ../encoder_cli/src/roi/low_delay_packer/ctu_alignment.cpp
        ../encoder_cli/src/roi/low_delay_packer/conflicts.cpp
        ../encoder_cli/src/roi/low_delay_packer/packing.cpp
        ../encoder_cli/src/roi/low_delay_packer/history.cpp
        ../encoder_cli/src/roi/low_delay_packer/mapping.cpp
        ../encoder_cli/src/roi/low_delay_packer/merging.cpp
        ../encoder_cli/src/roi/low_delay_packer/ordering.cpp
        ../encoder_cli/src/roi/low_delay_packer/positioning.cpp
        ../encoder_cli/src/roi/random_access_packer.cpp
        ../encoder_cli/src/roi/pri_packer_view.cpp
        ../encoder_cli/src/roi/random_access_packer/api.cpp
        ../encoder_cli/src/roi/random_access_packer/positioning.cpp
        ../encoder_cli/src/roi/random_access_packer/merging.cpp
        ../encoder_cli/src/roi/random_access_packer/period.cpp
        ../encoder_cli/src/roi/random_access_packer/history.cpp
        ../encoder_cli/src/roi/random_access_packer/packing.cpp
        ../encoder_cli/src/roi/rectangle_splitter.cpp
        ../encoder_cli/src/roi/rectangle_splitter/api.cpp
        ../encoder_cli/src/roi/rectangle_splitter/extraction.cpp
        ../encoder_cli/src/roi/rectangle_splitter/utils.cpp
        ../encoder_cli/src/roi/rectangle_splitter/validation.cpp
        ../encoder_cli/src/roi/rectangle_splitter/splitting.cpp
        ../encoder_cli/src/processing/frame_processing_utils.cpp
        ../encoder_cli/src/processing/memory_contract_helper.cpp
        ../encoder_cli/src/processing/branch_fps_tracker.cpp
        ../encoder_cli/src/gst/rtp_utils.cpp
        ../encoder_cli/src/gst/pipeline_utils.cpp
        ../encoder_cli/src/gst/input_types/file_input.cpp
        ../encoder_cli/src/gst/input_types/camera_input.cpp
        ../encoder_cli/src/gst/input_types/udp_input.cpp
        ../encoder_cli/src/gst/input_types/udp_stats.cpp
        ../encoder_cli/src/gst/input_types/input_ingress_facade.cpp
        ../encoder_cli/src/gst/input_types/udp_frame_tracking.cpp
        ../encoder_cli/src/gst/input_types/frame_drop_manager.cpp
        ../encoder_cli/src/gst/input_types/common_input.cpp
        ../encoder_cli/src/gst/input_types/rtp_timestamp_overlay.cpp
        ../encoder_cli/src/gst/input_types/frame_merger.cpp
        ../encoder_cli/src/gst/input_types/bypass_path.cpp
        ../encoder_cli/src/gst/input_types/appsink_polling.cpp
        ../encoder_cli/src/gst/input_types/frame_router.cpp
        ../encoder_cli/src/gst/input_types/last_inference_roi.cpp
        ../encoder_cli/src/gst/input_types/frame_path_meta.cpp
        ../encoder_cli/src/gst/gst_callbacks.cpp
        ../encoder_cli/src/gst/gpu_elements.cpp
        ../encoder_cli/src/input_analyzer/input_analyzer.cpp
        ../encoder_cli/src/processing/encoded_frame_processing.cpp
        ../encoder_cli/src/processing/encoder_submission_processing.cpp
        ../encoder_cli/src/processing/pipeline_message_processing.cpp
        ../encoder_cli/src/configuration_manager.cpp
        ../encoder_cli/src/cli_params.cpp
        ../encoder_cli/src/encoder_config_loader.cpp
        ../encoder_cli/src/encoder_config_parser.cpp
        ../encoder_cli/src/encoder_config_validator.cpp
        ../encoder_cli/src/encoder_config_defaults.cpp
        ../encoder_cli/src/encoder_enums.cpp
        ../encoder_cli/src/utils/config_parser.cpp
        ../encoder_cli/src/utils/memory_monitor.cpp
        ../encoder_cli/src/utils/gpu_monitor.cpp
        ../encoder_cli/src/global_data.cpp
        ../encoder_cli/src/global_data_instance_runtime.cpp
        ../encoder_cli/src/bridge/core/encoded_bridge_core.cpp
        ../encoder_cli/src/bridge/core/encoded_injection_bridge_core.cpp
        ../encoder_cli/src/bridge/core/ingress_bridge_core.cpp
        ../encoder_cli/src/bridge/core/random_access_bridge_core.cpp
        ../encoder_cli/src/bridge/orchestration/encoded_bridge_orchestrator.cpp
        ../encoder_cli/src/bridge/orchestration/encoded_bridge_startup_contract.cpp
        ../encoder_cli/src/bridge/orchestration/encoded_injection_bridge_orchestrator.cpp
        ../encoder_cli/src/bridge/orchestration/ingress_bridge_orchestrator.cpp
        ../encoder_cli/src/bridge/orchestration/random_access_bridge_orchestrator.cpp
        ../encoder_cli/src/reporting/simple_status_reporter.cpp
        ../encoder_cli/src/reporting/performance_monitor.cpp
        ../encoder_cli/src/utils/frame_saver.cpp
        ../encoder_cli/src/gst/input_pipeline.cpp
        ../encoder_cli/src/gst/input_pipeline_encoding.cpp
        ../encoder_cli/src/application/input_modes/input_mode_factory.cpp
        ../encoder_cli/src/application/input_modes/file/file_input_mode.cpp
        ../encoder_cli/src/application/input_modes/camera/camera_input_mode.cpp
        ../encoder_cli/src/application/input_modes/udp/udp_input_mode.cpp
        ../encoder_cli/src/application/application_runner.cpp
        ../encoder_cli/src/application/lifecycle_facade.cpp
        ../encoder_cli/src/application/file_mode.cpp
        ../encoder_cli/src/application/webrtc_connection.cpp
        ../encoder_cli/src/application/thread_management.cpp
        ../encoder_cli/src/application/app_periodic_gst_tasks.cpp
        ../encoder_cli/src/application/udp_mode.cpp
        ../encoder_cli/src/pipeline/startup.cpp
        ../encoder_cli/src/pipeline/state_management.cpp
        ../encoder_cli/src/gst/gst_pipeline.cpp
        ../encoder_cli/src/gst/output_pipeline.cpp
        ../encoder_cli/src/gst/output_pipeline/output_branches/branch_factory.cpp
        ../encoder_cli/src/gst/output_pipeline/output_branches/output_branch_facade.cpp
        ../encoder_cli/src/gst/output_pipeline/output_branches/file_h265/file_h265_branch.cpp
        ../encoder_cli/src/gst/output_pipeline/output_branches/file_mp4/file_mp4_branch.cpp
        ../encoder_cli/src/gst/output_pipeline/output_branches/display/fps_overlay.cpp
        ../encoder_cli/src/gst/output_pipeline/output_branches/debug/debug_branch.cpp
        ../encoder_cli/src/gst/output_pipeline/output_branches/udp/udp_branch.cpp
        ../encoder_cli/src/gst/output_pipeline/output_branches/webrtc/webrtc_branch.cpp
        ../encoder_cli/src/gst/output_pipeline/api.cpp
        ../encoder_cli/src/gst/output_pipeline/app/output_branch_controller.cpp
        ../encoder_cli/src/gst/output_pipeline/probes.cpp
        ../encoder_cli/src/gst/output_pipeline/output_roi_sequence_correlation.cpp
        ../encoder_cli/src/gst/output_pipeline/queues.cpp
        ../encoder_cli/src/gst/output_pipeline/rtp_branch.cpp
        ../encoder_cli/src/gst/output_pipeline/paths.cpp
        ../encoder_cli/src/gst/rtp_heartbeat.cpp
        ../encoder_cli/src/nvenc/qp_map_support.cpp
        ../encoder_cli/src/encoder_backend/gst_encoder_backend.cpp
        ../encoder_cli/src/encoder_backend/encoder_backend_view.cpp
        ../encoder_cli/src/encoder_backend/nvenc_api_backend.cpp
        ../encoder_cli/src/gst/input_types/roi_input/roi_input_debug.cpp
        ../encoder_cli/src/processing/roi_detection_processing.cpp
        ../encoder_cli/src/processing/roi_detection_processing/api.cpp
        ../encoder_cli/src/processing/roi_detection_processing/helpers.cpp
        ../encoder_cli/src/processing/roi_detection_processing/detection_modes/detection_factory.cpp
        ../encoder_cli/src/processing/roi_detection_processing/detection_modes/inference/inference_detection.cpp
        ../encoder_cli/src/processing/roi_detection_processing/detection_modes/groundtruth/groundtruth_detection.cpp
        ../encoder_cli/src/processing/roi_detection_processing/detection_modes/static/static_detection.cpp
        ../encoder_cli/src/processing/roi_detection_processing/detection_modes/passthrough/passthrough_detection.cpp
        ../encoder_cli/src/processing/roi_detection_processing/packing_modes/packing_factory.cpp
        ../encoder_cli/src/processing/roi_detection_processing/packing_modes/low_delay/low_delay_packing.cpp
        ../encoder_cli/src/processing/roi_detection_processing/packing_modes/random_access/random_access_packing.cpp
        ../encoder_cli/src/processing/roi_detection_processing/processing_modes/processing_factory.cpp
        ../encoder_cli/src/processing/roi_detection_processing/processing_modes/packing/packing_processing.cpp
        ../encoder_cli/src/processing/roi_detection_processing/processing_modes/background_blur/background_blur_processing.cpp
        ../encoder_cli/src/processing/roi_detection_processing/processing_modes/qp_map_delta/qp_map_delta_processing.cpp
        ../encoder_cli/src/processing/roi_detection_packing.cpp
        ../encoder_cli/src/processing/roi_detection_utils.cpp
        ../encoder_cli/src/processing/roi_detection_random_access.cpp
    )

    # Conditionally add DeepStream-dependent sources (nvds headers).
    if (DEEPSTREAM_PATH)
        target_sources(test_encoder PRIVATE
            encoder_cli/test_roi_detection_extraction.cpp
            encoder_cli/test_output_branch_display.cpp
            ../encoder_cli/src/gst/output_pipeline/output_branches/display/display_branch.cpp
            ../encoder_cli/src/gst/output_pipeline/output_branches/display/overlay_renderer.cpp
            ../encoder_cli/src/gst/input_types/roi_input/roi_input_deepstream_factory.cpp
            ../encoder_cli/src/gst/input_types/roi_input/roi_input_deepstream_link.cpp
            ../encoder_cli/src/gst/input_types/roi_input/roi_input_pts_probes.cpp
            ../encoder_cli/src/gst/input_types/roi_input/roi_input_branch_factories.cpp
            ../encoder_cli/src/gst/input_types/roi_input/roi_input_pipelines.cpp
            ../encoder_cli/src/processing/roi_detection_extraction.cpp
        )
        target_compile_definitions(test_encoder PRIVATE HAVE_DEEPSTREAM)
    else ()
        target_sources(test_encoder PRIVATE
            common/display_branch_stub.cpp
            common/encoder_deepstream_stub.cpp
        )
    endif ()

    # Conditionally add JSON-dependent sources
    if (JSONCPP_TARGET)
        target_sources(test_encoder PRIVATE
            encoder_cli/test_static_roi_data.cpp
            encoder_cli/test_groundtruth_roi_data.cpp
            ../encoder_cli/src/roi/static_roi_data.cpp
            ../encoder_cli/src/roi/groundtruth_roi_data.cpp
        )
    endif ()

    # Set output directory for encoder tests
    set_target_properties(test_encoder PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY ${TEST_LIBRARY_OUTPUT_DIRECTORY}
        ARCHIVE_OUTPUT_DIRECTORY ${TEST_ARCHIVE_OUTPUT_DIRECTORY}
    )

    target_include_directories(
        test_encoder
        PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/encoder_cli/src
        ${CMAKE_SOURCE_DIR}/libvcai/include
        ${CMAKE_CURRENT_SOURCE_DIR}/common
    )

    # Add DeepStream includes if found
    if (DEEPSTREAM_PATH)
        target_include_directories(test_encoder
            PRIVATE
            ${DEEPSTREAM_PATH}/sources/includes
        )
    endif ()

    target_link_libraries(
        test_encoder
        PRIVATE GTest::gtest_main
        test_vcai
        PkgConfig::gio
        PkgConfig::gstreamer
        PkgConfig::gstreamer-app
        PkgConfig::gstreamer-video
    )

    # Link JSON library if found
    if (JSONCPP_TARGET)
        target_link_libraries(test_encoder PRIVATE ${JSONCPP_TARGET})
    endif ()

    # Link DeepStream libraries if found
    if (DEEPSTREAM_PATH)
        target_link_libraries(test_encoder PRIVATE
            nvdsgst_meta
            nvds_meta
            nvds_yml_parser
            nvdsgst_helper
        )
    endif ()

    target_link_libraries(test_encoder PRIVATE ${CMAKE_DL_LIBS})
    target_compile_definitions(test_encoder PRIVATE HAVE_NVENCODEAPI_HEADERS=${TEST_HAVE_NVENCODEAPI_HEADERS})

    include(GoogleTest)
    # Disable test discovery when using ThreadSanitizer to avoid issues
    # gtest_discover_tests(test_encoder)
endif()

if(BUILD_RTP_SERVER)
    # Find jsoncpp for JSON parsing (same as encoder_cli)
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

    # Use GStreamer targets for rtp_server tests.

    # Build libvcai locally for rtp_server tests
    set(TEST_RTP_SERVER_VCAI_SOURCES ${LIBVCAI_SOURCES_WITH_H265})
    if (NOT DEEPSTREAM_PATH)
        list(REMOVE_ITEM TEST_RTP_SERVER_VCAI_SOURCES "${LIBVCAI_SOURCE_PREFIX}/src/nvosd_overlay.cpp")
    endif ()
    add_library(test_rtp_server_vcai SHARED ${TEST_RTP_SERVER_VCAI_SOURCES})

    target_include_directories(test_rtp_server_vcai
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/libvcai/include>
        $<INSTALL_INTERFACE:include>
    )
    
    if (DEEPSTREAM_PATH)
        target_include_directories(test_rtp_server_vcai PUBLIC
            ${DEEPSTREAM_PATH}/sources/includes
        )
        target_compile_definitions(test_rtp_server_vcai PRIVATE HAVE_DEEPSTREAM)
    endif ()

    target_link_libraries(test_rtp_server_vcai
        PUBLIC
        PkgConfig::gstreamer
        PkgConfig::gstreamer-sdp
        PkgConfig::gstreamer-app
        PkgConfig::gstreamer-video
        PkgConfig::gio
        PRIVATE
        PkgConfig::gtk3
    )

    add_executable(
        test_rtp_server
        libvcai/test_thread_safe_queue.cpp
        libvcai/test_area_copy.cpp
        libvcai/test_h265_sps_parser.cpp
        libvcai/test_communication_protocol.cpp
        libvcai/test_signaling_message_codec.cpp
        libvcai/test_signaling_server.cpp
        libvcai/test_text_drawer_char_mapping.cpp
        libvcai/test_text_drawer_render.cpp
        libvcai/test_gst_wrappers.cpp
        ../libvcai/src/gst_wrappers.cpp
        libvcai/test_background_blur.cpp
        libvcai/test_transport_webrtc.cpp
        libvcai/test_communication_manager.cpp
        libvcai/test_glyph_patterns.cpp
    )

    if (NOT DEEPSTREAM_PATH)
        target_sources(test_rtp_server PRIVATE
            common/rtp_server_debug_category_stub.cpp
        )
    endif ()

    if (DEEPSTREAM_PATH)
        target_sources(test_rtp_server PRIVATE
            rtp_server/test_rtp_config.cpp
            rtp_server/test_rtp_server_config.cpp
            rtp_server/test_transport_factory.cpp
            rtp_server/test_frame_buffer_utils.cpp
            rtp_server/test_rtp_statistics.cpp
            rtp_server/test_command_line.cpp
            rtp_server/test_configuration_manager.cpp
            rtp_server/test_pipeline_utils.cpp
            rtp_server/test_pipeline_creation.cpp
            rtp_server/test_decoded_frame_pipeline.cpp
            rtp_server/test_pipeline_api.cpp
            rtp_server/test_client_manager_session_monitor.cpp
            rtp_server/test_inference_engine_startup.cpp
            rtp_server/test_gpu_elements.cpp
            rtp_server/test_display_branch.cpp
            rtp_server/test_output_branch_utilities.cpp
            rtp_server/test_overlay_probes.cpp
            rtp_server/test_test_mode.cpp
            rtp_server/test_webrtc_signaling.cpp
            rtp_server/test_transport_and_linking.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/core/rtp_server_config.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/core/configuration_manager.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/core/command_line.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/core/server.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/core/inference_engine_startup.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/transport/transport_factory.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/transport/rtp_input/rtp_input_transport.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/transport/webrtc/webrtc_transport.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/transport/webrtc/webrtc_signaling.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline/api.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline/callbacks.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline/creation.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline/shutdown.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline/linking.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline/probes.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline/statistics.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/pipeline/utils.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/app/app_adapter.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/app/output_controller.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/branch_utils.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/branch_factory.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/display/display_branch.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/display/deepstream_config.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/display/label_utils.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/display/overlay_probes.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/display/test_mode.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/display/test_mode_groundtruth.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/display/test_mode_evaluation.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/display/test_mode_summary.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/debug_frames/debug_frames_branch.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/file/file_branch.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/output/rtp_output/rtp_output_branch.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/frame_buffer_utils.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/decoded_frame_memory.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/pipeline/decoded_frame_pipeline.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/gst/gpu_elements.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/client/client_manager.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/client/client_session.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/client/session_monitor.cpp
            ${CMAKE_SOURCE_DIR}/rtp_server/src/utils/rtp_statistics.cpp
        )
    endif ()

    # Set output directory for rtp_server tests
    set_target_properties(test_rtp_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${TEST_RUNTIME_OUTPUT_DIRECTORY}
        LIBRARY_OUTPUT_DIRECTORY ${TEST_LIBRARY_OUTPUT_DIRECTORY}
        ARCHIVE_OUTPUT_DIRECTORY ${TEST_ARCHIVE_OUTPUT_DIRECTORY}
    )

    target_include_directories(
        test_rtp_server
        PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/rtp_server/src
        ${CMAKE_SOURCE_DIR}/libvcai/include
        ${CMAKE_CURRENT_SOURCE_DIR}/common
    )

    target_compile_definitions(
        test_rtp_server
        PRIVATE
        TEST_SKIP_ENCODER_DEBUG_CATEGORIES
    )

    if (DEEPSTREAM_PATH)
        target_include_directories(test_rtp_server
            PRIVATE
            ${DEEPSTREAM_PATH}/sources/includes
        )
        target_compile_definitions(test_rtp_server PRIVATE HAVE_DEEPSTREAM)
    endif ()

    target_link_libraries(
        test_rtp_server
        PRIVATE GTest::gtest_main
        test_rtp_server_vcai
        PkgConfig::gio
        PkgConfig::gstreamer
        PkgConfig::gstreamer-sdp
        PkgConfig::gstreamer-app
        PkgConfig::gstreamer-video
        PkgConfig::gmodule
        pthread
    )

    if (DEEPSTREAM_PATH)
        target_link_libraries(test_rtp_server PRIVATE
            nvdsgst_meta
            nvds_meta
            nvds_yml_parser
            nvdsgst_helper
        )
    endif ()

    if (JSONCPP_TARGET)
        target_link_libraries(test_rtp_server PRIVATE ${JSONCPP_TARGET})
    endif ()

    include(GoogleTest)
    gtest_discover_tests(test_rtp_server)
endif()

# Ensure all libvcai unit tests are wired into at least one test binary.
file(GLOB LIBVCAI_TEST_SOURCES
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "libvcai/test_*.cpp"
)

set(WIRED_LIBVCAI_TEST_SOURCES)
foreach(test_target IN ITEMS test_encoder test_rtp_server)
    if (TARGET ${test_target})
        get_target_property(target_sources ${test_target} SOURCES)
        if (target_sources)
            foreach(source_file IN LISTS target_sources)
                if (IS_ABSOLUTE "${source_file}")
                    file(RELATIVE_PATH source_file_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${source_file}")
                else ()
                    set(source_file_rel "${source_file}")
                endif ()
                if (source_file_rel MATCHES "^libvcai/test_.*\\.cpp$")
                    list(APPEND WIRED_LIBVCAI_TEST_SOURCES "${source_file_rel}")
                endif ()
            endforeach ()
        endif ()
    endif ()
endforeach ()
list(REMOVE_DUPLICATES WIRED_LIBVCAI_TEST_SOURCES)

set(MISSING_LIBVCAI_TEST_SOURCES)
foreach(lib_test_source IN LISTS LIBVCAI_TEST_SOURCES)
    if (NOT lib_test_source IN_LIST WIRED_LIBVCAI_TEST_SOURCES)
        list(APPEND MISSING_LIBVCAI_TEST_SOURCES "${lib_test_source}")
    endif ()
endforeach ()

if (MISSING_LIBVCAI_TEST_SOURCES)
    message(FATAL_ERROR
        "Missing libvcai test source wiring. Add these files to test_encoder or test_rtp_server: "
        "${MISSING_LIBVCAI_TEST_SOURCES}"
    )
endif ()

# Print test output directories
message(STATUS "Test executables will be built to: ${TEST_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "Test libraries will be built to: ${TEST_LIBRARY_OUTPUT_DIRECTORY}")
