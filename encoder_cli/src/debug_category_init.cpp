// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "debug_category_init.h"
#include "../../libvcai/include/area_copy.h"
#include "../../libvcai/include/background_blur.h"
#include "../../libvcai/include/bitrate_calculation.h"
#include "../../libvcai/include/nal_utils.h"
#include "configuration_manager.h"
#include "encoder_cli_debug.h"
#include "encoder_config.h"
#include "global_data.h"
#include "gst/gpu_elements.h"
#include "gst/gst_callbacks.h"
#include "gst/gst_pipeline.h"
#include "gst/input_pipeline.h"
#include "gst/input_types/appsink_polling.h"
#include "gst/input_types/bypass_path.h"
#include "gst/input_types/camera_input.h"
#include "gst/input_types/common_input.h"
#include "gst/input_types/file_input.h"
#include "gst/input_types/frame_drop_manager.h"
#include "gst/input_types/frame_merger.h"
#include "gst/input_types/roi_input.h"
#include "gst/input_types/rtp_timestamp_overlay.h"
#include "gst/input_types/udp_input.h"
#include "gst/output_pipeline/output_branches/display/fps_overlay.h"
#include "gst/rtp_heartbeat.h"
#include "processing/branch_fps_tracker.h"
#include "processing/encoded_frame_processing.h"
#include "processing/encoder_submission_processing.h"
#include "processing/frame_processing_utils.h"
#include "processing/pipeline_message_processing.h"
#include "processing/roi_detection_processing.h"
#include "reporting/performance_monitor.h"
#include "reporting/simple_status_reporter.h"
#include "roi/groundtruth_roi_data.h"
#include "roi/low_delay_packer.h"
#include "roi/random_access_packer.h"
#include "roi/rectangle_splitter.h"
#include "roi/roi_manager.h"
#include "roi/roi_parser.h"
#include "roi/static_roi_data.h"
#include "utils/config_parser.h"
#include "utils/frame_saver.h"
#include "utils/gpu_monitor.h"
#include "utils/memory_monitor.h"

/**
 * @file debug_category_init.cpp
 * @brief Implementation of debug category initialization for the encoder_cli application.
 */

/**
 * @brief Initialize all debug categories for the application.
 *
 * This function initializes all GStreamer debug categories used throughout
 * the encoder_cli application. It should be called early in the application
 * startup, after gst_init() but before any logging operations.
 */
void initialize_debug_categories() {
    GST_DEBUG_CATEGORY_INIT(encoder_cli_main, "encoder-cli", 0, "Encoder CLI application");
    init_configuration_manager_debug_category();
    init_encoder_config_debug_category();
    init_packer_debug_category();
    init_nal_utils_debug_category();
    init_area_copy_debug_category();
    init_background_blur_debug_category();
    init_gpu_monitor_debug_category();
    init_memory_monitor_debug_category();
    init_roi_parser_debug_category();
    init_roi_manager_debug_category();
    init_rectangle_splitter_debug_category();
    init_low_delay_packer_debug_category();
    init_random_access_packer_debug_category();
    init_encoded_frame_processing_debug_category();
    init_encoder_submission_processing_debug_category();
    init_branch_fps_tracker_debug_category();
    init_frame_processing_utils_debug_category();
    init_pipeline_message_processing_debug_category();
    init_roi_detection_processing_debug_category();
    init_frame_saver_debug_category();
    init_gst_callbacks_debug_category();
    init_gst_pipeline_debug_category();
    init_gpu_elements_debug_category();
    init_input_pipeline_debug_category();
    init_common_input_debug_category();
    init_file_input_debug_category();
    init_camera_input_debug_category();
    init_udp_input_debug_category();
    init_udp_stats_debug_category();
    init_udp_frame_tracking_debug_category();
    init_frame_drop_manager_debug_category();
    init_roi_input_debug_category();
    init_rtp_timestamp_overlay_debug_category();
    init_fps_overlay_debug_category();
    init_simple_status_reporter_debug_category();
    init_performance_monitor_debug_category();
    init_config_parser_debug_category();
    init_static_roi_debug_category();
    init_groundtruth_roi_debug_category();
    init_bitrate_calculation_debug_category();
    init_rtp_heartbeat_debug_category();
    init_frame_merger_debug_category();
    init_bypass_path_debug_category();
    init_appsink_polling_debug_category();
}
