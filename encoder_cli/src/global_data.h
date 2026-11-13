// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <gst/gst.h>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

/**
 * @brief Initializes the packer debug category used by packing components.
 */
void init_packer_debug_category();
/**
 * @brief Returns the packer debug category instance.
 * @return The GStreamer debug category for packer logging.
 */
[[nodiscard]] GstDebugCategory *get_packer_debug_category();

#include <variant>
#include "../libvcai/include/bitrate_calculation.h"
#include "../libvcai/include/logging.h"
#include "../libvcai/include/pipeline_fps_monitor.h"
#include "application/worker_lifecycle_runtime.h"
#include "bridge/context/encoded_stream_ports.h"
#include "bridge/context/encoder_bridge_runtime.h"
#include "bridge/context/encoder_cli_app_host.h"
#include "bridge/context/ingress_bridge_runtime.h"
#include "bridge/context/memory_policy_runtime.h"
#include "bridge/context/metadata_contract_runtime.h"
#include "bridge/context/metadata_frame_bundle.h"
#include "encoded_frame_item.h"
#include "encoder_backend/encoder_backend.h"
#include "encoder_config.h"
#include "encoder_enums.h"
#include "encoder_submission_item.h"
#include "global_data_instance_runtime.h"
#include "gst/input_types/frame_drop_manager.h"
#include "gst_raii.h"
#include "input_analyzer/input_analyzer.h"
#include "packed_regions_info.h"
#include "reporting/performance_monitor.h"
#include "reporting/simple_status_reporter.h"
#include "roi/groundtruth_roi_data.h"
#include "roi/low_delay_packer.h"
#include "roi/random_access_packer.h"
#include "roi/roi_manager.h"
#include "roi/static_roi_data.h"
#include "runtime/bypass_state.h"
#include "runtime/monitoring_state.h"
#include "runtime/webrtc_state.h"
#include "thread_safe_queue.h"

/**
 * @file global_data.h
 * @brief Defines the central data structure shared across application threads.
 */

/**
 * @struct global_data
 * @brief Holds all shared state for the application.
 *
 * This includes GStreamer elements, thread-safe queues for inter-thread
 * communication, and other shared resources like the region map. This struct
 * is non-copyable and non-movable to enforce single ownership of the pipelines.
 */
struct global_data final : encoder_cli_app_host {
    /**
     * @brief Constructs the global_data struct.
     * @param config The encoder configuration for the application.
     */
    explicit global_data(encoder_config &&config) :
        params(std::move(config)), input_pipeline{}, encoding_pipeline{}, output_pipeline{}, encoded_ports{},
        encoder(nullptr), egress_bridge_runtime(), ingress_runtime(), worker_lifecycle(), memory_runtime(),
        metadata_runtime(), raw_frame_queue(), encoded_frame_queue(), encoder_submission_queue(),
        pending_encoded_contract_mutex(), pending_encoded_contract_states(), packed_regions_queue(),
        display_regions_queue(), debug_regions_queue(), bypass(), active_encoder_backend(nullptr),
        pending_qp_map_payload(std::nullopt), backend_supports_direct_qp_map(false), udp_bitrate_tracker(),
        shutdown_started(false), stop_processing(false), pgie_class_ids_to_filter{},
        roi_tracker(this->params.detection.base_confidence_threshold, this->params.detection.maximum_missing_frames,
                    this->params.detection.enable_prediction, this->params.detection.roi_accumulation_mode_value,
                    this->params.detection.roi_overlap_threshold,
                    [&]() {
                        stabilization_config settings;
                        settings.enabled = this->params.stabilization.enabled && this->params.inference_engine;
                        settings.mode = this->params.stabilization.mode;
                        settings.alpha = this->params.stabilization.alpha;
                        settings.alpha_size = this->params.stabilization.alpha_size;
                        settings.min_position_delta = this->params.stabilization.min_position_delta;
                        settings.min_size_delta = this->params.stabilization.min_size_delta;
                        settings.velocity_adaptive_factor = this->params.stabilization.velocity_adaptive_factor;
                        return settings;
                    }()),
        loaded_static_roi_data(std::nullopt), loaded_groundtruth_roi_data(std::nullopt), h265_filesink(nullptr),
        raw_video_parser(nullptr), num_input_frames(0), expected_file_input_frames(std::nullopt),
        file_input_forced_raw_eos(false), encoder_submission_queue_closed(false), file_mode_watchdog_triggered(false),
        current_frame_number(0), monitoring(),
        packer([&]() -> std::variant<std::unique_ptr<low_delay_packer>, std::unique_ptr<random_access_packer>> {
            // Use packer category - access via helper function
            auto *category = get_packer_debug_category();
            const int intra_period = get_intra_period(this->params);
            sf::info(category, "Initializing packer: packer_type='%s', num_frames=%d, intra_period=%d",
                     packer_type_to_string(this->params.packer_type_value), this->params.num_frames, intra_period);
            if (this->params.packer_type_value == packer_type::low_delay) {
                sf::info(category, "Creating low_delay_packer instance");
                return std::make_unique<low_delay_packer>(this->params.width, this->params.height, 64,
                                                          this->params.num_frames);
            }

            if (this->params.packer_type_value == packer_type::random_access) {
                sf::info(category, "Creating random_access_packer instance");
            } else {
                sf::warning(category, "Unknown packer_type, defaulting to random_access_packer");
            }

            return std::make_unique<random_access_packer>(this->params.width, this->params.height, default_ctu_size,
                                                          intra_period, 3);
        }()),
        webrtc(), adaptive_luma_analyzer(nullptr, &input_analyzer_destroy), adaptive_luma_mutex() {
        if (this->params.enable_status_reporter) {
            monitoring.status_reporter = std::make_unique<simple_status_reporter>(
                    this->params, bypass, memory_runtime, metadata_runtime, monitoring.fps_monitor, stop_processing,
                    udp_input_runtime, udp_frame_drop_manager, last_applied_luma_downscale, last_applied_luma_upscale);
        }
        if (this->params.enable_performance_monitoring) {
            monitoring.perf_monitor =
                    std::make_unique<performance_monitor>(this->params, ingress_runtime, memory_runtime,
                                                          metadata_runtime, stop_processing, udp_input_runtime);
        }
    }

