// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file test_debug_categories.height
 * @brief Centralized initialization of all GStreamer debug categories for tests.
 *
 * This file provides a single function to initialize all debug categories
 * that might be used by the code under test, so tests don't need to
 * initialize categories individually.
 */

// Forward declarations for debug category initialization functions.
// Some categories are encoder_cli-specific and are not linked into all test targets.
#ifndef TEST_SKIP_ENCODER_DEBUG_CATEGORIES
void init_low_delay_packer_debug_category();
void init_random_access_packer_debug_category();
void init_rectangle_splitter_debug_category();
void init_roi_manager_debug_category();
void init_roi_parser_debug_category();
void init_static_roi_debug_category();
void init_simple_status_reporter_debug_category();
void init_output_pipeline_debug_category();
void init_roi_input_debug_category();
void init_udp_input_debug_category();
void init_camera_input_debug_category();
void init_common_input_debug_category();
void init_file_input_debug_category();
void init_packer_debug_category();
void init_pipeline_utils_debug_category();
void init_input_pipeline_debug_category();
void init_rtp_utils_debug_category();
void init_gst_pipeline_debug_category();
void init_gst_callbacks_debug_category();
void init_encoded_frame_processing_debug_category();
void init_frame_processing_utils_debug_category();
void init_roi_detection_processing_debug_category();
void init_pipeline_message_processing_debug_category();
void init_encoder_config_debug_category();
void init_config_parser_debug_category();
void init_frame_saver_debug_category();
void init_encoder_cli_main_debug_category();
#endif
void init_configuration_manager_debug_category();
void init_bitrate_calculation_debug_category();

/**
 * @brief Initialize all GStreamer debug categories used by encoder_cli code.
 *
 * This function should be called once in test setup to ensure all categories
 * are initialized before any logging calls are made.
 */
inline void init_all_test_debug_categories() {
    // Initialize all debug categories that might be used by the code under test.
#ifndef TEST_SKIP_ENCODER_DEBUG_CATEGORIES
    init_low_delay_packer_debug_category();
    init_random_access_packer_debug_category();
    init_rectangle_splitter_debug_category();
    init_roi_manager_debug_category();
    init_roi_parser_debug_category();
    init_static_roi_debug_category();
    init_simple_status_reporter_debug_category();
    init_output_pipeline_debug_category();
    init_roi_input_debug_category();
    init_udp_input_debug_category();
    init_camera_input_debug_category();
    init_common_input_debug_category();
    init_file_input_debug_category();
    init_packer_debug_category();
    init_pipeline_utils_debug_category();
    init_input_pipeline_debug_category();
    init_rtp_utils_debug_category();
    init_gst_pipeline_debug_category();
    init_gst_callbacks_debug_category();
    init_encoded_frame_processing_debug_category();
    init_frame_processing_utils_debug_category();
    init_roi_detection_processing_debug_category();
    init_pipeline_message_processing_debug_category();
    init_encoder_config_debug_category();
    init_config_parser_debug_category();
    init_frame_saver_debug_category();
    init_encoder_cli_main_debug_category();
#endif
    init_configuration_manager_debug_category();
    init_bitrate_calculation_debug_category();
}
