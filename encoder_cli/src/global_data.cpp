// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "global_data.h"

#include <array>
#include <chrono>
#include <gst/gst.h>
#include <new>
#include <string_view>
#include <thread>
#include "../libvcai/include/logging.h"
#include "application/app_periodic_gst_tasks.h"
#include "application/thread_management.h"
#include "encoder_backend/encoder_backend.h"
#include "encoder_cli_debug.h"
#include "gst/input_types/frame_path_meta.h"
#include "gst/rtp_heartbeat.h"
#include "gst/rtp_utils.h"

/// GStreamer debug category for packer initialization and status.
GST_DEBUG_CATEGORY_STATIC(packer_category);

/**
 * @brief Initializes the GStreamer debug category for packer operations.
 */
void init_packer_debug_category() {
    GST_DEBUG_CATEGORY_INIT(packer_category, "packer", 0, "Packer initialization and operations");
}

/**
 * @brief Gets the packer debug category.
 * @return Pointer to the packer debug category.
 */
GstDebugCategory *get_packer_debug_category() { return packer_category; }

/**
 * @file global_data.cpp
 * @brief Implementation of the global_data destructor.
 */

namespace {
    /**
     * @brief Enable bus flushing for a pipeline.
     * @param pipeline Pipeline wrapper to update.
     */
    void set_bus_flushing(const sf::unique_gst_element_ptr &pipeline) {
        if (!pipeline) {
            return;
        }

        if (GstBus *bus = gst_element_get_bus(pipeline.get())) {
            gst_bus_set_flushing(bus, TRUE);
            gst_object_unref(bus);
        }
    }

    /**
     * @brief Pause a pipeline and wait for state transition.
     * @param pipeline Pipeline wrapper to pause.
     * @param timeout State transition timeout.
     */
    void pause_pipeline(const sf::unique_gst_element_ptr &pipeline, const GstClockTime timeout) {
        if (!pipeline) {
            return;
        }

        gst_element_set_state(pipeline.get(), GST_STATE_PAUSED);
        gst_element_get_state(pipeline.get(), nullptr, nullptr, timeout);
    }

    /**
     * @brief Send flush events to a pipeline.
     * @param pipeline Pipeline wrapper to flush.
     */
    void flush_pipeline(const sf::unique_gst_element_ptr &pipeline) {
        if (!pipeline) {
            return;
        }

        gst_element_send_event(pipeline.get(), gst_event_new_flush_start());
        gst_element_send_event(pipeline.get(), gst_event_new_flush_stop(true));
    }

    /**
     * @brief Transition a pipeline to NULL with async handling.
     * @param pipeline Pipeline wrapper to stop.
     * @param timeout State transition timeout.
     * @param extra_wait Additional wait when transition stays async.
     */
    void null_pipeline(const sf::unique_gst_element_ptr &pipeline, const GstClockTime timeout,
                       const std::chrono::milliseconds extra_wait) {
        if (!pipeline) {
            return;
        }

        gst_element_set_state(pipeline.get(), GST_STATE_NULL);
        GstState current_state = GST_STATE_NULL;
        GstState pending = GST_STATE_NULL;
        const GstStateChangeReturn get_result =
                gst_element_get_state(pipeline.get(), &current_state, &pending, timeout);
        if (get_result == GST_STATE_CHANGE_ASYNC && pending != GST_STATE_NULL) {
            std::this_thread::sleep_for(extra_wait);
            gst_element_get_state(pipeline.get(), &current_state, &pending, timeout);
        }
    }
} // namespace