    global_data(const global_data &) = delete;
    global_data &operator=(const global_data &) = delete;
    global_data(global_data &&) = delete;
    global_data &operator=(global_data &&) = delete;

    encoder_config params;

    sf::unique_gst_element_ptr input_pipeline;
    sf::unique_gst_element_ptr encoding_pipeline;
    sf::unique_gst_element_ptr output_pipeline;

    /**
     * @brief Non-owning handles for encoding and SEI appsrc elements (egress into encoder/output graphs).
     */
    encoded_stream_ports encoded_ports{};

    /**
     * @brief Non-owning pointer to the encoder element.
     * Its lifecycle is managed by the GStreamer pipeline.
     * @note This is a non-owning reference - do not call gst_object_unref() on this pointer.
     */
    GstElement *encoder;

    /**
     * @brief Runtime state for encoder egress bridge lifecycle.
     */
    encoded_bridge_runtime egress_bridge_runtime;

    /**
     * @brief Runtime state for encoder ingress bridge lifecycle.
     */
    ingress_bridge_runtime ingress_runtime;

    /**
     * @brief Runtime state for worker start/stop/join lifecycle sequencing.
     */
    worker_lifecycle_runtime worker_lifecycle;

    /**
     * @brief Runtime state for memory-domain and mutability fallback policy.
     */
    memory_policy_runtime memory_runtime;

    /**
     * @brief Runtime state for strict frame-metadata contract sequencing.
     */
    metadata_contract_runtime metadata_runtime;

    sf::thread_safe_queue<sf::unique_gst_sample_ptr> raw_frame_queue;
    /**
     * @brief Queue of encoded access units plus per-frame EOI signaling state.
     */
    sf::thread_safe_queue<encoded_frame_item> encoded_frame_queue;
    /**
     * @brief Queue decoupling ROI processing from blocking encoding appsrc pushes (file mode).
     */
    sf::thread_safe_queue<encoder_submission_item> encoder_submission_queue;
    /**
     * @brief Mutex protecting pending luma-adaptation signaling state handoff.
     */
    std::mutex pending_encoded_contract_mutex;
    /**
     * @brief Per-frame contract state queued from encoder submission to callback consumption.
     */
    std::deque<pending_encoded_contract_state> pending_encoded_contract_states;
    /**
     * @brief Queue for packed regions info, used only in --roi-data mode.
     * In this mode, frame order is guaranteed, so a simple FIFO is sufficient.
     */
    sf::thread_safe_queue<metadata_frame_bundle> packed_regions_queue;

