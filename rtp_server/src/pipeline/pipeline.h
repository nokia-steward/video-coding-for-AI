// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <gst/gst.h>
#include <memory>
#include <optional>
#include <string>

#include <vector>
#include "../core/rtp_config.h"
#include "../core/rtp_server_config.h"
#include "app/app_adapter.h"
#include "app/output_controller.h"
#include "area_copy.h"
#include "context/pipeline_contexts.h"
#include "gst_raii.h"
#include "output/display/test_mode.h"

// Forward declarations
struct rtp_pipeline;
struct client_session;
/**
 * @brief Pipeline management struct for RTP server.
 *
 * This struct handles the creation, configuration, and management of one top-level GStreamer pipeline
 * for RTP video streaming with H.265 decoding, display, and file saving capabilities.
 */
struct rtp_pipeline {
    /**
     * @brief Constructor.
     * @param port The port number for the pipeline.
     * @param loop The main loop for the application.
     * @param rtp_output_config RTP output configuration.
     * @param output_directory Optional output directory for saving video files.
     * @param background_color Background color for empty buffers (default: green, which is 0 in YUV).
     * @param transport_ctx Transport context with function table and user data.
     * @param draw_bitrate Enable bitrate display overlay (default: false).
     * @param server_config Optional pointer to full server configuration (for DeepStream inference).
     */
    rtp_pipeline(guint16 port, GMainLoop *loop, const rtp_output_config &rtp_output_config_param = rtp_output_config{},
                 const std::optional<std::string> &output_directory = std::nullopt,
                 sf::background_color background_color = sf::background_color::green,
                 transport_context transport_ctx_param = {.functions = {}}, bool draw_bitrate = false,
                 bool draw_bitrate_large = false, bool draw_fps = false,
                 const rtp_server_config *server_config = nullptr);

    /**
     * @brief Destructor.
     */
    ~rtp_pipeline();

    // Disable copy constructor and assignment operator
    rtp_pipeline(const rtp_pipeline &) = delete;
    rtp_pipeline &operator=(const rtp_pipeline &) = delete;

    /**
     * @brief Create and configure the GStreamer pipeline.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool create_pipeline();

    /**
     * @brief Start the pipeline.
     * @return true if successful, false otherwise.
     */
    [[nodiscard]] bool start_pipeline();

    /**
     * @brief Stop the pipeline.
     */
    void stop_pipeline();

    /**
     * @brief Stop the pipeline after draining buffered frames.
     */
    void stop_pipeline_graceful();

    /**
     * @brief Get the main pipeline element (for external management).
     * @return The GStreamer pipeline element.
     */
    [[nodiscard]] GstElement *get_pipeline() const { return main_pipeline.get(); }

    /**
     * @brief Get the port number.
     * @return The port number.
     */
    [[nodiscard]] guint16 get_port() const { return port; }

    /**
     * @brief Check if bitrate overlay is enabled.
     * @return true when bitrate overlay is enabled.
     */
    [[nodiscard]] bool is_bitrate_overlay_enabled() const { return draw_bitrate; }

    /**
     * @brief Check if large red bitrate overlay is enabled.
     * @return true when large bitrate overlay is enabled.
     */
    [[nodiscard]] bool is_bitrate_overlay_large_enabled() const { return draw_bitrate_large; }

    /**
     * @brief Check if FPS overlay is enabled.
     * @return true when FPS overlay is enabled.
     */
    [[nodiscard]] bool is_fps_overlay_enabled() const { return draw_fps; }

    /**
     * @brief Log periodic statistics summary.
     */
    void log_periodic_statistics() const;

    /**
     * @brief Log comprehensive pipeline statistics.
     */
    void log_pipeline_statistics() const;

    /**
     * @brief Set the client session for RTP statistics monitoring.
     * @param session Pointer to the client session (non-owning).
     */
    void set_client_session(client_session *session);

    client_session *client_session_ptr; // Non-owning pointer for RTP statistics

    // Transport-specific input pipeline elements (public for transport functions)
    sf::unique_gst_element_no_delete_ptr rtp_input_source; // Used for RTP input transport
    sf::unique_gst_element_no_delete_ptr webrtcbin; // Used for WebRTC transport
    sf::unique_gst_element_no_delete_ptr rtp_input_capsfilter; // Used for RTP input transport
    sf::unique_gst_element_no_delete_ptr webrtc_rtp_capsfilter; // Used for WebRTC transport - enforces H265 RTP caps

    // Shared input pipeline elements (public for transport functions)
    sf::unique_gst_element_no_delete_ptr rtph265depay;
    sf::unique_gst_element_no_delete_ptr h265parse;
    sf::unique_gst_element_no_delete_ptr h265_capsfilter; // Capsfilter after h265parse to force byte-stream format
    sf::unique_gst_element_no_delete_ptr queue_encoded; // Queue after byte-stream caps (replaces appsink bridge tail)

