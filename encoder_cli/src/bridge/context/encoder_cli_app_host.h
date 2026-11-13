// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <gst/gst.h>

#include "../../encoded_frame_item.h"
#include "../../gst/input_types/frame_merger.h"
#include "../../reporting/performance_monitor.h"
#include "ingress_bridge_context.h"

/**
 * @file encoder_cli_app_host.h
 * @brief Abstract application host for bridge adapters (plugin-migration boundary).
 *
 * Bridge app adapters depend on this interface instead of concrete global_data so a
 * future GStreamer element can supply an alternate implementation while tests use fakes.
 */
class encoder_cli_app_host {
public:
    encoder_cli_app_host() = default;
    virtual ~encoder_cli_app_host() = default;

    encoder_cli_app_host(const encoder_cli_app_host &) = delete;
    encoder_cli_app_host &operator=(const encoder_cli_app_host &) = delete;
    encoder_cli_app_host(encoder_cli_app_host &&) = delete;
    encoder_cli_app_host &operator=(encoder_cli_app_host &&) = delete;

    // --- Shared application hooks (ingress + pipeline + encoder + random_access) ---
    [[nodiscard]] virtual bool app_should_force_close_file_raw_queue(guint64 frame_count) = 0;
    [[nodiscard]] virtual bool app_mark_file_input_forced_raw_eos_once() = 0;
    virtual void app_close_raw_queue_once() = 0;
    virtual void app_close_encoded_queue_once() = 0;
    [[nodiscard]] virtual std::optional<pending_encoded_contract_state> app_pop_pending_contract_state() = 0;
    virtual void app_request_fail_fast_stop(const char *reason) = 0;
    virtual void app_request_metadata_contract_fail_stop(const char *reason) = 0;

    // --- Ingress bridge ---
    [[nodiscard]] virtual bool ing_should_process_raw_sample() = 0;
    [[nodiscard]] virtual uint64_t ing_record_dropped_raw_sample(ingress_bridge_context &context) = 0;
    [[nodiscard]] virtual uint64_t ing_record_encoded_sample_log(ingress_bridge_context &context) = 0;
    [[nodiscard]] virtual bool ing_enqueue_raw_sample_with_contract(sf::unique_gst_sample_ptr sample,
                                                                    frame_path_type path,
                                                                    GstDebugCategory *category) = 0;
    virtual void ing_enqueue_encoded_item(encoded_frame_item item) = 0;
    [[nodiscard]] virtual guint64 ing_increment_input_frame_count() = 0;

    // --- Processing / pipeline adapter ---
    virtual void pip_record_roi_detection_update(size_t detected_count, size_t tracked_count,
                                                 const char *detection_mode_name) = 0;
    virtual void pip_record_roi_filtering_update(int filtered_count) = 0;
    virtual void pip_record_encoded_frame_update() = 0;
    virtual void pip_record_stage_timing(monitoring_stage stage, std::chrono::steady_clock::time_point start_time,
                                         std::chrono::steady_clock::time_point end_time) = 0;
    virtual void pip_close_metadata_queues_once() = 0;
    [[nodiscard]] virtual bool pip_is_watchdog_forced_shutdown() = 0;
    [[nodiscard]] virtual bool pip_has_active_encoder_backend() = 0;
    virtual void pip_signal_encoder_backend_eos() = 0;
    virtual void pip_report_output_pipeline_status(const char *status) = 0;

    // --- Encoder egress adapter ---
    virtual void enc_record_output_update() = 0;
    virtual void enc_record_sei_injection(guint64 sei_count) = 0;
    virtual void enc_record_encoder_submission() = 0;
    [[nodiscard]] virtual bool enc_rollback_latest_pending_luma_state() = 0;
};