    /**
     * @brief Queue for display borders derived from packed regions.
     * This is consumed by the display branch to add NvDsObjectMeta boxes.
     */
    sf::thread_safe_queue<metadata_frame_bundle> display_regions_queue;

    /**
     * @brief Queue for debug branch borders derived from packed regions.
     * This is consumed by the debug branch pad probe to draw software borders
     * on saved PNG frames.
     */
    sf::thread_safe_queue<metadata_frame_bundle> debug_regions_queue;

    bypass_state bypass;

    /**
     * @brief Active encoder backend selected during startup.
     */
    std::unique_ptr<encoder_backend> active_encoder_backend;

    /**
     * @brief QP-map payload produced for current frame and consumed on submission.
     */
    std::optional<qp_map_frame_payload> pending_qp_map_payload;

    /**
     * @brief True when direct NvEncodeAPI QP-map backend is active.
     */
    bool backend_supports_direct_qp_map;

    /**
     * @brief Bitrate tracker for incoming UDP data.
     * Tracks bitrate using a sliding window approach.
     */
    mutable sf::bitrate_tracker udp_bitrate_tracker;

    /**
     * @brief Per-instance UDP packet/frame probe counters (M9).
     */
    udp_input_runtime_state udp_input_runtime{};

    /**
     * @brief Per-instance adaptive frame drop state for UDP decoder output.
     */
    frame_drop_manager udp_frame_drop_manager{};

    /**
     * @brief Mutable display-branch probe and border-dump state (M9).
     */
    output_display_branch_runtime output_display_runtime{};

    /**
     * @brief Mutable debug-branch probe state (M9).
     */
    output_debug_branch_runtime output_debug_runtime{};

    /**
     * @brief Output pipeline diagnostic probe state (per-instance; appspace report follow-up).
     */
    output_pipeline_probe_runtime output_probe_runtime{};

    std::atomic_bool shutdown_started;
    std::atomic_bool stop_processing;

    /**
     * @brief Flag to track if the display window has been closed.
     */
    std::atomic_bool display_window_closed{false};

    /**
     * @brief Flag to track if the system is ready to process frames.
     * Frames are dropped until this flag is set to true, preventing buffering
     * of frames before processing threads are ready.
     */
    std::atomic_bool ready_to_process_frames{false};

    std::vector<size_t> pgie_class_ids_to_filter;

    /**
     * @brief ROI manager for tracking and predicting object positions.
     */
    roi_manager roi_tracker;

    /**
     * @brief Static ROI data loaded from JSON file (if provided).
     */
    std::optional<static_roi_data> loaded_static_roi_data;

    /**
     * @brief Per-frame groundtruth ROI data loaded from a COCO JSON file (if provided).
     *
     * When present, the groundtruth detection mode uses this to provide per-frame
     * ROIs from COCO annotations instead of running inference or using static ROIs.
     */
    std::optional<groundtruth_roi_data> loaded_groundtruth_roi_data;

    /**
     * @brief Non-owning pointer to the H.265 filesink element.
     * Its lifecycle is managed by the GStreamer pipeline.
     * @note This is a non-owning reference - do not call gst_object_unref() on this pointer.
     */
    GstElement *h265_filesink;

    /**
     * @brief Non-owning pointer to the raw video parser element.
     * Its lifecycle is managed by the GStreamer pipeline.
     * @note This is a non-owning reference - do not call gst_object_unref() on this pointer.
     */
    GstElement *raw_video_parser;

    guint64 num_input_frames;
    /**
     * @brief Expected number of input frames when running in file input mode.
     * Set during pipeline creation when file size and frame size are known.
     */
    std::optional<guint64> expected_file_input_frames;

    /**
     * @brief True when raw frame queue was force-closed after expected file frames.
     */
    std::atomic_bool file_input_forced_raw_eos;

    /**
     * @brief True once the encoder submission queue has been closed for this lifecycle.
     */
    std::atomic_bool encoder_submission_queue_closed;

    /**
     * @brief True when file-mode watchdog forced shutdown due to post-EOS stall.
     */
    std::atomic_bool file_mode_watchdog_triggered;

    /**
     * @brief Current frame number for frame counting.
     */
    std::atomic<int> current_frame_number;

