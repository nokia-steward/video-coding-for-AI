// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "gpu_elements.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>

#include "../../libvcai/include/gst_raii.h"
#include "../../libvcai/include/logging.h"
#include "../encoder_config.h"

/**
 * @file gpu_elements.cpp
 * @brief GPU encoder/decoder element helpers for GStreamer pipelines.
 */

/// GStreamer debug category for GPU element helpers.
GST_DEBUG_CATEGORY_STATIC(gpu_elements_debug);
#define GST_CAT_DEFAULT gpu_elements_debug

void init_gpu_elements_debug_category() {
    GST_DEBUG_CATEGORY_INIT(gpu_elements_debug, "gpu-elements", 0, "GPU encoder/decoder element helpers");
}

namespace {

    /**
     * @brief Whether this build uses the ARM64/Jetson encoder path.
     */
    constexpr bool build_uses_arm64_encoder() {
#if defined(USE_ARM64_ENCODER) && USE_ARM64_ENCODER == 1
        return true;
#else
        return false;
#endif
    }

    // Registry: add new encoders here with caps_profile, gop_property, appsrc, and platform flags.
    // appsrc.format nullptr => input_pipeline uses data.params.format or NV12 default
    const encoder_descriptor encoder_registry[] = {
            {"nvh265enc", false, true, encoder_caps_profile::cuda, encoder_gop_property::gop_size, {nullptr, false}},
            {"nvv4l2h265enc",
             true,
             false,
             encoder_caps_profile::nvmm,
             encoder_gop_property::iframeinterval,
             {"I420", true}},
            // kvazaarenc: system-memory I420 only; plugin from lib/kvazaar (bootstrap.sh kvazaar)
            {"kvazaarenc",
             true,
             true,
             encoder_caps_profile::system,
             encoder_gop_property::intra_period,
             {"I420", false}},
    };

    [[nodiscard]] bool descriptor_supported_on_current_build(const encoder_descriptor &descriptor) {
        if (build_uses_arm64_encoder()) {
            return descriptor.supported_when_arm64_encoder;
        }
        return descriptor.supported_when_x86_encoder;
    }

    void apply_encoder_properties_jetson(GstElement *encoder, int bitrate_bps, std::optional<int> const_qp) {
        if (const_qp.has_value()) {
            const auto qp_val = const_qp.value();
            const auto qp_range = format_nvv4l2_qp_range(qp_val);
            auto *encoder_class = G_OBJECT_GET_CLASS(encoder);
            if (g_object_class_find_property(encoder_class, "qp-range") == nullptr) {
                throw std::runtime_error(
                        "nvv4l2h265enc has no qp-range property; cannot apply constant QP from config");
            }
            g_object_set(encoder, "preset-level", 1, "control-rate", 0, "qp-range", qp_range.c_str(), nullptr);
            sf::info(gpu_elements_debug, "Encoder configured for constant QP=%d (control-rate=0, qp-range=%s)", qp_val,
                     qp_range.c_str());
        } else {
            g_object_set(encoder, "preset-level", 1, "control-rate", 1, nullptr);
            if (bitrate_bps > 0) {
                g_object_set(encoder, "bitrate", bitrate_bps, nullptr);
            }
        }
    }

    /**
     * @brief kvazaarenc: bitrate in kbps; qp 0-51; preset medium (2) default per plugin.
     */
    void apply_encoder_properties_kvazaar(GstElement *encoder, int bitrate_bps, std::optional<int> const_qp) {
        // Preset: 0=ultrafast .. 4=veryslow; medium=2 matches plugin default
        g_object_set(encoder, "preset", 2, nullptr);
        if (const_qp.has_value()) {
            const int qp_val = std::clamp(const_qp.value(), 0, 51);
            g_object_set(encoder, "qp", qp_val, nullptr);
            sf::info(gpu_elements_debug, "kvazaarenc configured for constant QP=%d", qp_val);
        } else if (bitrate_bps > 0) {
            const int bitrate_kbps = std::clamp(bitrate_bps / 1000, 1, 800000);
            g_object_set(encoder, "bitrate", bitrate_kbps, nullptr);
        }
    }

    void apply_encoder_properties_desktop(GstElement *encoder, int bitrate_bps, std::optional<int> const_qp) {
        if (const_qp.has_value()) {
            const auto qp_val = const_qp.value();
            g_object_set(encoder, "preset", 1, "rc-mode", 1, "qp-const-i", qp_val, "qp-const-p", qp_val, "qp-const-b",
                         qp_val, nullptr);
            sf::info(gpu_elements_debug, "Encoder configured for constant QP=%d (rc-mode=constqp)", qp_val);
        } else {
            g_object_set(encoder, "preset", 1, "rc-mode", 2, nullptr);
            if (bitrate_bps > 0) {
                const int bitrate_kbps = bitrate_bps / 1000;
                const int clamped_bitrate_kbps = std::clamp(bitrate_kbps, 1, 2048000);
                g_object_set(encoder, "bitrate", clamped_bitrate_kbps, nullptr);
            }
        }
    }

} // namespace

