// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoded_frame_processing.h"

#include "../bridge/app/pipeline_app_adapter.h"
#include "../bridge/context/encoded_injection_bridge_context.h"
#include "../bridge/orchestration/encoded_injection_bridge_orchestrator.h"
#include "../reporting/performance_monitor.h"
#include "memory_contract_helper.h"

#include <chrono>
#include <gst/gst.h>

#include "../../libvcai/include/logging.h"

GST_DEBUG_CATEGORY_STATIC(encoded_frame_processing);

void init_encoded_frame_processing_debug_category() {
    GST_DEBUG_CATEGORY_INIT(encoded_frame_processing, "encoded-frame-processing", 0,
                            "Processing of encoded H.265 frames and SEI injection");
}

namespace {

    [[nodiscard]] guint64 get_frame_skip(const global_data &data) {
        if (data.params.roi_info && data.params.roi_info->frame_rate_details &&
            data.params.roi_info->frame_rate_details->frame_skip >= 0) {
            return static_cast<guint64>(data.params.roi_info->frame_rate_details->frame_skip);
        }
        return 0;
    }

    [[nodiscard]] encoded_injection_bridge_context make_encoded_injection_context(global_data &data,
                                                                                  const uint64_t frame_skip) {
        return encoded_injection_bridge_context{
                .ports = &data.encoded_ports,
                .egress_runtime = &data.egress_bridge_runtime,
                .metadata_runtime = &data.metadata_runtime,
                .packed_regions_queue = &data.packed_regions_queue,
                .memory_runtime = &data.memory_runtime,
                .params = &data.params,
                .host = &data,
                .frame_counter = 0,
                .frame_skip = frame_skip,
        };
    }

    enum class encoded_frame_iter_result { Continue, ExitLoop };

    [[nodiscard]] encoded_frame_iter_result process_encoded_frames_iteration(global_data &data,
                                                                             encoded_injection_bridge_context &context,
                                                                             pipeline_app_adapter &app_adapter) {
        auto item_opt = data.encoded_frame_queue.pop();
        if (!item_opt) {
            sf::info(encoded_frame_processing, "Encoded frame processing finished (queue drained).");
            return encoded_frame_iter_result::ExitLoop;
        }

        const auto start_time = std::chrono::steady_clock::now();

        auto item = std::move(*item_opt);
        auto sample = std::move(item.sample);
        if (const GstCaps *sample_caps = gst_sample_get_caps(sample.get())) {
            const auto sample_domain = classify_caps_memory_domain(sample_caps);
            if (sample_domain != memory_domain_contract::unknown && sample_domain != memory_domain_contract::system) {
                sf::warning(encoded_frame_processing,
                            "Encoded sample caps report non-system domain '%s'; map fallback may be used",
                            memory_domain_to_string(sample_domain));
            }
        }
        auto *buffer_in = gst_sample_get_buffer(sample.get());
        if (buffer_in == nullptr) {
            sf::warning(encoded_frame_processing, "Skipping encoded sample with null buffer");
            return encoded_frame_iter_result::Continue;
        }

        app_adapter.record_encoded_frame_update();

        const auto step_result = encoded_injection_process_frame_via_orchestrator(
                context, buffer_in, item.luma_adaptation, item.contract_sequence_id);
        if (step_result.status == encoded_bridge_step_status::fatal_stop) {
            sf::warning(encoded_frame_processing, "Fatal stop while processing encoded frame: %s",
                        gst_flow_get_name(step_result.flow_return));
            return encoded_frame_iter_result::ExitLoop;
        }
        if (step_result.flow_return != GST_FLOW_OK) {
            sf::warning(encoded_frame_processing, "Failed to push encoded frame to sei appsrc: %s",
                        gst_flow_get_name(step_result.flow_return));
        }

        app_adapter.record_stage_timing(monitoring_stage::encoded_frame_processing, start_time,
                                        std::chrono::steady_clock::now());
        return encoded_frame_iter_result::Continue;
    }