    monitoring_state monitoring;

    /**
     * @brief Packer for rectangle packing (either low_delay or random_access).
     */
    std::variant<std::unique_ptr<low_delay_packer>, std::unique_ptr<random_access_packer>> packer;

    /**
     * @brief Assigned RTP port from server handshake (0 if RTP streaming is disabled).
     */
    std::atomic<uint16_t> assigned_rtp_port{0};

    /**
     * @brief Session id assigned by the RTP handshake (0 if unavailable).
     */
    std::atomic<uint32_t> assigned_rtp_session_id{0};

    /**
     * @brief Joinable RTP/WebRTC heartbeat worker (when communication streaming is enabled).
     */
    std::optional<std::jthread> rtp_heartbeat_thread{};

    /**
     * @brief Flag to track if heartbeat thread has been started.
     */
    std::atomic<bool> heartbeat_thread_started{false};

    /**
     * @brief File-mode idle watchdog (GstTask); prefer `output_pipeline`, else `input_pipeline` anchor semantics in
     * code.
     */
    bypass_gst_poll_slot file_mode_watchdog_slot{};

    /**
     * @brief Shared GstTask for periodic status and performance reporting ticks.
     */
    bypass_gst_poll_slot app_periodic_reporting_slot{};

    webrtc_state webrtc;

    /**
     * @brief Adaptive luma input analyzer (when enable_adaptive_luma_adaptation). Destroyed at shutdown.
     */
    input_analyzer_ptr adaptive_luma_analyzer{nullptr, &input_analyzer_destroy};
    std::mutex adaptive_luma_mutex;
    std::pair<double, double> adaptive_luma_cached_factors{1.0, 1.0};
    uint64_t adaptive_luma_frame_count{0};

    /** Last (d, u) actually applied when submitting a frame (for status reporting). */
    std::atomic<double> last_applied_luma_downscale{1.0};
    std::atomic<double> last_applied_luma_upscale{1.0};

    /**
     * @brief Destructor to clean up GStreamer resources.
     */
    ~global_data();

    /**
     * @brief Signals all threads to stop and cleans up resources.
     */
    void shutdown();

    /**
     * @brief Resets processing state for UDP reconnection (closes queues, resets flags, but keeps pipelines running).
     */
    void reset_processing_state();

    /**
     * @brief Starts a new worker lifecycle cycle generation.
     */
    void begin_worker_lifecycle_cycle();

    /**
     * @brief Requests worker stop once and closes queues in deterministic order.
     * @param close_metadata_queues True to also close packed/display/debug queues.
     * @return True when this call performed first stop transition for the generation.
     */
    [[nodiscard]] bool request_worker_stop_once(bool close_metadata_queues);

    /**
     * @brief Marks processing worker group activity.
     * @param active True while processing workers are running.
     */
    void mark_processing_workers_active(bool active);

    /**
     * @brief Marks message worker group activity.
     * @param active True while message workers are running.
     */
    void mark_message_workers_active(bool active);

    /**
     * @brief Marks bypass polling worker group activity.
     * @param active True while bypass polling workers are running.
     */
    void mark_polling_workers_active(bool active);

    /**
     * @brief Marks the current worker lifecycle cycle as drained.
     */
    void mark_worker_lifecycle_drained();

    /**
     * @brief Closes raw frame queue exactly once per lifecycle cycle.
     */
    void close_raw_queue_once();

    /**
     * @brief Closes encoded frame queue exactly once per lifecycle cycle.
     */
    void close_encoded_queue_once();

    /**
     * @brief Closes encoder submission queue exactly once per lifecycle cycle.
     */
    void close_encoder_submission_queue_once();

    /**
     * @brief Closes metadata/debug queues exactly once per lifecycle cycle.
     */
    void close_metadata_queues_once();

    /**
     * @brief Resets ingress/egress lifecycle runtime state for next startup cycle.
     */
    void reset_bridge_lifecycle_state();

    /**
     * @brief Handles graceful shutdown when display window is closed.
     * @return true if application should continue, false if it should shutdown.
     */
    [[nodiscard]] bool handle_display_window_closed();

    /**
     * @brief Loads static ROI data from JSON file if configured.
     * @return true if loading was successful or not needed, false on error.
     */
    [[nodiscard]] bool load_static_roi_data();

