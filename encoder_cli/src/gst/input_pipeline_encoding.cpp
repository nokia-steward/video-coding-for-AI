// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file input_pipeline_encoding.cpp
 * @brief Encoding pipeline construction (encoding appsrc branch); split from input_pipeline.cpp.
 */
#include <algorithm>
#include <cstdint>
#include <glib.h>
#include <gst/gst.h>
#include <optional>
#include <string_view>
#include "../../libvcai/include/area_copy.h"
#include "../../libvcai/include/logging.h"
#include "../bridge/orchestration/encoded_bridge_startup_contract.h"
#include "../encoder_backend/encoder_backend.h"
#include "../encoder_enums.h"
#include "../nvenc/qp_map_support.h"
#include "../processing/memory_contract_helper.h"
#include "../reporting/performance_monitor.h"
#include "gpu_elements.h"
#include "gst_callbacks.h"
#include "input_pipeline.h"
#include "input_pipeline_debug.h"
#include "input_types/rtp_timestamp_overlay.h"

#include "../input_analyzer/input_analyzer.h"

namespace {
    /// Machine-readable contract schema consumed by TET summary gating.
    constexpr std::string_view encoder_backend_contract_schema = "encoder_backend_contract/v1";

    /// Bits per pixel for high-quality H.265 encoding.
    constexpr double h265_bpp_high_quality = 0.12;
    /// Bits per pixel for medium-quality H.265 encoding.
    constexpr double h265_bpp_medium_quality = 0.08;
    /// Bits per pixel for low-quality H.265 encoding.
    constexpr double h265_bpp_low_quality = 0.05;

    /// Default BPP target used when no override is provided.
    constexpr double h265_default_bpp = h265_bpp_medium_quality;

    /// Minimum allowed bitrate in bits per second.
    constexpr int min_bitrate_bps = 500'000;
    /// Maximum allowed bitrate in bits per second.
    constexpr int max_bitrate_bps = 50'000'000;

    /**
     * @brief Calculates a default encoder bitrate in bits per second.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param framerate Frame rate in frames per second.
     * @return Target bitrate in bits per second.
     */
    [[nodiscard]] int calculate_default_bitrate_bps(const int width, const int height, const int framerate) {
        if (width <= 0 || height <= 0 || framerate <= 0) {
            sf::warning(input_pipeline, "Invalid dimensions for bitrate calculation: %dx%d@%dfps", width, height,
                        framerate);
            return min_bitrate_bps;
        }

        const int64_t pixels_per_second = static_cast<int64_t>(width) * height * framerate;
        const double raw_bitrate = static_cast<double>(pixels_per_second) * h265_default_bpp;
        const int bitrate_bps = static_cast<int>(
                std::clamp(raw_bitrate, static_cast<double>(min_bitrate_bps), static_cast<double>(max_bitrate_bps)));

        sf::info(input_pipeline, "Calculated bitrate for %dx%d@%dfps: %d bps (%.2f Mbps)", width, height, framerate,
                 bitrate_bps, static_cast<double>(bitrate_bps) / 1'000'000.0);

        return bitrate_bps;
    }

    /**
     * @brief Escapes a string for safe JSON embedding.
     * @param value Input text.
     * @return JSON-escaped text.
     */
    [[nodiscard]] std::string escape_json_string(const std::string_view value) {
        static constexpr char hex_digits[] = "0123456789ABCDEF";
        std::string escaped;
        escaped.reserve(value.size() + 16);
        for (const char raw_char: value) {
            const unsigned char c = static_cast<unsigned char>(raw_char);
            switch (c) {
                case '\"':
                    escaped += "\\\"";
                    break;
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\b':
                    escaped += "\\b";
                    break;
                case '\f':
                    escaped += "\\f";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    if (c < 0x20U) {
                        escaped += "\\u00";
                        escaped.push_back(hex_digits[(c >> 4U) & 0x0FU]);
                        escaped.push_back(hex_digits[c & 0x0FU]);
                    } else {
                        escaped.push_back(static_cast<char>(c));
                    }
                    break;
            }
        }
        return escaped;
    }