global_data::~global_data() {
    join_rtp_heartbeat_if_running(*this);
    join_app_periodic_reporting_gst_task(*this);
    join_file_mode_watchdog_gst_task(*this);

    // If shutdown() was called, pipelines are already in NULL state.
    // Release ownership without destroying - gst_object_unref on DeepStream elements
    // can hang indefinitely due to GPU resource cleanup. Since we're exiting anyway,
    // let the OS clean up the resources.
    if (stop_processing.load()) {
        (void) output_pipeline.release();
        (void) encoding_pipeline.release();
        (void) input_pipeline.release();
        return;
    }

    // Only reach here if shutdown() was NOT called (early destruction or error path).
    // Safely transition pipelines to NULL state.
    const std::array<const sf::unique_gst_element_ptr *, 3> pipelines = {&input_pipeline, &encoding_pipeline,
                                                                         &output_pipeline};

    for (const auto *pipeline: pipelines) {
        pause_pipeline(*pipeline, 2 * GST_SECOND);
    }

    // Additional wait for DeepStream GPU operations (tracker, nvinfer) to complete.
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    for (const auto *pipeline: pipelines) {
        null_pipeline(*pipeline, GST_CLOCK_TIME_NONE, std::chrono::milliseconds(1000));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
}

/**
 * @brief Stops processing and tears down pipelines safely.
 *
 * Shuts down queues, pauses pipelines, flushes buffers, and transitions to NULL
 * while giving GPU elements time to finish outstanding work.
 */
void global_data::shutdown() {
    bool expected_shutdown_started = false;
    if (!shutdown_started.compare_exchange_strong(expected_shutdown_started, true)) {
        sf::warning(get_packer_debug_category(), "global_data::shutdown already in progress; skipping re-entry");
        return;
    }

    const bool fast_file_shutdown = params.input_type_value == input_type::file;

    sf::info(get_packer_debug_category(),
             "global_data::shutdown begin (input_type=%s, stop_processing=%s, assigned_port=%u, session_id=%u)",
             input_type_to_string(params.input_type_value), stop_processing.load() ? "true" : "false",
             assigned_rtp_port.load(), assigned_rtp_session_id.load());

    const uint16_t disconnect_port = assigned_rtp_port.load();
    const uint32_t disconnect_session = assigned_rtp_session_id.load();
    const bool skip_disconnect_for_file_mode = params.input_type_value == input_type::file;
    if (skip_disconnect_for_file_mode && disconnect_port > 0) {
        sf::info(get_packer_debug_category(),
                 "Skipping disconnect request during file-mode shutdown (transport teardown can block); "
                 "port=%u session_id=%u",
                 disconnect_port, disconnect_session);
    }
    if (!skip_disconnect_for_file_mode && !params.communication.host.empty() && params.communication.port > 0 &&
        disconnect_port > 0) {
        const std::string transport_type =
                (params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc) ? "webrtc"
                                                                                                              : "rtp";
        sf::info(get_packer_debug_category(),
                 "Sending disconnect request before shutdown (transport=%s port=%u session_id=%u)",
                 transport_type.c_str(), disconnect_port, disconnect_session);
        if (!send_rtp_disconnect(params.communication.host, params.communication.port, transport_type,
                                 params.webrtc.signaling_url, disconnect_port, disconnect_session)) {
            sf::warning(get_packer_debug_category(), "Disconnect request did not receive ack");
        }
    }

    // This will signal all threads waiting on queues to wake up and exit.
    (void) request_worker_stop_once(true);
    join_bypass_polling_threads(*this);
    join_rtp_heartbeat_if_running(*this);
    join_app_periodic_reporting_gst_task(*this);
    join_file_mode_watchdog_gst_task(*this);
    sf::info(get_packer_debug_category(), "global_data::shutdown: queues closed and stop_processing set");

    const std::array<const sf::unique_gst_element_ptr *, 3> pipelines = {&input_pipeline, &encoding_pipeline,
                                                                         &output_pipeline};
    for (const auto *pipeline: pipelines) {
        set_bus_flushing(*pipeline);
    }
    sf::info(get_packer_debug_category(), "global_data::shutdown: enabled bus flushing on all pipelines");

    // Give message processing threads time to exit their loops after seeing stop_processing=true
    // and returning from flushed bus operations.
    std::this_thread::sleep_for(fast_file_shutdown ? std::chrono::milliseconds(20) : std::chrono::milliseconds(100));

    // Set pipelines to PAUSED first to stop new data flow.
    const GstClockTime pause_timeout = fast_file_shutdown ? 500 * GST_MSECOND : 2 * GST_SECOND;
    for (const auto *pipeline: pipelines) {
        pause_pipeline(*pipeline, pause_timeout);
    }
    sf::info(get_packer_debug_category(), "global_data::shutdown: all pipelines paused");

    // Send flush events after pausing to clear any remaining buffers.
    // In fast file shutdown we intentionally skip output flush to avoid a known
    // blocking path in gst_element_send_event on output pipeline flush_start.
    for (const auto *pipeline: pipelines) {
        if (fast_file_shutdown && pipeline->get() == output_pipeline.get()) {
            sf::info(get_packer_debug_category(), "Skipping output pipeline flush in fast file-mode shutdown");
            continue;
        }
        flush_pipeline(*pipeline);
    }
    sf::info(get_packer_debug_category(), "global_data::shutdown: flush events sent");

    if (!fast_file_shutdown) {
        // Wait significantly longer to ensure all data flow has completely stopped
        // This is critical to avoid race conditions where buffers are pushed to released pads
        // The tee element in particular needs time to finish processing all branches
        // DeepStream tracker (nvtracker) needs additional time to complete GPU operations
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        // Additional wait specifically for DeepStream GPU operations (tracker, nvinfer) to complete.
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }

    // Now set pipelines to NULL state to stop them and release resources.
    const GstClockTime shutdown_timeout = fast_file_shutdown ? (1 * GST_SECOND) : (5 * GST_SECOND);
    const std::chrono::milliseconds input_extra_wait =
            fast_file_shutdown ? std::chrono::milliseconds(100) : std::chrono::milliseconds(2000);
    const std::chrono::milliseconds encoding_extra_wait =
            fast_file_shutdown ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000);
    const std::chrono::milliseconds output_extra_wait =
            fast_file_shutdown ? std::chrono::milliseconds(100) : std::chrono::milliseconds(1000);

    // Input pipeline needs a longer async grace period.
    null_pipeline(input_pipeline, shutdown_timeout, input_extra_wait);
    sf::info(get_packer_debug_category(), "global_data::shutdown: input pipeline transitioned to NULL");
    if (!fast_file_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    null_pipeline(encoding_pipeline, shutdown_timeout, encoding_extra_wait);
    sf::info(get_packer_debug_category(), "global_data::shutdown: encoding pipeline transitioned to NULL");
    null_pipeline(output_pipeline, shutdown_timeout, output_extra_wait);
    sf::info(get_packer_debug_category(), "global_data::shutdown: output pipeline transitioned to NULL");

    if (!fast_file_shutdown) {
        // Extra grace after NULL-state transitions (GPU/network teardown); heartbeat is already joined above.
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
    sf::info(get_packer_debug_category(), "global_data::shutdown complete");
}

/**
 * @brief Resets queues and counters for UDP reconnection.
 *
 * Keeps pipelines running but reinitializes queues and counters for a new stream.
 */
void global_data::reset_processing_state() {
    // Signal threads to stop processing current connection
    (void) request_worker_stop_once(true);
    join_bypass_polling_threads(*this);
    join_rtp_heartbeat_if_running(*this);

    // Drain any remaining items from queues
    raw_frame_queue.drain();
    encoded_frame_queue.drain();
    encoder_submission_queue.drain();
    packed_regions_queue.drain();
    display_regions_queue.drain();
    debug_regions_queue.drain();

    // Reset counters and state
    num_input_frames = 0;
    expected_file_input_frames.reset();
    file_input_forced_raw_eos = false;
    encoder_submission_queue_closed = false;
    file_mode_watchdog_triggered = false;
    current_frame_number.store(0);
    pending_qp_map_payload.reset();
    reset_bridge_lifecycle_state();

    // Reset stop_processing flag for next connection
    shutdown_started = false;
    stop_processing = false;

    // Reset queues in place using placement new (queues contain non-movable mutexes)
    raw_frame_queue.~thread_safe_queue();
    new (&raw_frame_queue) sf::thread_safe_queue<sf::unique_gst_sample_ptr>();
    encoded_frame_queue.~thread_safe_queue();
    new (&encoded_frame_queue) sf::thread_safe_queue<encoded_frame_item>();
    encoder_submission_queue.~thread_safe_queue();
    new (&encoder_submission_queue) sf::thread_safe_queue<encoder_submission_item>();
    packed_regions_queue.~thread_safe_queue();
    new (&packed_regions_queue) sf::thread_safe_queue<metadata_frame_bundle>();
    display_regions_queue.~thread_safe_queue();
    new (&display_regions_queue) sf::thread_safe_queue<metadata_frame_bundle>();
    debug_regions_queue.~thread_safe_queue();
    new (&debug_regions_queue) sf::thread_safe_queue<metadata_frame_bundle>();

    udp_input_runtime.reset_frame_tracking_counters();
    udp_frame_drop_manager.reset_for_processing_cycle();
    output_display_runtime.reset_for_processing_cycle();
    output_debug_runtime.reset_for_processing_cycle();
    output_probe_runtime.reset_for_processing_cycle();

    // Note: We don't reset pipelines to null state - they stay in PLAYING state
    // to be ready for the next connection
    restart_bypass_polling_threads_if_needed(*this);
}

void global_data::begin_worker_lifecycle_cycle() { worker_lifecycle.begin_cycle(); }

bool global_data::request_worker_stop_once(const bool close_metadata_queues) {
    const bool first_stop_transition = worker_lifecycle.request_stop_once();
    stop_processing = true;
    close_raw_queue_once();
    close_encoded_queue_once();
    if (close_metadata_queues) {
        close_metadata_queues_once();
    }
    return first_stop_transition;
}

void global_data::mark_processing_workers_active(const bool active) {
    worker_lifecycle.processing_workers_active = active;
}

void global_data::mark_message_workers_active(const bool active) { worker_lifecycle.message_workers_active = active; }

void global_data::mark_polling_workers_active(const bool active) { worker_lifecycle.polling_workers_active = active; }

void global_data::mark_worker_lifecycle_drained() { worker_lifecycle.mark_drained(); }

void global_data::close_raw_queue_once() {
    if (!ingress_runtime.raw_queue_closed.exchange(true)) {
        raw_frame_queue.close();
    }
}

void global_data::close_encoded_queue_once() {
    clear_pending_encoded_contract_states();
    if (!ingress_runtime.encoded_queue_closed.exchange(true)) {
        encoded_frame_queue.close();
    }
}

void global_data::close_encoder_submission_queue_once() {
    if (!encoder_submission_queue_closed.exchange(true)) {
        encoder_submission_queue.close();
    }
}

void global_data::close_metadata_queues_once() {
    if (!egress_bridge_runtime.metadata_queues_closed.exchange(true)) {
        packed_regions_queue.close();
        display_regions_queue.close();
        debug_regions_queue.close();
    }
}

void global_data::reset_bridge_lifecycle_state() {
    egress_bridge_runtime.reset();
    ingress_runtime.reset();
    worker_lifecycle.reset();
    memory_runtime.reset();
    metadata_runtime.reset();
}

/**
 * @brief Handles display window close events.
 * @return True if other outputs remain active, false if shutdown is required.
 */
bool global_data::handle_display_window_closed() {
    display_window_closed = true;

    // Check if other output branches are still active
    const bool has_h265_branch = params.debug.enable_h265_branch;
    const bool has_mp4_branch = params.debug.enable_mp4_branch;
    const bool has_debug_branch = params.debug.save_frames;
    const bool has_rtp_branch = !params.communication.host.empty() && params.communication.port > 0;

    if (const bool has_other_outputs = has_h265_branch || has_mp4_branch || has_debug_branch || has_rtp_branch;
        !has_other_outputs) {
        // Display was the only output, shutdown gracefully
        return false;
    }

    // Other outputs are still active, continue processing
    return true;
}

/**
 * @brief Loads static ROI data from JSON if configured.
 * @return True if data was loaded or not needed, false on errors.
 */
bool global_data::load_static_roi_data() {
    // Only load if static ROI JSON file is configured
    if (params.static_roi_json_file.empty()) {
        sf::info(get_static_roi_debug_category(),
                 "No static ROI JSON file configured, skipping static ROI data loading");
        return true;
    }

    sf::info(get_static_roi_debug_category(), "Loading static ROI data from: %s", params.static_roi_json_file.c_str());

    const auto loaded_data = ::load_static_roi_data(params.static_roi_json_file);
    if (!loaded_data) {
        sf::error(get_static_roi_debug_category(), "Failed to load static ROI data from %s",
                  params.static_roi_json_file.c_str());
        return false;
    }

    loaded_static_roi_data = *loaded_data;
    sf::info(get_static_roi_debug_category(), "Successfully loaded %zu static ROIs", loaded_static_roi_data->size());

    return true;
}

/**
 * @brief Loads per-frame groundtruth ROI data from COCO JSON if configured.
 * @return True if data was loaded or not needed, false on errors.
 */
bool global_data::load_groundtruth_roi_data() {
    if (params.groundtruth_roi_file.empty()) {
        sf::info(get_groundtruth_roi_debug_category(),
                 "No groundtruth ROI file configured, skipping groundtruth ROI data loading");
        return true;
    }

    sf::info(get_groundtruth_roi_debug_category(), "Loading groundtruth ROI data from: %s",
             params.groundtruth_roi_file.c_str());

    const auto loaded_data = ::load_groundtruth_roi_data(params.groundtruth_roi_file);
    if (!loaded_data) {
        sf::error(get_groundtruth_roi_debug_category(), "Failed to load groundtruth ROI data from %s",
                  params.groundtruth_roi_file.c_str());
        return false;
    }

    loaded_groundtruth_roi_data = *loaded_data;
    sf::info(get_groundtruth_roi_debug_category(),
             "Successfully loaded groundtruth ROI data: %zu frames, %zu total annotations",
             loaded_groundtruth_roi_data->size(), loaded_groundtruth_roi_data->total_annotations());

    return true;
}

void global_data::push_pending_encoded_contract_state(const uint64_t sequence_id,
                                                      encoded_frame_luma_adaptation_state state) {
    std::scoped_lock lock(pending_encoded_contract_mutex);
    pending_encoded_contract_states.push_back(
            pending_encoded_contract_state{.sequence_id = sequence_id, .luma_adaptation = std::move(state)});
}

std::optional<pending_encoded_contract_state> global_data::pop_pending_encoded_contract_state() {
    std::scoped_lock lock(pending_encoded_contract_mutex);
    if (pending_encoded_contract_states.empty()) {
        return std::nullopt;
    }

    pending_encoded_contract_state state = pending_encoded_contract_states.front();
    pending_encoded_contract_states.pop_front();
    return state;
}

bool global_data::remove_latest_pending_encoded_contract_state() {
    std::scoped_lock lock(pending_encoded_contract_mutex);
    if (pending_encoded_contract_states.empty()) {
        return false;
    }

    pending_encoded_contract_states.pop_back();
    return true;
}

void global_data::clear_pending_encoded_contract_states() {
    std::scoped_lock lock(pending_encoded_contract_mutex);
    pending_encoded_contract_states.clear();
}

bool global_data::wait_for_pending_encoded_contract_drain(const std::chrono::milliseconds timeout,
                                                          const bool honor_stop_request) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (honor_stop_request && stop_processing.load()) {
            return false;
        }
        {
            std::scoped_lock lock(pending_encoded_contract_mutex);
            if (pending_encoded_contract_states.empty()) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool global_data::has_pending_encoded_contract_states() {
    std::scoped_lock lock(pending_encoded_contract_mutex);
    return !pending_encoded_contract_states.empty();
}

bool global_data::has_encoding_flush_backlog() {
    if (has_pending_encoded_contract_states()) {
        return true;
    }

    if (encoded_ports.encoding_src == nullptr) {
        return false;
    }

    guint64 level_bytes = 0;
    guint level_buffers = 0;
    g_object_get(G_OBJECT(encoded_ports.encoding_src), "current-level-bytes", &level_bytes, "current-level-buffers",
                 &level_buffers, nullptr);
    return level_bytes > 0 || level_buffers > 0;
}

bool global_data::wait_for_encoding_appsrc_drain(const std::chrono::milliseconds timeout) {
    if (encoded_ports.encoding_src == nullptr) {
        return true;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (stop_processing.load()) {
            return false;
        }

        guint64 level_bytes = 0;
        guint level_buffers = 0;
        g_object_get(G_OBJECT(encoded_ports.encoding_src), "current-level-bytes", &level_bytes, "current-level-buffers",
                     &level_buffers, nullptr);
        if (level_bytes == 0 && level_buffers == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    guint64 level_bytes = 0;
    guint level_buffers = 0;
    g_object_get(G_OBJECT(encoded_ports.encoding_src), "current-level-bytes", &level_bytes, "current-level-buffers",
                 &level_buffers, nullptr);
    sf::warning(encoder_cli_main, "Encoding appsrc drain timed out with level_bytes=%llu level_buffers=%u",
                static_cast<unsigned long long>(level_bytes), level_buffers);
    return false;
}

void global_data::request_metadata_contract_fail_stop(const char *reason) {
    metadata_runtime.record_fail_stop();
    (void) request_worker_stop_once(true);
    sf::error(get_packer_debug_category(), "Metadata contract fail-fast stop requested: %s",
              reason != nullptr ? reason : "unknown reason");
}

bool global_data::app_should_force_close_file_raw_queue(const guint64 frame_count) {
    return params.input_type_value == input_type::file && expected_file_input_frames.has_value() &&
           frame_count >= *expected_file_input_frames;
}

bool global_data::app_mark_file_input_forced_raw_eos_once() { return !file_input_forced_raw_eos.exchange(true); }

void global_data::app_close_raw_queue_once() { close_raw_queue_once(); }

void global_data::app_close_encoded_queue_once() { close_encoded_queue_once(); }

std::optional<pending_encoded_contract_state> global_data::app_pop_pending_contract_state() {
    return pop_pending_encoded_contract_state();
}

void global_data::app_request_fail_fast_stop(const char *reason) {
    (void) request_worker_stop_once(true);
    sf::error(encoder_cli_main, "Fail-fast stop requested: %s", reason != nullptr ? reason : "unknown reason");
}

void global_data::app_request_metadata_contract_fail_stop(const char *reason) {
    request_metadata_contract_fail_stop(reason);
}

bool global_data::ing_should_process_raw_sample() {
    if (params.input_type_value == input_type::file) {
        return true;
    }
    return ready_to_process_frames.load();
}

uint64_t global_data::ing_record_dropped_raw_sample(ingress_bridge_context &context) {
    if (context.runtime == nullptr) {
        return 0;
    }
    return ++context.runtime->dropped_raw_sample_count;
}

uint64_t global_data::ing_record_encoded_sample_log(ingress_bridge_context &context) {
    if (context.runtime == nullptr) {
        return 0;
    }
    return ++context.runtime->encoded_sample_log_count;
}

bool global_data::ing_enqueue_raw_sample_with_contract(sf::unique_gst_sample_ptr sample, const frame_path_type path,
                                                       GstDebugCategory *category) {
    const uint64_t sequence_id = metadata_runtime.next_sequence_id();
    if (!attach_frame_path_metadata(sample, path, sequence_id)) {
        metadata_runtime.record_validation_failure();
        request_metadata_contract_fail_stop("failed to attach raw frame contract metadata");
        sf::error(category, "Failed to attach frame contract metadata (sequence=%llu)",
                  static_cast<unsigned long long>(sequence_id));
        return false;
    }
    raw_frame_queue.push(std::move(sample));
    return true;
}

void global_data::ing_enqueue_encoded_item(encoded_frame_item item) { encoded_frame_queue.push(std::move(item)); }

guint64 global_data::ing_increment_input_frame_count() { return ++num_input_frames; }

void global_data::pip_record_roi_detection_update(const size_t detected_count, const size_t tracked_count,
                                                  const char *detection_mode_name) {
    if (!monitoring.status_reporter) {
        return;
    }

    monitoring.status_reporter->update_frame_processing();
    const std::string_view mode_name = detection_mode_name != nullptr ? detection_mode_name : "";
    if (mode_name == "inference") {
        monitoring.status_reporter->update_roi_detection(static_cast<int>(detected_count),
                                                         static_cast<int>(tracked_count));
    } else if (mode_name == "static" || mode_name == "groundtruth") {
        monitoring.status_reporter->update_roi_detection(static_cast<int>(detected_count), 0);
    }
}

void global_data::pip_record_roi_filtering_update(const int filtered_count) {
    if (filtered_count <= 0 || !monitoring.status_reporter) {
        return;
    }
    monitoring.status_reporter->update_roi_filtering(filtered_count);
}

void global_data::pip_record_encoded_frame_update() {
    if (!monitoring.status_reporter) {
        return;
    }
    monitoring.status_reporter->update_encoding();
}

void global_data::pip_record_stage_timing(const monitoring_stage stage,
                                          const std::chrono::steady_clock::time_point start_time,
                                          const std::chrono::steady_clock::time_point end_time) {
    if (!monitoring.perf_monitor || end_time < start_time) {
        return;
    }

    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    monitoring.perf_monitor->record_timing(stage, static_cast<uint64_t>(duration.count()));
}

void global_data::pip_close_metadata_queues_once() { close_metadata_queues_once(); }

bool global_data::pip_is_watchdog_forced_shutdown() { return file_mode_watchdog_triggered.load(); }

bool global_data::pip_has_active_encoder_backend() { return active_encoder_backend != nullptr; }

void global_data::pip_signal_encoder_backend_eos() {
    if (!active_encoder_backend) {
        return;
    }
    active_encoder_backend->signal_eos(*this);
}

void global_data::pip_report_output_pipeline_status(const char *status) {
    if (!monitoring.status_reporter) {
        return;
    }

    const char *const status_text = status != nullptr ? status : "unknown";
    monitoring.status_reporter->report_pipeline_status("output", status_text);
}

void global_data::enc_record_output_update() {
    if (monitoring.status_reporter) {
        monitoring.status_reporter->update_output();
    }
}

void global_data::enc_record_sei_injection(const guint64 sei_count) {
    if (monitoring.status_reporter && sei_count > 0) {
        monitoring.status_reporter->update_sei_injection(static_cast<int>(sei_count));
    }
}

void global_data::enc_record_encoder_submission() {
    if (monitoring.status_reporter) {
        monitoring.status_reporter->update_pushed_to_encoder();
    }
}

bool global_data::enc_rollback_latest_pending_luma_state() {
    if (remove_latest_pending_encoded_contract_state()) {
        return true;
    }

    sf::warning(encoder_cli_main, "Failed to rollback pending luma state; queue may be misaligned");
    return false;
}
