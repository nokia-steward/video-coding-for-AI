// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoded_injection_bridge_core.h"

#include "../../../../libvcai/include/encoder_optimization_info.h"
#include "../../../../libvcai/include/logging.h"
#include "../../../../libvcai/include/luma_adaptation_math.h"
#include "../../../../libvcai/include/nal_utils.h"
#include "../../processing/encoded_frame_processing.h"
#include "../../processing/memory_contract_helper.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

namespace {
    [[nodiscard]] bool map_buffer_for_read_with_fallback(encoded_injection_transform_deps deps, GstBuffer *input_buffer,
                                                         GstMapInfo &map_info,
                                                         sf::unique_gst_buffer_ptr &fallback_buffer,
                                                         const char *stage_label) {
        if (gst_buffer_map(input_buffer, &map_info, GST_MAP_READ)) {
            return true;
        }

        fallback_buffer = create_system_memory_fallback_copy(input_buffer, deps.memory_runtime,
                                                             get_packer_debug_category(), stage_label);
        if (!fallback_buffer) {
            return false;
        }

        if (gst_buffer_map(fallback_buffer.get(), &map_info, GST_MAP_READ)) {
            return true;
        }

        deps.memory_runtime.record_fallback_failure();
        sf::error(get_packer_debug_category(), "[%s] Fallback buffer could not be mapped for read access", stage_label);
        fallback_buffer.reset();
        return false;
    }

    [[nodiscard]] sf::unique_gst_buffer_ptr create_owned_buffer_from_bytes(const std::vector<uint8_t> &bytes,
                                                                           GstBuffer *source) {
        auto buffer_out = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, bytes.size(), nullptr));
        if (!buffer_out) {
            return nullptr;
        }

        GstMapInfo map_info{};
        if (!gst_buffer_map(buffer_out.get(), &map_info, GST_MAP_WRITE)) {
            return nullptr;
        }
        std::memcpy(map_info.data, bytes.data(), bytes.size());
        gst_buffer_unmap(buffer_out.get(), &map_info);

        GST_BUFFER_PTS(buffer_out.get()) = GST_BUFFER_PTS(source);
        GST_BUFFER_DTS(buffer_out.get()) = GST_BUFFER_DTS(source);
        GST_BUFFER_DURATION(buffer_out.get()) = GST_BUFFER_DURATION(source);
        GST_BUFFER_OFFSET(buffer_out.get()) = GST_BUFFER_OFFSET(source);
        GST_BUFFER_OFFSET_END(buffer_out.get()) = GST_BUFFER_OFFSET_END(source);
        GST_BUFFER_FLAGS(buffer_out.get()) = GST_BUFFER_FLAGS(source);
        return buffer_out;
    }

    [[nodiscard]] std::vector<uint8_t>
    inject_pri_and_optional_eoi(encoded_injection_transform_deps deps, GstBuffer *input_buffer,
                                const sf::packed_regions_info &region_info,
                                const encoded_frame_luma_adaptation_state &luma_state, bool &map_ok) {
        map_ok = false;
        sf::unique_gst_buffer_ptr fallback_buffer;
        GstMapInfo map_info{};
        if (!map_buffer_for_read_with_fallback(deps, input_buffer, map_info, fallback_buffer,
                                               "encoded_injection.transform_with_metadata")) {
            return {};
        }

        map_ok = true;
        const auto sei_nal_unit = sf::create_sei_nal_unit(region_info);
        const auto frame_with_pri = sf::inject_sei_message(map_info.data, map_info.size, sei_nal_unit);
        gst_buffer_unmap(fallback_buffer ? fallback_buffer.get() : input_buffer, &map_info);
        return maybe_inject_eoi_sei(frame_with_pri.data(), frame_with_pri.size(),
                                    deps.params.is_background_blur_effective(), deps.params.is_qp_map_delta_effective(),
                                    deps.params.qp_map_background_delta, luma_state.enabled,
                                    luma_state.downscale_factor, luma_state.upscale_multiplier);
    }

    [[nodiscard]] std::vector<uint8_t> inject_optional_eoi_only(encoded_injection_transform_deps deps,
                                                                GstBuffer *input_buffer,
                                                                const encoded_frame_luma_adaptation_state &luma_state,
                                                                bool &map_ok) {
        map_ok = false;
        sf::unique_gst_buffer_ptr fallback_buffer;
        GstMapInfo map_info{};
        if (!map_buffer_for_read_with_fallback(deps, input_buffer, map_info, fallback_buffer,
                                               "encoded_injection.transform_without_metadata")) {
            return {};
        }
        map_ok = true;
        const auto injected =
                maybe_inject_eoi_sei(map_info.data, map_info.size, deps.params.is_background_blur_effective(),
                                     deps.params.is_qp_map_delta_effective(), deps.params.qp_map_background_delta,
                                     luma_state.enabled, luma_state.downscale_factor, luma_state.upscale_multiplier);
        gst_buffer_unmap(fallback_buffer ? fallback_buffer.get() : input_buffer, &map_info);
        return injected;
    }
} // namespace