    /**
     * @brief Emits a stable backend contract line for test automation.
     * @param data Global runtime state.
     *
     * This CONTRACT_JSON payload is consumed by
     * scripts/tet_comparison_framework/lib/run_tet_comparison_main.sh to gate
     * qp_map_delta summary inclusion. Keep keys backward compatible.
     */
    void emit_encoder_backend_contract(const global_data &data) {
        const std::string requested_mode = background_mode_to_string(data.params.background_mode_value);
        const std::string effective_mode = background_mode_to_string(data.params.effective_background_mode_value);
        const std::string fallback_reason = data.params.background_mode_fallback_reason;

        std::string contract_line;
        contract_line.reserve(384U + fallback_reason.size());
        contract_line += "CONTRACT_JSON:{\"schema\":\"";
        contract_line += std::string(encoder_backend_contract_schema);
        contract_line += "\",\"requested_background_mode\":\"";
        contract_line += escape_json_string(requested_mode);
        contract_line += "\",\"effective_background_mode\":\"";
        contract_line += escape_json_string(effective_mode);
        contract_line += "\",\"direct_qp_map_backend\":";
        contract_line += data.backend_supports_direct_qp_map ? "true" : "false";
        contract_line += ",\"qp_map_requested\":";
        contract_line += data.params.is_qp_map_delta_requested() ? "true" : "false";
        contract_line += ",\"qp_map_effective\":";
        contract_line += data.params.is_qp_map_delta_effective() ? "true" : "false";
        contract_line += ",\"fallback_reason\":\"";
        contract_line += escape_json_string(fallback_reason);
        contract_line += "\"}";

        sf::info(input_pipeline, "%s", contract_line.c_str());
    }
} // namespace

/**
 * @brief Builds the encoding pipeline and connects appsrc/appsink.
 * @param data Global data structure to populate with elements.
 * @return True if the pipeline was created successfully.
 */