    // Output branch elements (public for branch functions to access)
    sf::unique_gst_element_no_delete_ptr decoded_output_capsfilter;
    sf::unique_gst_element_no_delete_ptr gpu_video_convert;
    sf::unique_gst_element_no_delete_ptr tee;
    sf::unique_gst_element_no_delete_ptr queue_display;
    sf::unique_gst_element_no_delete_ptr display_convert;
    sf::unique_gst_element_no_delete_ptr capsfilter_display;
    sf::unique_gst_element_no_delete_ptr nvosd_display;
    sf::unique_gst_element_no_delete_ptr post_osd_tee;
    sf::unique_gst_element_no_delete_ptr output_sink;
    sf::unique_gst_element_no_delete_ptr queue_file;
    sf::unique_gst_element_no_delete_ptr h265_encoder;
    sf::unique_gst_element_no_delete_ptr filesink;
    sf::unique_gst_element_no_delete_ptr queue_rtp_output;
    sf::unique_gst_element_no_delete_ptr h265_encoder_rtp_output;
    sf::unique_gst_element_no_delete_ptr rtph265pay_rtp_output;
    sf::unique_gst_element_no_delete_ptr rtp_output_sink;
    sf::unique_gst_element_no_delete_ptr queue_debug_frames;
    sf::unique_gst_element_no_delete_ptr debug_frames_download;
    sf::unique_gst_element_no_delete_ptr debug_frames_convert;
    sf::unique_gst_element_no_delete_ptr debug_frames_png_encoder;
    sf::unique_gst_element_no_delete_ptr debug_frames_sink;

    // DeepStream elements (for display branch inference)
    sf::unique_gst_element_no_delete_ptr nvstreammux; // Stream multiplexer
    sf::unique_gst_element_no_delete_ptr nvinfer; // Inference engine
    sf::unique_gst_element_no_delete_ptr nvtracker; // Tracker

    // Class IDs for label filtering (empty means show all detections)
    std::vector<size_t> class_ids_to_filter;

    // Test mode context (optional, enabled when groundtruth file is provided)
    std::unique_ptr<test_mode_context> test_mode_ctx;

    // Configuration (made public for branch functions to access)
    guint16 port;
    GMainLoop *main_loop;
    std::string filename;
    rtp_output_config rtp_output;
    std::optional<std::string> output_directory;
    const rtp_server_config *server_config; // Non-owning pointer to server config

    // =========================================================================
    // Single top-level pipeline
    // =========================================================================
    sf::unique_gst_element_ptr main_pipeline;

    // =========================================================================
    // Decode / VCAI processing elements
    // =========================================================================
    sf::unique_gst_element_no_delete_ptr sf_h265_sei_extract;
    sf::unique_gst_element_no_delete_ptr h265_decoder;
    sf::unique_gst_element_no_delete_ptr decoded_converter;
    sf::unique_gst_element_no_delete_ptr decoded_capsfilter;
    sf::unique_gst_element_no_delete_ptr sf_pri_reconstruct;
    sf::unique_gst_element_no_delete_ptr sf_eoi_luma_backscale;
    sf::unique_gst_element_no_delete_ptr sf_tet_apply;
    sf::unique_gst_element_no_delete_ptr queue_decoded; // After TET, before output capsfilter
    sf::background_color background_color;
    transport_context transport_ctx; // Transport abstraction
    transport_type transport_type_enum; // rtp or webrtc (kept for compatibility)
    bool draw_bitrate; // Enable bitrate display overlay
    bool draw_bitrate_large; // Enable large red bitrate display overlay (2x size, red color)
    bool draw_fps; // Enable FPS display overlay
    pipeline_app_adapter app_adapter;
    output_branch_controller output_controller;

    // =========================================================================
    // Runtime Contexts
    // =========================================================================
    pipeline_runtime_context runtime_context;
    pipeline_stats_context stats_context;

    transport_build_context transport_build_ctx;

    /**
     * @brief Setup bus message handling for the main pipeline.
     */
    void setup_bus_watchers() const;

    // Private helper methods are now in separate files:
    // - pipeline/callbacks.h - Callback functions
    // - pipeline/creation.h - Element creation and configuration
    // - pipeline/linking.h - Pipeline linking
    // - pipeline/probes.h - Pad probes
    // - pipeline/statistics.h - Statistics logging
    // - pipeline/utils.h - Utility functions (filename generation, cleanup)
};

/**
 * @brief Initialize the pipeline debug category.
 */
void init_pipeline_debug_category();
