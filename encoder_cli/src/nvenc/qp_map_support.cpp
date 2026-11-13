// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "qp_map_support.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <ranges>
#include <string>

#include "../../libvcai/include/logging.h"
#include "../encoder_cli_debug.h"
#include "../roi/ctu_alignment.h"

namespace {
    [[nodiscard]] bool is_x86_architecture() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] std::string normalize_property_name(const char *name) {
        if (!name) {
            return {};
        }
        std::string normalized = name;
        std::ranges::transform(normalized, normalized.begin(),
                               [](const char ch) { return static_cast<char>(std::tolower(ch)); });
        return normalized;
    }

    [[nodiscard]] bool property_looks_like_qp_map_control(const std::string &name) {
        return name.find("qp-map") != std::string::npos || name.find("qp_map") != std::string::npos ||
               name.find("qpmap") != std::string::npos;
    }

    [[nodiscard]] bool property_looks_like_ctb_size_control(const std::string &name) {
        return name == "ctb-size" || name == "ctu-size" || name == "max-cu-size" || name == "max-ctu-size";
    }

    [[nodiscard]] int detect_ctb_size_from_properties(GObjectClass *klass, GstElement *encoder) {
        guint property_count = 0;
        GParamSpec **properties = g_object_class_list_properties(klass, &property_count);
        if (!properties) {
            return default_ctu_size;
        }

        int detected_size = default_ctu_size;
        for (guint index = 0; index < property_count; ++index) {
            const auto *property = properties[index];
            if (!property || !property->name) {
                continue;
            }
            const std::string normalized_name = normalize_property_name(property->name);
            if (!property_looks_like_ctb_size_control(normalized_name)) {
                continue;
            }
            if (!(property->flags & G_PARAM_READABLE)) {
                continue;
            }

            GValue value = G_VALUE_INIT;
            g_value_init(&value, G_PARAM_SPEC_VALUE_TYPE(property));
            g_object_get_property(G_OBJECT(encoder), property->name, &value);
            if (G_VALUE_HOLDS_INT(&value)) {
                const int raw_value = g_value_get_int(&value);
                if (raw_value > 0) {
                    detected_size = raw_value;
                    g_value_unset(&value);
                    break;
                }
            }
            if (G_VALUE_HOLDS_UINT(&value)) {
                const guint raw_value = g_value_get_uint(&value);
                if (raw_value > 0U && raw_value <= static_cast<guint>(std::numeric_limits<int>::max())) {
                    detected_size = static_cast<int>(raw_value);
                    g_value_unset(&value);
                    break;
                }
            }
            g_value_unset(&value);
        }

        g_free(properties);
        return detected_size;
    }
} // namespace

qp_map_support_probe_result probe_qp_map_delta_support(GstElement *encoder) {
    qp_map_support_probe_result result{};
    result.detected_ctb_size = default_ctu_size;

    if (!is_x86_architecture()) {
        result.reason = "qp_map_delta is currently enabled only on x86 desktop systems";
        return result;
    }

    if (!encoder) {
        result.reason = "encoder element is not initialized";
        return result;
    }

    GstElementFactory *factory = gst_element_get_factory(encoder);
    if (!factory) {
        result.reason = "encoder factory is unavailable";
        return result;
    }

    const char *factory_name = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory));
    const std::string encoder_name = factory_name ? factory_name : "";
    if (encoder_name != "nvh265enc") {
        result.reason = "active encoder is not nvh265enc";
        return result;
    }

    auto *klass = G_OBJECT_GET_CLASS(encoder);
    if (!klass) {
        result.reason = "encoder class metadata unavailable";
        return result;
    }

    guint property_count = 0;
    GParamSpec **properties = g_object_class_list_properties(klass, &property_count);
    if (!properties) {
        result.reason = "failed to inspect encoder properties";
        return result;
    }

    bool found_qp_map_property = false;
    for (guint index = 0; index < property_count; ++index) {
        const auto *property = properties[index];
        if (!property || !property->name) {
            continue;
        }

        const std::string normalized_name = normalize_property_name(property->name);
        if (property_looks_like_qp_map_control(normalized_name)) {
            found_qp_map_property = true;
            break;
        }
    }
    g_free(properties);

    if (!found_qp_map_property) {
        result.reason = "encoder does not expose QP-map controls";
        return result;
    }

    result.detected_ctb_size = detect_ctb_size_from_properties(klass, encoder);
    result.supported = true;
    result.reason.clear();
    return result;
}

void resolve_effective_background_mode(encoder_config &config, GstElement *encoder) {
    config.effective_background_mode_value = config.background_mode_value;
    config.background_mode_fallback_reason.clear();
    config.qp_map_ctb_size = default_ctu_size;

    if (!config.is_qp_map_delta_requested()) {
        return;
    }

    const auto probe_result = probe_qp_map_delta_support(encoder);
    if (probe_result.supported) {
        config.effective_background_mode_value = background_mode::qp_map_delta;
        config.qp_map_ctb_size = probe_result.detected_ctb_size > 0 ? probe_result.detected_ctb_size : default_ctu_size;
        sf::info(encoder_cli_main, "qp_map_delta enabled (ctb_size=%d)", config.qp_map_ctb_size);
        return;
    }

    config.effective_background_mode_value = background_mode::background_blur;
    config.background_mode_fallback_reason = probe_result.reason;
    config.qp_map_ctb_size = default_ctu_size;
    sf::warning(encoder_cli_main,
                "Requested qp_map_delta is unsupported, falling back to background_blur (reason: %s, ctb_size=%d)",
                config.background_mode_fallback_reason.c_str(), config.qp_map_ctb_size);
}