    void run_encoded_frame_epilogue([[maybe_unused]] global_data &data, encoded_injection_bridge_context &context,
                                    pipeline_app_adapter &app_adapter) {
        if (app_adapter.is_watchdog_forced_shutdown()) {
            sf::warning(encoded_frame_processing,
                        "Skipping appsrc EOS during watchdog-triggered forced shutdown to avoid output pipeline stall");
            return;
        }

        const auto eos_result = encoded_injection_signal_eos_via_orchestrator(context);
        if (eos_result.status == encoded_bridge_step_status::fatal_stop) {
            sf::warning(encoded_frame_processing, "Failed to send EOS to sei appsrc: %s",
                        gst_flow_get_name(eos_result.flow_return));
        }
    }

    struct encoded_frame_task_userdata {
        global_data *data = nullptr;
        encoded_injection_bridge_context context{};
        std::unique_ptr<pipeline_app_adapter> app_adapter{};
        GstTask *self_task = nullptr;
        std::atomic<bool> epilogue_done{false};
    };

    void encoded_frame_epilogue_once(encoded_frame_task_userdata *ud) {
        bool expected = false;
        if (!ud->epilogue_done.compare_exchange_strong(expected, true)) {
            return;
        }
        run_encoded_frame_epilogue(*ud->data, ud->context, *ud->app_adapter);
    }

    void encoded_frame_task_entry(gpointer user_data) {
        auto *ud = static_cast<encoded_frame_task_userdata *>(user_data);
        const auto step = process_encoded_frames_iteration(*ud->data, ud->context, *ud->app_adapter);
        if (step == encoded_frame_iter_result::ExitLoop) {
            encoded_frame_epilogue_once(ud);
            gst_task_stop(GST_TASK(ud->self_task));
        }
    }

    void destroy_encoded_frame_task_userdata(gpointer user_data) {
        delete static_cast<encoded_frame_task_userdata *>(user_data);
    }

} // namespace

void process_encoded_frames(global_data &data) {
    sf::info(encoded_frame_processing, "Starting encoded frame processing thread");
    pipeline_app_adapter app_adapter(data);

    auto context = make_encoded_injection_context(data, get_frame_skip(data));

    while (process_encoded_frames_iteration(data, context, app_adapter) == encoded_frame_iter_result::Continue) {
    }

    run_encoded_frame_epilogue(data, context, app_adapter);
}

bool start_encoded_frames_gst_task(global_data &data, bypass_gst_poll_slot &slot) {
    if (slot.task != nullptr) {
        return false;
    }

    // RAII_EXEMPT: GstTask user_data; freed in destroy_encoded_frame_task_userdata.
    auto *ud = new encoded_frame_task_userdata{};
    ud->data = &data;
    ud->context = make_encoded_injection_context(data, get_frame_skip(data));
    ud->app_adapter = std::make_unique<pipeline_app_adapter>(data);

    GstTask *const task = gst_task_new(encoded_frame_task_entry, ud, destroy_encoded_frame_task_userdata);
    if (!task) {
        delete ud;
        sf::error(encoded_frame_processing, "Failed to create GstTask for encoded frame processing");
        return false;
    }
    ud->self_task = task;

    gst_task_set_lock(task, &slot.lock);
    sf::info(encoded_frame_processing, "Encoded frame processing GstTask starting");
    if (!gst_task_start(task)) {
        sf::error(encoded_frame_processing, "Failed to start GstTask for encoded frame processing");
        gst_object_unref(task);
        return false;
    }

    slot.task = task;
    return true;
}

void join_encoded_frames_gst_task(global_data & /*data*/, bypass_gst_poll_slot &slot) {
    if (slot.task == nullptr) {
        return;
    }
    GstTask *const task = slot.task;
    slot.task = nullptr;

    gst_task_stop(task);
    if (!gst_task_join(task)) {
        sf::warning(encoded_frame_processing, "gst_task_join failed for encoded frame GstTask");
    }
    gst_object_unref(task);
}
