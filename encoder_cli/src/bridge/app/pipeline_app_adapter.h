// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <chrono>
#include <cstddef>

#include "../context/encoder_cli_app_host.h"

/**
 * @file pipeline_app_adapter.h
 * @brief App-side adapter for non-media side effects used by processing loops.
 */

/**
 * @brief Bridges processing-side app concerns behind an explicit boundary.
 */
class pipeline_app_adapter {
public:
    explicit pipeline_app_adapter(encoder_cli_app_host &host_ref) : host(host_ref) {}

    void record_roi_detection_update(size_t detected_count, size_t tracked_count,
                                     const char *detection_mode_name) const {
        host.pip_record_roi_detection_update(detected_count, tracked_count, detection_mode_name);
    }

    void record_roi_filtering_update(int filtered_count) const { host.pip_record_roi_filtering_update(filtered_count); }

    void record_encoded_frame_update() const { host.pip_record_encoded_frame_update(); }

    void record_stage_timing(monitoring_stage stage, std::chrono::steady_clock::time_point start_time,
                             std::chrono::steady_clock::time_point end_time) const {
        host.pip_record_stage_timing(stage, start_time, end_time);
    }

    void request_fail_fast_stop(const char *reason) const { host.app_request_fail_fast_stop(reason); }

    void request_metadata_contract_fail_stop(const char *reason) const {
        host.app_request_metadata_contract_fail_stop(reason);
    }

    [[nodiscard]] bool should_force_close_file_raw_queue(guint64 processed_frames) const {
        return host.app_should_force_close_file_raw_queue(processed_frames);
    }

    [[nodiscard]] bool mark_file_input_forced_raw_eos_once() const {
        return host.app_mark_file_input_forced_raw_eos_once();
    }

    void close_raw_queue_once() const { host.app_close_raw_queue_once(); }

    void close_encoded_queue_once() const { host.app_close_encoded_queue_once(); }

    void close_metadata_queues_once() const { host.pip_close_metadata_queues_once(); }

    [[nodiscard]] bool is_watchdog_forced_shutdown() const { return host.pip_is_watchdog_forced_shutdown(); }

    [[nodiscard]] bool has_active_encoder_backend() const { return host.pip_has_active_encoder_backend(); }

    void signal_encoder_backend_eos() const { host.pip_signal_encoder_backend_eos(); }

    void report_output_pipeline_status(const char *status) const { host.pip_report_output_pipeline_status(status); }

private:
    encoder_cli_app_host &host;
};
