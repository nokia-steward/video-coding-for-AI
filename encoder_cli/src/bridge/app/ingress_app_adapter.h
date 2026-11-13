// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../context/encoder_cli_app_host.h"
#include "../context/ingress_bridge_context.h"

/**
 * @file ingress_app_adapter.h
 * @brief App-side adapter for ingress callback/polling side effects.
 */

/**
 * @brief Bridges ingress media results to application-owned queues and counters.
 */
class ingress_app_adapter {
public:
    explicit ingress_app_adapter(encoder_cli_app_host &host_ref) : host(host_ref) {}

    [[nodiscard]] bool should_process_raw_sample() const { return host.ing_should_process_raw_sample(); }

    [[nodiscard]] uint64_t record_dropped_raw_sample(ingress_bridge_context &context) const {
        return host.ing_record_dropped_raw_sample(context);
    }

    [[nodiscard]] uint64_t record_encoded_sample_log(ingress_bridge_context &context) const {
        return host.ing_record_encoded_sample_log(context);
    }

    [[nodiscard]] bool enqueue_raw_sample_with_contract(sf::unique_gst_sample_ptr sample, const frame_path_type path,
                                                        GstDebugCategory *category) const {
        return host.ing_enqueue_raw_sample_with_contract(std::move(sample), path, category);
    }

    void enqueue_encoded_item(encoded_frame_item item) const { host.ing_enqueue_encoded_item(std::move(item)); }

    [[nodiscard]] guint64 increment_input_frame_count() const { return host.ing_increment_input_frame_count(); }

    [[nodiscard]] bool should_force_close_file_raw_queue(const guint64 pushed_frames) const {
        return host.app_should_force_close_file_raw_queue(pushed_frames);
    }

    [[nodiscard]] bool mark_file_input_forced_raw_eos_once() const {
        return host.app_mark_file_input_forced_raw_eos_once();
    }

    void close_raw_queue_once(ingress_bridge_context &context) const {
        (void) context;
        host.app_close_raw_queue_once();
    }

    void close_encoded_queue_once(ingress_bridge_context &context) const {
        (void) context;
        host.app_close_encoded_queue_once();
    }

    [[nodiscard]] std::optional<pending_encoded_contract_state> pop_pending_contract_state() const {
        return host.app_pop_pending_contract_state();
    }

    void request_fail_fast_stop(const char *reason) const { host.app_request_fail_fast_stop(reason); }

private:
    encoder_cli_app_host &host;
};