const char *get_default_encoder_element_name() {
#if defined(USE_ARM64_ENCODER) && USE_ARM64_ENCODER == 1
    return "nvv4l2h265enc";
#else
    return "nvh265enc";
#endif
}

const char *get_encoder_element_name() { return get_default_encoder_element_name(); }

std::string resolve_encoder_element_factory_name(const encoder_config &config) {
    if (config.encoder_element.has_value()) {
        const std::string trimmed = [](std::string value) {
            const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
            value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
            return value;
        }(*config.encoder_element);
        if (!trimmed.empty()) {
            // User-friendly alias: config "kvazaar" -> factory kvazaarenc
            if (trimmed == "kvazaar" || trimmed == "Kvazaar") {
                return std::string("kvazaarenc");
            }
            return trimmed;
        }
    }
    return std::string(get_default_encoder_element_name());
}

const encoder_descriptor *find_encoder_descriptor(const std::string_view factory_name) {
    for (const auto &descriptor: encoder_registry) {
        if (factory_name == descriptor.factory_name) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool encoder_supported_on_current_build(const std::string_view factory_name) {
    const encoder_descriptor *descriptor = find_encoder_descriptor(factory_name);
    if (descriptor == nullptr) {
        return false;
    }
    return descriptor_supported_on_current_build(*descriptor);
}

void validate_encoder_element_or_throw(const encoder_config &config) {
    const std::string name = resolve_encoder_element_factory_name(config);
    const encoder_descriptor *descriptor = find_encoder_descriptor(name);
    if (descriptor == nullptr) {
        std::string supported;
        for (const auto &entry: encoder_registry) {
            if (!supported.empty()) {
                supported += ", ";
            }
            supported += entry.factory_name;
        }
        throw std::runtime_error("Unsupported encoder element '" + name + "'. Supported: " + supported);
    }
    if (!descriptor_supported_on_current_build(*descriptor)) {
        throw std::runtime_error("Encoder element '" + name + "' is not supported on this platform build.");
    }
    GstElementFactory *factory = gst_element_factory_find(name.c_str());
    if (factory == nullptr) {
        std::string hint = " Install the required GStreamer plugin and verify GST_PLUGIN_PATH.";
        if (name == "kvazaarenc") {
            hint = " Run ./scripts/setup/bootstrap.sh kvazaar so libgstkvazaarenc.so is under lib/kvazaar; "
                   "encoder_cli run scripts use it via isolated plugin dir.";
        }
        throw std::runtime_error("Encoder element plugin not found: '" + name + "'." + hint);
    }
    gst_object_unref(factory);
}

const char *get_decoder_element_name() {
#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
    return "nvv4l2decoder";
#else
    return "nvh265dec";
#endif
}

sf::unique_gst_element_ptr create_h265_encoder(const char *name, const std::string_view factory_name,
                                               const int bitrate_bps, const std::optional<int> const_qp) {
    const encoder_descriptor *descriptor = find_encoder_descriptor(factory_name);
    if (descriptor == nullptr) {
        throw std::runtime_error(std::string("Unsupported encoder element: ") + std::string(factory_name));
    }
    if (!descriptor_supported_on_current_build(*descriptor)) {
        throw std::runtime_error(std::string("Encoder element not supported on this build: ") +
                                 std::string(factory_name));
    }

    auto encoder = sf::unique_gst_element_ptr(gst_element_factory_make(std::string(factory_name).c_str(), name));
    if (!encoder) {
        const std::string msg = std::string("Failed to create GPU encoder: ") + std::string(factory_name) +
                                " - Ensure encoder plugin is installed";
        sf::error(gpu_elements_debug, "%s", msg.c_str());
        throw std::runtime_error(msg);
    }

    if (descriptor->gop_property == encoder_gop_property::iframeinterval) {
        apply_encoder_properties_jetson(encoder.get(), bitrate_bps, const_qp);
    } else if (descriptor->gop_property == encoder_gop_property::intra_period) {
        apply_encoder_properties_kvazaar(encoder.get(), bitrate_bps, const_qp);
    } else {
        apply_encoder_properties_desktop(encoder.get(), bitrate_bps, const_qp);
    }

    return encoder;
}

std::string format_nvv4l2_qp_range(const int qp) {
    const auto qp_text = std::to_string(qp);
    return qp_text + "," + qp_text + ":" + qp_text + "," + qp_text + ":" + qp_text + "," + qp_text;
}

sf::unique_gst_element_ptr create_h265_decoder(const char *name) {
    auto *decoder = gst_element_factory_make(get_decoder_element_name(), name);
    if (!decoder) {
        const std::string msg = std::string("Failed to create GPU decoder: ") + get_decoder_element_name() +
                                " - Ensure NVIDIA decoder plugin is installed and CUDA is working";
        sf::error(gpu_elements_debug, "%s", msg.c_str());
        throw std::runtime_error(msg);
    }

#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
    g_object_set(decoder, "enable-max-performance", TRUE, nullptr);
#endif

    return sf::unique_gst_element_ptr(decoder);
}

sf::unique_gst_element_ptr create_gpu_video_convert(const char *name) {
    auto *converter = gst_element_factory_make("nvvideoconvert", name);
    if (!converter) {
        const std::string msg = "Failed to create nvvideoconvert - ensure NVIDIA GStreamer plugins are installed";
        sf::error(gpu_elements_debug, "%s", msg.c_str());
        throw std::runtime_error(msg);
    }

    return sf::unique_gst_element_ptr(converter);
}

sf::unique_gst_element_ptr create_nvosd_element(const char *name) {
    auto *nvosd = gst_element_factory_make("nvdsosd", name);
    if (!nvosd) {
        sf::error(gpu_elements_debug, "Failed to create nvosd element - DeepStream may not be installed");
        return sf::unique_gst_element_ptr(nullptr);
    }

    auto *nvosd_class = G_OBJECT_GET_CLASS(nvosd);
    if (g_object_class_find_property(nvosd_class, "process-mode") != nullptr) {
        g_object_set(nvosd, "process-mode", 1, nullptr); // GPU mode
    }
    if (g_object_class_find_property(nvosd_class, "display-text") != nullptr) {
        g_object_set(nvosd, "display-text", TRUE, nullptr);
    }
    if (g_object_class_find_property(nvosd_class, "display-bbox") != nullptr) {
        g_object_set(nvosd, "display-bbox", TRUE, nullptr);
    }

    return sf::unique_gst_element_ptr(nvosd);
}

sf::unique_gst_caps_ptr create_nvmm_caps(const char *format) {
    if (!format || format[0] == '\0') {
        sf::warning(gpu_elements_debug, "Invalid NVMM caps format string");
        return sf::unique_gst_caps_ptr(nullptr);
    }

    const std::string caps_string = std::string("video/x-raw(memory:NVMM),format=") + format;
    return sf::unique_gst_caps_ptr(gst_caps_from_string(caps_string.c_str()));
}

sf::unique_gst_caps_ptr create_cuda_caps(const char *format) {
    if (!format || format[0] == '\0') {
        sf::warning(gpu_elements_debug, "Invalid CUDA caps format string");
        return sf::unique_gst_caps_ptr(nullptr);
    }

    const std::string caps_string = std::string("video/x-raw(memory:CUDA),format=") + format;
    return sf::unique_gst_caps_ptr(gst_caps_from_string(caps_string.c_str()));
}

sf::unique_gst_element_ptr create_cpu_to_gpu_upload(const char *name) {
    auto converter = create_gpu_video_convert(name);
    if (!converter) {
        return sf::unique_gst_element_ptr(nullptr);
    }

    g_object_set(converter.get(), "compute-hw", 1, nullptr);
    auto *converter_class = G_OBJECT_GET_CLASS(converter.get());
    if (g_object_class_find_property(converter_class, "nvbuf-memory-type") != nullptr) {
#if defined(USE_ARM64_ENCODER) && USE_ARM64_ENCODER == 1
        g_object_set(converter.get(), "nvbuf-memory-type", 4, nullptr);
#else
        g_object_set(converter.get(), "nvbuf-memory-type", 3, nullptr);
#endif
    } else {
        sf::warning(gpu_elements_debug, "nvvideoconvert missing nvbuf-memory-type property");
    }

    return converter;
}

sf::unique_gst_element_ptr create_gpu_to_cpu_download(const char *name) {
    auto converter = create_gpu_video_convert(name);
    if (!converter) {
        return sf::unique_gst_element_ptr(nullptr);
    }

    g_object_set(converter.get(), "compute-hw", 1, nullptr);
    auto *converter_class = G_OBJECT_GET_CLASS(converter.get());
    if (g_object_class_find_property(converter_class, "nvbuf-memory-type") != nullptr) {
        g_object_set(converter.get(), "nvbuf-memory-type", 0, nullptr);
    } else {
        sf::warning(gpu_elements_debug, "nvvideoconvert missing nvbuf-memory-type property");
    }

    return converter;
}

bool check_gpu_elements_available() {
    const auto encoder = create_h265_encoder("gpu-encoder-check", get_default_encoder_element_name(), 0);
    const auto decoder = create_h265_decoder("gpu-decoder-check");
    const auto converter = create_gpu_video_convert("gpu-convert-check");
    return encoder && decoder && converter;
}
