// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "gpu_elements.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include "../core/rtp_server_config.h"
#include "gst_raii.h"
#include "logging.h"

/**
 * @file gpu_elements.cpp
 * @brief GPU encoder/decoder element helpers for GStreamer pipelines.
 */

GST_DEBUG_CATEGORY_STATIC(gpu_elements_debug);
#define GST_CAT_DEFAULT gpu_elements_debug

void init_gpu_elements_debug_category() {
    GST_DEBUG_CATEGORY_INIT(gpu_elements_debug, "gpu-elements", 0, "GPU encoder/decoder element helpers");
}

namespace {

    // nvh265dec: NVMM output — downstream uses nvvideoconvert to download.
    // openhevcdec: software decoder — system memory; use videoconvert toward I420 for decoded_appsink.
    const char *const decoder_registry[] = {"nvh265dec", "openhevcdec"};

    [[nodiscard]] bool is_in_registry(const std::string_view factory_name) {
        for (const char *const name: decoder_registry) {
            if (factory_name == name) {
                return true;
            }
        }
        return false;
    }

} // namespace

const char *get_encoder_element_name() { return "nvh265enc"; }

const char *get_default_decoder_element_name() { return "nvh265dec"; }

std::string resolve_decoder_element_factory_name(const rtp_server_config &config) {
    if (config.decoder_element.has_value()) {
        std::string value = *config.decoder_element;
        const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
        if (!value.empty()) {
            return value;
        }
    }
    return std::string(get_default_decoder_element_name());
}

bool is_registered_decoder_element(const std::string_view factory_name) { return is_in_registry(factory_name); }

void validate_decoder_element_or_throw(const rtp_server_config &config) {
    const std::string name = resolve_decoder_element_factory_name(config);
    if (!is_in_registry(name)) {
        std::string supported;
        for (const char *const entry: decoder_registry) {
            if (!supported.empty()) {
                supported += ", ";
            }
            supported += entry;
        }
        throw std::runtime_error("Unsupported decoder element '" + name + "'. Supported: " + supported);
    }
    GstElementFactory *factory = gst_element_factory_find(name.c_str());
    if (factory == nullptr) {
        throw std::runtime_error("Decoder element plugin not found: '" + name +
                                 "'. Install the required GStreamer plugin and verify GST_PLUGIN_PATH.");
    }
    gst_object_unref(factory);
}

sf::unique_gst_element_ptr create_h265_encoder(const char *name, const int bitrate_bps) {
    auto *encoder = gst_element_factory_make(get_encoder_element_name(), name);
    if (!encoder) {
        const std::string msg = std::string("Failed to create GPU encoder: ") + get_encoder_element_name() +
                                " - Ensure NVIDIA encoder plugin (nvenc) is installed, CUDA is working, and the "
                                "GPU supports NVENC";
        sf::error(gpu_elements_debug, "%s", msg.c_str());
        throw std::runtime_error(msg);
    }

    g_object_set(encoder, "preset", 1, "rc-mode", 2, nullptr);

    if (bitrate_bps > 0) {
        const int bitrate_kbps = bitrate_bps / 1000;
        const int clamped_bitrate_kbps = std::clamp(bitrate_kbps, 1, 2048000);
        g_object_set(encoder, "bitrate", clamped_bitrate_kbps, nullptr);
    }

    return sf::unique_gst_element_ptr(encoder);
}

sf::unique_gst_element_ptr create_h265_decoder(const char *name, const std::string_view factory_name) {
    if (!is_in_registry(factory_name)) {
        throw std::runtime_error(std::string("Unsupported decoder element: ") + std::string(factory_name));
    }

    auto *decoder = gst_element_factory_make(std::string(factory_name).c_str(), name);
    if (!decoder) {
        std::string msg = std::string("Failed to create H.265 decoder: ") + std::string(factory_name) + " - ";
        if (factory_name == "nvh265dec") {
            msg += "Ensure NVIDIA decoder plugin is installed";
        } else if (factory_name == "openhevcdec") {
            msg += "Ensure gst-openhevc is installed and GST_PLUGIN_PATH includes the plugin";
        } else {
            msg += "Ensure the required GStreamer plugin is installed";
        }
        sf::error(gpu_elements_debug, "%s", msg.c_str());
        throw std::runtime_error(msg);
    }

    return sf::unique_gst_element_ptr(decoder);
}