std::vector<uint8_t> maybe_inject_eoi_sei(const uint8_t *const frame_data, const size_t frame_length,
                                          const bool background_blur_enabled, const bool qp_map_delta_enabled,
                                          const int qp_map_background_delta, const bool luma_adaptation_enabled,
                                          const double luma_downscale_factor,
                                          const std::optional<double> luma_upscale_multiplier) {
    if (!frame_data || frame_length == 0) {
        return {};
    }

    if (!sf::contains_hevc_irap_vcl_nal(frame_data, frame_length)) {
        return {frame_data, frame_data + frame_length};
    }

    const bool inject_object_based = background_blur_enabled || qp_map_delta_enabled;
    const bool inject_luma_adaptation = luma_adaptation_enabled;
    if (!inject_object_based && !inject_luma_adaptation) {
        return {frame_data, frame_data + frame_length};
    }

    sf::encoder_optimization_info eoi_data{};
    eoi_data.cancel_flag = false;
    eoi_data.persistence_flag = true;
    eoi_data.for_human_viewing_idc = 0;
    eoi_data.for_machine_analysis_idc = 3;
    eoi_data.reserved_zero_2bits = 0;
    eoi_data.type = 0;

    if (inject_object_based) {
        eoi_data.type = static_cast<uint16_t>(eoi_data.type.value() | sf::encoder_optimization_info::type_object_based);
        if (qp_map_delta_enabled) {
            eoi_data.object_based_idc = 0x02;
            eoi_data.quant_threshold_delta = static_cast<unsigned int>(std::clamp(qp_map_background_delta, 0, 51));
            eoi_data.pic_quant_object_flag = true;
        } else {
            eoi_data.object_based_idc = 0x01;
        }
    }

    if (inject_luma_adaptation) {
        eoi_data.type =
                static_cast<uint16_t>(eoi_data.type.value() | sf::encoder_optimization_info::type_luma_adaptation);
        eoi_data.luma_adaptation_idc = 1U;
        const auto derived_signal = sf::derive_luma_adaptation_signal(luma_downscale_factor, luma_upscale_multiplier);
        eoi_data.ratio_luma_sign_flag = derived_signal.ratio_luma_sign_flag;
        eoi_data.ratio_luma_value = derived_signal.ratio_luma_value;
        eoi_data.backscale_ratio_value = derived_signal.backscale_ratio_value;
    }

    const auto eoi_payload = sf::write_encoder_optimization_info(eoi_data);
    const auto eoi_sei_nal_unit = sf::create_sei_nal_unit(215, eoi_payload);
    return sf::inject_sei_message(frame_data, frame_length, eoi_sei_nal_unit);
}

encoded_injection_bridge_core::transform_result
encoded_injection_bridge_core::transform_with_metadata(encoded_injection_transform_deps deps, GstBuffer *input_buffer,
                                                       const sf::packed_regions_info &region_info,
                                                       const encoded_frame_luma_adaptation_state &luma_state) {
    if (input_buffer == nullptr) {
        return {.status = step_status::fatal_stop,
                .count_sei_injection = false,
                .owned_buffer = nullptr,
                .use_input_buffer_ref = true};
    }

    bool map_ok = false;
    const auto injected_bytes = inject_pri_and_optional_eoi(deps, input_buffer, region_info, luma_state, map_ok);
    if (!map_ok) {
        return {.status = step_status::recoverable_drop,
                .count_sei_injection = false,
                .owned_buffer = nullptr,
                .use_input_buffer_ref = true};
    }

    auto owned = create_owned_buffer_from_bytes(injected_bytes, input_buffer);
    if (!owned) {
        return {.status = step_status::fatal_stop,
                .count_sei_injection = false,
                .owned_buffer = nullptr,
                .use_input_buffer_ref = true};
    }

    return {.status = step_status::ok,
            .count_sei_injection = true,
            .owned_buffer = std::move(owned),
            .use_input_buffer_ref = false};
}

encoded_injection_bridge_core::transform_result
encoded_injection_bridge_core::transform_without_metadata(encoded_injection_transform_deps deps,
                                                          GstBuffer *input_buffer,
                                                          const encoded_frame_luma_adaptation_state &luma_state) {
    if (input_buffer == nullptr) {
        return {.status = step_status::fatal_stop,
                .count_sei_injection = false,
                .owned_buffer = nullptr,
                .use_input_buffer_ref = true};
    }

    if (!deps.params.is_background_blur_effective() && !deps.params.is_qp_map_delta_effective() &&
        !luma_state.enabled) {
        return {.status = step_status::ok,
                .count_sei_injection = false,
                .owned_buffer = nullptr,
                .use_input_buffer_ref = true};
    }

    bool map_ok = false;
    const auto injected_bytes = inject_optional_eoi_only(deps, input_buffer, luma_state, map_ok);
    if (!map_ok) {
        return {.status = step_status::recoverable_drop,
                .count_sei_injection = false,
                .owned_buffer = nullptr,
                .use_input_buffer_ref = true};
    }

    if (injected_bytes.size() <= gst_buffer_get_size(input_buffer)) {
        return {.status = step_status::ok,
                .count_sei_injection = false,
                .owned_buffer = nullptr,
                .use_input_buffer_ref = true};
    }

    auto owned = create_owned_buffer_from_bytes(injected_bytes, input_buffer);
    if (!owned) {
        return {.status = step_status::fatal_stop,
                .count_sei_injection = false,
                .owned_buffer = nullptr,
                .use_input_buffer_ref = true};
    }

    return {.status = step_status::ok,
            .count_sei_injection = true,
            .owned_buffer = std::move(owned),
            .use_input_buffer_ref = false};
}
