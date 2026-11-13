// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "random_access_bridge_orchestrator.h"

#include <variant>

#include "../../global_data.h"

namespace {
    [[nodiscard]] bool should_fail_fast(const random_access_bridge_core::step_result &result) {
        return result.status == random_access_bridge_core::step_status::fatal_stop;
    }
} // namespace

random_access_packer *random_access_get_packer_via_orchestrator(random_access_bridge_context &context,
                                                                const random_access_app_adapter &adapter) {
    if (context.data == nullptr) {
        adapter.request_fail_fast_stop("random_access context missing global_data");
        return nullptr;
    }

    const auto *ra_packer = std::get_if<std::unique_ptr<random_access_packer>>(&context.data->packer);
    if (!ra_packer || !*ra_packer) {
        adapter.request_fail_fast_stop("random_access_frame_processing called without random_access_packer");
        return nullptr;
    }

    context.packer = ra_packer->get();
    return context.packer;
}

random_access_bridge_core::step_result random_access_process_period_via_orchestrator(
        random_access_bridge_context &context, const random_access_app_adapter &adapter,
        const random_access_packer::complete_period &period, const GstClockTime frame_duration, GstClockTime &next_pts,
        std::chrono::steady_clock::time_point &last_push_time) {
    if (context.data == nullptr) {
        adapter.request_fail_fast_stop("random_access process invoked with null global_data");
        return {.status = random_access_bridge_core::step_status::fatal_stop, .flow_return = GST_FLOW_ERROR};
    }

    random_access_bridge_core::media_session session{*context.data};
    const auto result =
            random_access_bridge_core::process_period(session, period, frame_duration, next_pts, last_push_time);
    if (should_fail_fast(result)) {
        adapter.request_fail_fast_stop("random_access period processing reported fatal_stop");
    }
    return result;
}

random_access_bridge_core::step_result
random_access_cleanup_via_orchestrator(random_access_bridge_context &context,
                                       const random_access_app_adapter &adapter) {
    if (context.data == nullptr || context.packer == nullptr) {
        adapter.request_fail_fast_stop("random_access cleanup invoked with incomplete context");
        return {.status = random_access_bridge_core::step_status::fatal_stop, .flow_return = GST_FLOW_ERROR};
    }

    random_access_bridge_core::media_session session{*context.data};
    const auto result = random_access_bridge_core::cleanup(session, context.packer);
    if (should_fail_fast(result)) {
        adapter.request_fail_fast_stop("random_access cleanup reported fatal_stop");
    }
    return result;
}