sf::unique_gst_element_ptr create_gpu_video_convert(const char *name) {
    auto *converter = gst_element_factory_make("nvvideoconvert", name);
    if (!converter) {
        const std::string msg = "Failed to create nvvideoconvert - ensure NVIDIA GStreamer plugins are installed";
        sf::error(gpu_elements_debug, "%s", msg.c_str());
        throw std::runtime_error(msg);
    }

    g_object_set(converter, "compute-hw", 1, nullptr);
    auto *converter_class = G_OBJECT_GET_CLASS(converter);
    if (g_object_class_find_property(converter_class, "nvbuf-memory-type") != nullptr) {
        g_object_set(converter, "nvbuf-memory-type", 3, nullptr);
    } else {
        sf::warning(gpu_elements_debug, "nvvideoconvert missing nvbuf-memory-type property");
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
        g_object_set(nvosd, "process-mode", 1, nullptr);
    }
    if (g_object_class_find_property(nvosd_class, "display-text") != nullptr) {
        g_object_set(nvosd, "display-text", TRUE, nullptr);
    }
    if (g_object_class_find_property(nvosd_class, "display-bbox") != nullptr) {
        g_object_set(nvosd, "display-bbox", TRUE, nullptr);
    }

    return sf::unique_gst_element_ptr(nvosd);
}

sf::unique_gst_element_ptr create_gpu_to_cpu_download(const char *name) {
    auto converter = create_gpu_video_convert(name);
    if (!converter) {
        return sf::unique_gst_element_ptr(nullptr);
    }

    auto *converter_class = G_OBJECT_GET_CLASS(converter.get());
    if (g_object_class_find_property(converter_class, "nvbuf-memory-type") != nullptr) {
        g_object_set(converter.get(), "nvbuf-memory-type", 0, nullptr);
    } else {
        sf::warning(gpu_elements_debug, "nvvideoconvert missing nvbuf-memory-type property");
    }

    return converter;
}

bool decoder_outputs_system_memory(const std::string_view decoder_factory_name) {
    return decoder_factory_name == "openhevcdec";
}

sf::unique_gst_element_ptr create_decoded_postprocess_convert(const char *name,
                                                              const std::string_view decoder_factory_name) {
    if (decoder_outputs_system_memory(decoder_factory_name)) {
        auto *converter = gst_element_factory_make("videoconvert", name);
        if (!converter) {
            const std::string msg =
                    "Failed to create videoconvert after software decoder — ensure gst-plugins-base is available";
            sf::error(gpu_elements_debug, "%s", msg.c_str());
            throw std::runtime_error(msg);
        }
        return sf::unique_gst_element_ptr(converter);
    }
    return create_gpu_to_cpu_download(name);
}

bool check_gpu_elements_available(const rtp_server_config *config) {
    try {
        const auto encoder = create_h265_encoder("gpu-encoder-check", 0);
        const std::string decoder_factory_name = config != nullptr ? resolve_decoder_element_factory_name(*config)
                                                                   : std::string(get_default_decoder_element_name());
        const auto decoder = create_h265_decoder("gpu-decoder-check", decoder_factory_name);
        const auto postprocess_convert =
                create_decoded_postprocess_convert("gpu-decoded-convert-check", decoder_factory_name);
        if (!encoder || !decoder || !postprocess_convert) {
            return false;
        }
        // NVMM decoders feed display branch via nvvideoconvert; software decoders use videoconvert in processing only.
        if (!decoder_outputs_system_memory(decoder_factory_name)) {
            const auto gpu_convert = create_gpu_video_convert("gpu-convert-check");
            if (!gpu_convert) {
                return false;
            }
        } else {
            try {
                const auto gpu_convert = create_gpu_video_convert("gpu-convert-check");
                (void) gpu_convert;
            } catch (const std::exception &error) {
                sf::warning(gpu_elements_debug,
                            "nvvideoconvert unavailable (%s); display branch may fail if it requires NVMM",
                            error.what());
            }
        }
        return true;
    } catch (const std::exception &error) {
        sf::error(gpu_elements_debug, "Pipeline element availability check failed: %s", error.what());
        return false;
    }
}

bool check_gpu_elements_available() { return check_gpu_elements_available(nullptr); }