    /**
     * @brief Loads per-frame groundtruth ROI data from COCO JSON file if configured.
     * @return true if loading was successful or not needed, false on error.
     */
    [[nodiscard]] bool load_groundtruth_roi_data();

    /**
     * @brief Queues per-frame encoded contract state for callback consumption.
     * @param sequence_id Strict contract sequence id.
     * @param state Per-frame luma-adaptation state determined at submission.
     */
    void push_pending_encoded_contract_state(uint64_t sequence_id, encoded_frame_luma_adaptation_state state);

    /**
     * @brief Pops next pending per-frame encoded contract state.
     * @return Pending state when available, std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<pending_encoded_contract_state> pop_pending_encoded_contract_state();

    /**
     * @brief Removes the most recently queued per-frame encoded contract state.
     * @return True when a state was removed, false when queue was empty.
     */
    [[nodiscard]] bool remove_latest_pending_encoded_contract_state();

    /**
     * @brief Clears all queued per-frame encoded contract states.
     */
    void clear_pending_encoded_contract_states();

    /**
     * @brief Blocks until all pending encoded contract states are consumed by callbacks.
     * @param timeout Maximum wait duration.
     * @param honor_stop_request When false, keep waiting even if stop_processing is set (file-mode flush).
     * @return true when the queue drained, false on timeout or stop request.
     */
    [[nodiscard]] bool wait_for_pending_encoded_contract_drain(std::chrono::milliseconds timeout,
                                                               bool honor_stop_request = true);

    /**
     * @brief Returns true while encoded callbacks have not yet consumed all submitted frame contracts.
     */
    [[nodiscard]] bool has_pending_encoded_contract_states();

    /**
     * @brief Returns true while frames remain in the encoding appsrc queue or pending contract backlog.
     */
    [[nodiscard]] bool has_encoding_flush_backlog();

    /**
     * @brief Waits until the encoding appsrc has no queued input buffers/bytes.
     */
    [[nodiscard]] bool wait_for_encoding_appsrc_drain(std::chrono::milliseconds timeout);

    /**
     * @brief Triggers a strict metadata-contract fail-fast stop and counts it.
     * @param reason Human-readable reason for diagnostics.
     */
    void request_metadata_contract_fail_stop(const char *reason);

    // encoder_cli_app_host
    [[nodiscard]] bool app_should_force_close_file_raw_queue(guint64 frame_count) override;
    [[nodiscard]] bool app_mark_file_input_forced_raw_eos_once() override;
    void app_close_raw_queue_once() override;
    void app_close_encoded_queue_once() override;
    [[nodiscard]] std::optional<pending_encoded_contract_state> app_pop_pending_contract_state() override;
    void app_request_fail_fast_stop(const char *reason) override;
    void app_request_metadata_contract_fail_stop(const char *reason) override;

    [[nodiscard]] bool ing_should_process_raw_sample() override;
    [[nodiscard]] uint64_t ing_record_dropped_raw_sample(ingress_bridge_context &context) override;
    [[nodiscard]] uint64_t ing_record_encoded_sample_log(ingress_bridge_context &context) override;
    [[nodiscard]] bool ing_enqueue_raw_sample_with_contract(sf::unique_gst_sample_ptr sample, frame_path_type path,
                                                            GstDebugCategory *category) override;
    void ing_enqueue_encoded_item(encoded_frame_item item) override;
    [[nodiscard]] guint64 ing_increment_input_frame_count() override;

    void pip_record_roi_detection_update(size_t detected_count, size_t tracked_count,
                                         const char *detection_mode_name) override;
    void pip_record_roi_filtering_update(int filtered_count) override;
    void pip_record_encoded_frame_update() override;
    void pip_record_stage_timing(monitoring_stage stage, std::chrono::steady_clock::time_point start_time,
                                 std::chrono::steady_clock::time_point end_time) override;
    void pip_close_metadata_queues_once() override;
    [[nodiscard]] bool pip_is_watchdog_forced_shutdown() override;
    [[nodiscard]] bool pip_has_active_encoder_backend() override;
    void pip_signal_encoder_backend_eos() override;
    void pip_report_output_pipeline_status(const char *status) override;

    void enc_record_output_update() override;
    void enc_record_sei_injection(guint64 sei_count) override;
    void enc_record_encoder_submission() override;
    [[nodiscard]] bool enc_rollback_latest_pending_luma_state() override;
};