bool create_encoding_pipeline(global_data &data) {
    constexpr int direct_nvenc_ctb_size = 32;

    data.active_encoder_backend.reset();
    data.backend_supports_direct_qp_map = false;
    data.pending_qp_map_payload.reset();

    const bool qp_map_direct_requested = data.params.is_qp_map_delta_requested();
    if (qp_map_direct_requested) {
        auto direct_backend_result = try_create_nvenc_api_backend(data.params);
        if (direct_backend_result.backend && direct_backend_result.qp_map_direct_supported) {
            data.active_encoder_backend = std::move(direct_backend_result.backend);
            data.backend_supports_direct_qp_map = true;
            data.params.effective_background_mode_value = background_mode::qp_map_delta;
            data.params.qp_map_ctb_size = direct_nvenc_ctb_size;
            data.params.background_mode_fallback_reason.clear();
            sf::info(input_pipeline, "Using direct NvEncodeAPI backend for qp_map_delta mode (ctb_size=%d)",
                     data.params.qp_map_ctb_size);
            sf::info(input_pipeline, "qp_map_delta config: ctb_size=%d, background_delta=%d",
                     data.params.qp_map_ctb_size, data.params.qp_map_background_delta);

            if (data.params.enable_adaptive_luma_adaptation) {
                if (!input_analyzer_built_with_onnx_runtime()) {
                    sf::error(input_pipeline,
                              "Adaptive luma is enabled but encoder_cli was built without ONNX Runtime. "
                              "Install ONNX Runtime (see docs/part03/install-onnx-runtime.md), set "
                              "CMAKE_PREFIX_PATH, and rebuild encoder_cli to use the adaptive luma input analyzer.");
                    return false;
                }
                auto analyzer = input_analyzer_create();
                if (!input_analyzer_init(analyzer.get(), data.params.input_analyzer_model_path)) {
                    sf::error(input_pipeline, "Adaptive luma input analyzer failed to load: %s. Exiting.",
                              data.params.input_analyzer_model_path.c_str());
                    return false;
                }
                data.adaptive_luma_analyzer = std::move(analyzer);
                sf::info(input_pipeline, "Adaptive luma enabled: input analyzer loaded (model: %s)",
                         data.params.input_analyzer_model_path.c_str());
            }

            emit_encoder_backend_contract(data);
            if (data.monitoring.status_reporter) {
                data.monitoring.status_reporter->report_pipeline_status("encoding", "created successfully");
            }
            return true;
        }

        data.params.effective_background_mode_value = background_mode::background_blur;
        data.params.background_mode_fallback_reason = direct_backend_result.reason.empty()
                                                              ? "Direct NvEncodeAPI backend unavailable"
                                                              : direct_backend_result.reason;
        data.params.qp_map_ctb_size = default_ctu_size;
        sf::warning(input_pipeline,
                    "Requested qp_map_delta is unsupported by direct backend, falling back to background_blur "
                    "(reason: %s, ctb_size=%d)",
                    data.params.background_mode_fallback_reason.c_str(), data.params.qp_map_ctb_size);
        sf::info(input_pipeline,
                 "qp_map_delta requested values: ctb_size=%d, background_delta=%d "
                 "(effective_mode=%s; direct backend not active, values are not applied)",
                 data.params.qp_map_ctb_size, data.params.qp_map_background_delta,
                 background_mode_to_string(data.params.effective_background_mode_value));
    }

    const char *format_str = data.params.format.has_value() ? sf::video_format_to_string(*data.params.format) : "NV12";
    const std::string encoder_factory_name = resolve_encoder_element_factory_name(data.params);
    const encoder_descriptor *const enc_desc = find_encoder_descriptor(encoder_factory_name);
    if (enc_desc == nullptr) {
        sf::error(input_pipeline, "Unsupported encoder element: %s", encoder_factory_name.c_str());
        return false;
    }
    const bool use_nvmm_memory = (enc_desc->caps_profile == encoder_caps_profile::nvmm);
    const bool use_system_memory = (enc_desc->caps_profile == encoder_caps_profile::system);
    const char *encoder_format = use_system_memory ? "I420"
                                 : (enc_desc->caps_profile == encoder_caps_profile::cuda || use_nvmm_memory)
                                         ? "NV12"
                                         : format_str;
    sf::debug(input_pipeline, "Creating encoding pipeline with format='%s', width=%d, height=%d, framerate=%d",
              format_str, data.params.width, data.params.height, data.params.framerate);

    data.encoding_pipeline.reset(gst_pipeline_new("encoding-pipeline"));
    if (!data.encoding_pipeline.get()) {
        sf::error(input_pipeline, "Encoding pipeline could not be created. GStreamer may not be properly initialized.");
        return false;
    }

    data.encoded_ports.encoding_src = gst_element_factory_make("appsrc", "encoding-src");
    if (!data.encoded_ports.encoding_src) {
        sf::error(input_pipeline, "Failed to create appsrc element. gstreamer-app plugin may not be available.");
        return false;
    }

    const int bitrate_bps = calculate_default_bitrate_bps(data.params.width, data.params.height, data.params.framerate);
    sf::unique_gst_element_ptr encoder_owner;
    try {
        encoder_owner = create_h265_encoder("h265-encoder", encoder_factory_name, bitrate_bps, data.params.encoder_qp);
        data.encoder = encoder_owner.get();
    } catch (const std::runtime_error &exception) {
        sf::error(input_pipeline, "Failed to create encoder '%s': %s", encoder_factory_name.c_str(), exception.what());
        return false;
    }
    if (!data.encoder) {
        sf::error(input_pipeline, "Failed to create %s element. Plugin may not be installed.",
                  encoder_factory_name.c_str());
        return false;
    }

    if (!qp_map_direct_requested) {
        resolve_effective_background_mode(data.params, data.encoder);
    }
    emit_encoder_backend_contract(data);

    // Log the encoder rate control mode for visibility
    if (data.params.encoder_qp.has_value()) {
        sf::info(input_pipeline, "Encoder configured for constant QP=%d", data.params.encoder_qp.value());
    } else {
        sf::info(input_pipeline, "Encoder configured for CBR at %d bps", bitrate_bps);
    }

    // Configure encoder GOP based on packer_type to match ROI packing mode
    // Low delay mode: use lp-gop (low delay GOP) for minimal latency
    // Random access mode: use random access GOP for better compression and random access points
    const int gop_size =
            (data.params.packer_type_value == packer_type::random_access) ? get_intra_period(data.params) : 4;
    const int effective_gop_size = get_encoder_keyframe_period(data.params);
    if (enc_desc->gop_property == encoder_gop_property::intra_period && gop_size > 0 &&
        effective_gop_size != gop_size) {
        constexpr int kvm_b_gop_alignment = 16;
        sf::info(input_pipeline, "Adjusted kvazaar intra-period from %d to %d (must be a multiple of B-GOP length %d)",
                 gop_size, effective_gop_size, kvm_b_gop_alignment);
    }
    if (enc_desc->gop_property == encoder_gop_property::iframeinterval) {
        g_object_set(data.encoder, "iframeinterval", effective_gop_size, nullptr);
    } else if (enc_desc->gop_property == encoder_gop_property::intra_period) {
        g_object_set(data.encoder, "intra-period", effective_gop_size, nullptr);
    } else {
        g_object_set(data.encoder, "gop-size", effective_gop_size, nullptr);
    }
    if (data.params.packer_type_value == packer_type::random_access) {
        sf::info(input_pipeline, "Configured encoder for random_access mode: GOP size=%d", effective_gop_size);
    } else if (data.params.packer_type_value == packer_type::low_delay) {
        sf::info(input_pipeline, "Configured encoder for low_delay mode: GOP size=%d", effective_gop_size);
    } else {
        sf::warning(input_pipeline, "Unknown packer_type, defaulting to low_delay GOP size=%d", effective_gop_size);
    }

    sf::unique_gst_element_ptr upload_converter_owner;
    GstElement *upload_converter = nullptr;
    GstElement *format_converter = nullptr;
    if (!use_system_memory) {
        upload_converter_owner = create_cpu_to_gpu_upload("encoding-upload");
        upload_converter = upload_converter_owner.get();
        if (!upload_converter) {
            sf::error(input_pipeline, "Failed to create GPU upload converter for encoding pipeline.");
            return false;
        }
    }
    auto *upload_capsfilter = gst_element_factory_make("capsfilter", "encoding-upload-caps");
    if (!upload_capsfilter) {
        sf::error(input_pipeline, "Failed to create capsfilter for encoding upload pipeline.");
        return false;
    }

    auto *appsink = gst_element_factory_make("appsink", "encoding-sink");
    if (!appsink) {
        sf::error(input_pipeline, "Failed to create appsink element. gstreamer-app plugin may not be available.");
        return false;
    }

    // Appsrc format from encoder descriptor (Jetson path uses I420; desktop uses config or NV12)
    const char *appsrc_format = enc_desc->appsrc.format;
    if (appsrc_format == nullptr || appsrc_format[0] == '\0') {
        appsrc_format = data.params.format.has_value() ? sf::video_format_to_string(*data.params.format) : "NV12";
    }
    const sf::unique_gst_caps_ptr caps(gst_caps_new_simple(
            "video/x-raw", "format", G_TYPE_STRING, appsrc_format, "width", G_TYPE_INT, data.params.width, "height",
            G_TYPE_INT, data.params.height, "framerate", GST_TYPE_FRACTION, data.params.framerate, 1, "colorimetry",
            G_TYPE_STRING, "bt709", nullptr));

    sf::unique_gst_caps_ptr upload_caps;
    if (use_system_memory) {
        // kvazaarenc sink accepts system-memory I420 only
        upload_caps.reset(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", "width", G_TYPE_INT,
                                              data.params.width, "height", G_TYPE_INT, data.params.height, "framerate",
                                              GST_TYPE_FRACTION, data.params.framerate, 1, "colorimetry", G_TYPE_STRING,
                                              "bt709", nullptr));
        if (!upload_caps) {
            sf::error(input_pipeline, "Failed to create system-memory I420 caps for kvazaarenc.");
            return false;
        }
        if (appsrc_format != nullptr && std::string_view(appsrc_format) != "I420") {
            format_converter = gst_element_factory_make("videoconvert", "encoding-format-convert");
            if (!format_converter) {
                sf::error(input_pipeline, "Failed to create videoconvert for kvazaarenc I420 path.");
                return false;
            }
        }
    } else if (use_nvmm_memory) {
        // For ARM64/Jetson: nvv4l2h265enc requires NVMM memory caps
        upload_caps = create_nvmm_caps(encoder_format);
        if (!upload_caps) {
            sf::error(input_pipeline, "Failed to create NVMM caps for encoding upload pipeline.");
            return false;
        }
        gst_caps_set_simple(upload_caps.get(), "width", G_TYPE_INT, data.params.width, "height", G_TYPE_INT,
                            data.params.height, "framerate", GST_TYPE_FRACTION, data.params.framerate, 1, "colorimetry",
                            G_TYPE_STRING, "bt709", nullptr);
    } else {
        // For x86_64: nvh265enc uses CUDA memory caps
        upload_caps.reset(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, encoder_format, "width",
                                              G_TYPE_INT, data.params.width, "height", G_TYPE_INT, data.params.height,
                                              "framerate", GST_TYPE_FRACTION, data.params.framerate, 1, "colorimetry",
                                              G_TYPE_STRING, "bt709", nullptr));
        if (!upload_caps) {
            sf::error(input_pipeline, "Failed to create upload caps for encoding pipeline.");
            return false;
        }
    }
    g_object_set(upload_capsfilter, "caps", upload_caps.get(), nullptr);
    const auto upload_memory_domain = classify_caps_memory_domain(upload_caps.get());
    data.memory_runtime.encoding_upload_expected_domain.store(upload_memory_domain);
    sf::info(input_pipeline, "Memory contract: encoding upload domain set to '%s'",
             memory_domain_to_string(upload_memory_domain));

    // Configure and lock encoding appsrc startup contract once per runtime instance.
    const encoded_bridge_startup_contract_options startup_options{
            .is_live = false,
            .do_timestamp = false,
            .block = enc_desc->appsrc.block,
            .max_bytes = std::nullopt,
    };
    if (!configure_encoded_bridge_startup_contract(data.encoded_ports, data.egress_bridge_runtime,
                                                   encoded_bridge_target::encoding_src, caps.get(), startup_options)) {
        sf::error(input_pipeline, "Failed to establish startup contract for encoding_src");
        return false;
    }
    // sync=false reduces latency and prevents frame drops in live streaming scenarios
    // Increased buffers to 4 for better smoothing
    g_object_set(appsink, "emit-signals", TRUE, "max-buffers", 4, "sync", false, nullptr);

    g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_encoded_sample), &data);
    g_signal_connect(appsink, "eos", G_CALLBACK(on_encoded_eos), &data);

    bool success = false;
    if (use_system_memory) {
        if (format_converter) {
            gst_bin_add_many(GST_BIN(data.encoding_pipeline.get()), data.encoded_ports.encoding_src, format_converter,
                             upload_capsfilter, data.encoder, appsink, nullptr);
            // RAII_EXEMPT: gst_bin_add_many takes ownership of GstElement refs.
            encoder_owner.release();
            success = gst_element_link_many(data.encoded_ports.encoding_src, format_converter, upload_capsfilter,
                                            data.encoder, appsink, nullptr);
        } else {
            gst_bin_add_many(GST_BIN(data.encoding_pipeline.get()), data.encoded_ports.encoding_src, upload_capsfilter,
                             data.encoder, appsink, nullptr);
            // RAII_EXEMPT: gst_bin_add_many takes ownership of GstElement refs.
            encoder_owner.release();
            success = gst_element_link_many(data.encoded_ports.encoding_src, upload_capsfilter, data.encoder, appsink,
                                            nullptr);
        }
    } else {
        gst_bin_add_many(GST_BIN(data.encoding_pipeline.get()), data.encoded_ports.encoding_src, upload_converter,
                         upload_capsfilter, data.encoder, appsink, nullptr);
        // RAII_EXEMPT: gst_bin_add_many takes ownership of GstElement refs.
        upload_converter_owner.release();
        // RAII_EXEMPT: gst_bin_add_many takes ownership of GstElement refs.
        encoder_owner.release();
        success = gst_element_link_many(data.encoded_ports.encoding_src, upload_converter, upload_capsfilter,
                                        data.encoder, appsink, nullptr);
    }

    if (!success) {
        sf::error(input_pipeline, "Failed to link encoding pipeline elements: appsrc -> %s -> appsink",
                  encoder_factory_name.c_str());
    }

    // Add pad probes for encoding stage monitoring
    if (success && data.monitoring.perf_monitor) {
        // Add probe on encoder sink pad (start timing)
        GstPad *encoder_sink = gst_element_get_static_pad(data.encoder, "sink");
        if (encoder_sink) {
            gst_pad_add_probe(
                    encoder_sink, GST_PAD_PROBE_TYPE_BUFFER,
                    []([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
                            auto *global_data_ptr = static_cast<global_data *>(user_data);
                            if (global_data_ptr && global_data_ptr->monitoring.perf_monitor) {
                                GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                                if (buffer) {
                                    const GstClockTime pts = GST_BUFFER_PTS(buffer);
                                    if (pts != GST_CLOCK_TIME_NONE) {
                                        global_data_ptr->monitoring.perf_monitor->record_start_time(
                                                monitoring_stage::encoding, pts);
                                    }
                                }
                            }
                        }
                        return GST_PAD_PROBE_OK;
                    },
                    &data, nullptr);
            gst_object_unref(encoder_sink);
        }

        // Add probe on encoder src pad (end timing)
        GstPad *encoder_src = gst_element_get_static_pad(data.encoder, "src");
        if (encoder_src) {
            gst_pad_add_probe(
                    encoder_src, GST_PAD_PROBE_TYPE_BUFFER,
                    []([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
                            auto *global_data_ptr = static_cast<global_data *>(user_data);
                            if (global_data_ptr && global_data_ptr->monitoring.perf_monitor) {
                                GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                                if (buffer) {
                                    const GstClockTime pts = GST_BUFFER_PTS(buffer);
                                    if (pts != GST_CLOCK_TIME_NONE) {
                                        global_data_ptr->monitoring.perf_monitor->record_end_time(
                                                monitoring_stage::encoding, pts);
                                    }
                                }
                            }
                        }
                        return GST_PAD_PROBE_OK;
                    },
                    &data, nullptr);
            gst_object_unref(encoder_src);
        }
    }

    if (data.monitoring.status_reporter) {
        data.monitoring.status_reporter->report_pipeline_status("encoding",
                                                                success ? "created successfully" : "creation failed");
    }

    // Setup RTP timestamp overlay on appsrc output if enabled
    if (success && data.params.debug.draw_rtp_timestamp) {
        if (!setup_rtp_timestamp_overlay(data.encoded_ports.encoding_src, data.params)) {
            sf::warning(input_pipeline, "Failed to setup RTP timestamp overlay, continuing without it");
        }
    }

    if (success) {
        data.active_encoder_backend = create_gst_encoder_backend();
        if (!data.active_encoder_backend) {
            sf::error(input_pipeline, "Failed to initialize GStreamer encoder backend");
            return false;
        }

        if (data.params.enable_adaptive_luma_adaptation) {
            if (!input_analyzer_built_with_onnx_runtime()) {
                sf::error(input_pipeline,
                          "Adaptive luma is enabled but encoder_cli was built without ONNX Runtime. "
                          "Install ONNX Runtime (see docs/part03/install-onnx-runtime.md), set "
                          "CMAKE_PREFIX_PATH, and rebuild encoder_cli to use the adaptive luma input analyzer.");
                return false;
            }
            auto analyzer = input_analyzer_create();
            if (!input_analyzer_init(analyzer.get(), data.params.input_analyzer_model_path)) {
                sf::error(input_pipeline, "Adaptive luma input analyzer failed to load: %s. Exiting.",
                          data.params.input_analyzer_model_path.c_str());
                return false;
            }
            data.adaptive_luma_analyzer = std::move(analyzer);
            sf::info(input_pipeline, "Adaptive luma enabled: input analyzer loaded (model: %s)",
                     data.params.input_analyzer_model_path.c_str());
        }
    }

    return success;
}
