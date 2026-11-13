// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "memory_contract_helper.h"

#include <string_view>

#include "../../libvcai/include/logging.h"

namespace {
    [[nodiscard]] bool is_non_system_memory_feature(const gchar *feature_name) {
        if (feature_name == nullptr) {
            return false;
        }
        constexpr std::string_view memory_prefix = "memory:";
        const std::string_view feature{feature_name};
        return feature.rfind(memory_prefix, 0) == 0 && feature != GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY;
    }
} // namespace

memory_domain_contract classify_caps_memory_domain(const GstCaps *caps) {
    if (caps == nullptr || gst_caps_get_size(caps) == 0) {
        return memory_domain_contract::unknown;
    }

    memory_domain_contract detected_domain = memory_domain_contract::system;

    const guint structure_count = gst_caps_get_size(caps);
    for (guint index = 0; index < structure_count; ++index) {
        const GstCapsFeatures *features = gst_caps_get_features(caps, index);
        if (features == nullptr || gst_caps_features_is_any(features)) {
            continue;
        }

        const guint feature_count = gst_caps_features_get_size(features);
        for (guint feature_index = 0; feature_index < feature_count; ++feature_index) {
            const gchar *feature_name = gst_caps_features_get_nth(features, feature_index);
            if (!is_non_system_memory_feature(feature_name)) {
                continue;
            }
            if (g_strcmp0(feature_name, "memory:NVMM") == 0) {
                return memory_domain_contract::nvmm;
            }
            if (g_strcmp0(feature_name, "memory:CUDAMemory") == 0) {
                return memory_domain_contract::cuda;
            }
            if (g_strcmp0(feature_name, "memory:DMABuf") == 0) {
                return memory_domain_contract::dmabuf;
            }
            detected_domain = memory_domain_contract::non_system_other;
        }
    }

    return detected_domain;
}

const char *memory_domain_to_string(const memory_domain_contract domain) {
    switch (domain) {
        case memory_domain_contract::unknown:
            return "unknown";
        case memory_domain_contract::system:
            return "system";
        case memory_domain_contract::nvmm:
            return "nvmm";
        case memory_domain_contract::cuda:
            return "cuda";
        case memory_domain_contract::dmabuf:
            return "dmabuf";
        case memory_domain_contract::non_system_other:
            return "non_system_other";
        default:
            return "unknown";
    }
}

sf::unique_gst_buffer_ptr create_system_memory_fallback_copy(GstBuffer *input_buffer, memory_policy_runtime &runtime,
                                                             GstDebugCategory *category, const char *stage) {
    if (input_buffer == nullptr) {
        runtime.record_fallback_failure();
        sf::error(category, "[%s] Cannot create memory fallback copy from null buffer", stage);
        return nullptr;
    }

    sf::unique_gst_buffer_ptr fallback_copy(gst_buffer_copy_deep(input_buffer));
    if (!fallback_copy) {
        runtime.record_fallback_failure();
        sf::error(category, "[%s] Failed to create deep-copy fallback buffer", stage);
        return nullptr;
    }

    runtime.record_memory_domain_fallback_conversion();
    sf::warning(category, "[%s] Applied memory-domain fallback conversion via deep-copy buffer", stage);
    return fallback_copy;
}

writable_probe_buffer_result ensure_writable_probe_buffer_with_fallback(GstPadProbeInfo *info, GstBuffer *input_buffer,
                                                                        memory_policy_runtime &runtime,
                                                                        GstDebugCategory *category, const char *stage) {
    if (input_buffer == nullptr) {
        runtime.record_fallback_failure();
        sf::error(category, "[%s] Probe buffer is null", stage);
        return {.status = memory_contract_status::fatal_stop, .buffer = nullptr};
    }

    if (gst_buffer_is_writable(input_buffer)) {
        return {.status = memory_contract_status::ok, .buffer = input_buffer};
    }

    GstBuffer *writable_buffer = gst_buffer_make_writable(input_buffer);
    if (writable_buffer == nullptr) {
        runtime.record_fallback_failure();
        sf::error(category, "[%s] Failed to make probe buffer writable", stage);
        return {.status = memory_contract_status::fatal_stop, .buffer = nullptr};
    }

    if (info != nullptr) {
        GST_PAD_PROBE_INFO_DATA(info) = writable_buffer;
    }
    runtime.record_writable_buffer_fallback_conversion();
    sf::warning(category, "[%s] Applied writable-buffer fallback conversion", stage);
    return {.status = memory_contract_status::fallback_applied, .buffer = writable_buffer};
}
