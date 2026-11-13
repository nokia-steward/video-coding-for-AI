// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "udp_input.h"
#include "../../global_data.h"
#include "../gpu_elements.h"
#include "frame_drop_manager.h"
#include "gst_raii.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <gst/gst.h>
#include <sstream>
#include <string>
#include <string_view>
#include "../../../libvcai/include/logging.h"

/**
 * @file udp_input.cpp
 * @brief Implementation of UDP input pipeline creation and management.
 */

/// GStreamer debug category for UDP input handling.
GST_DEBUG_CATEGORY_STATIC(udp_input);

namespace {
    /**
     * @brief Converts a string to uppercase.
     * @param value The string to convert.
     * @return A new string with all characters converted to uppercase.
     */
    std::string to_upper_copy(const std::string &value) {
        std::string result = value;
        std::ranges::transform(result, result.begin(),
                               [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return result;
    }

    /**
     * @brief Pad probe callback to detect UDP packet reception.
     *
     * This callback is invoked by GStreamer for each buffer passing through the UDP source pad.
     * It tracks packet reception times, logs connection events, and tracks bitrate.
     *
     * @param pad The pad that received the buffer (unused).
     * @param info The pad probe info containing the buffer.
     * @param user_data Pointer to the concrete aggregate (escape hatch via the facade's instance()); the callback
     * reads mutable per-instance UDP counters, which GStreamer requires through a stable raw pointer.
     * @return GST_PAD_PROBE_OK to continue processing.
     */
    GstPadProbeReturn udp_packet_probe([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) {
        if ((GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) == 0) {
            return GST_PAD_PROBE_OK;
        }
        if (user_data == nullptr) {
            return GST_PAD_PROBE_OK;
        }

        auto &data = *static_cast<global_data *>(user_data);
        auto &ur = data.udp_input_runtime;

        const auto now = std::chrono::steady_clock::now();
        const auto now_rep = now.time_since_epoch().count();
        ur.last_packet_time_rep.store(now_rep);

        const auto buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        GstMapInfo map_info;
        gsize buffer_size = 0;

        if (gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
            buffer_size = map_info.size;
            gst_buffer_unmap(buffer, &map_info);
        }

        ur.udp_packet_count.fetch_add(1);

        if (buffer_size > 0) {
            ur.udp_bytes_received.fetch_add(buffer_size);
            ur.udp_total_packet_sizes.fetch_add(buffer_size);

            data.udp_bitrate_tracker.add_buffer_sample(buffer, buffer_size);

            guint64 current_min = ur.udp_min_packet_size.load();
            while (buffer_size < current_min || current_min == 0) {
                if (ur.udp_min_packet_size.compare_exchange_weak(current_min, buffer_size)) {
                    break;
                }
                current_min = ur.udp_min_packet_size.load();
            }

            guint64 current_max = ur.udp_max_packet_size.load();
            while (buffer_size > current_max) {
                if (ur.udp_max_packet_size.compare_exchange_weak(current_max, buffer_size)) {
                    break;
                }
                current_max = ur.udp_max_packet_size.load();
            }
        }

        const bool was_first = ur.first_udp_packet_received.exchange(true);
        if (!was_first) {
            sf::info(udp_input, "First UDP packet received! Size: %zu bytes", buffer_size);
        }

        const guint64 count = ur.udp_packet_count.load();
        if (count % 100 == 0) {
            sf::info(udp_input, "UDP packets received: %llu, total bytes: %llu", static_cast<unsigned long long>(count),
                     static_cast<unsigned long long>(ur.udp_bytes_received.load()));
        }

        return GST_PAD_PROBE_OK;
    }

    /**
     * @brief Configures the UDP source element with port, address, and capabilities.
     *
     * GStreamer's g_object_set requires a non-const GstElement pointer, so this function
     * cannot take udpsrc as const even though it only configures properties.
     *
     * @param config The encoder configuration (UDP parameters).
     * @param udpsrc The UDP source element to configure (non-const due to GStreamer API requirements).
     * @return true if configuration was successful, false otherwise.
     */
    bool configure_udpsrc(const encoder_config &config, GstElement *udpsrc) {
        if (!udpsrc) {
            return false;
        }

        g_object_set(udpsrc, "port", static_cast<gint>(config.udp.resolved_port()), nullptr);
        const std::string bind_address = config.udp.resolved_bind_address();
        if (!bind_address.empty()) {
            g_object_set(udpsrc, "address", bind_address.c_str(), nullptr);
        }

        const std::string encoding_upper = to_upper_copy(config.udp.resolved_encoding());
        // Create flexible caps without fixing payload type to allow negotiation from actual RTP stream
        // Payload type will be negotiated automatically from incoming RTP packets
        std::ostringstream caps_stream;
        caps_stream << "application/x-rtp, media=(string)video, encoding-name=(string)" << encoding_upper
                    << ", clock-rate=(int)" << config.udp.resolved_clock_rate();

        const sf::unique_gst_caps_ptr caps(gst_caps_from_string(caps_stream.str().c_str()));
        if (!caps) {
            sf::error(udp_input, "Failed to create caps for UDP source with encoding %s", encoding_upper.c_str());
            return false;
        }

        g_object_set(udpsrc, "caps", caps.get(), "auto-multicast", TRUE, nullptr);

        // Log configuration at INFO level (will be visible with encoder-cli:4 or udp-input:4)
        sf::info(udp_input, "Configured udpsrc on port %u with flexible caps (payload type will be negotiated): %s",
                 config.udp.resolved_port(), caps_stream.str().c_str());

        sf::info(udp_input, "UDP source configured: listening on %s:%u for %s RTP stream",
                 bind_address.empty() ? "0.0.0.0" : bind_address.c_str(), config.udp.resolved_port(),
                 encoding_upper.c_str());

        return true;
    }

    /**
     * @brief Checks if the UDP encoding is H.265.
     * @param config The encoder configuration.
     * @return true if encoding is H.265, false otherwise.
     */
    bool using_h265(const encoder_config &config) { return to_upper_copy(config.udp.resolved_encoding()) == "H265"; }
} // namespace

void init_udp_input_debug_category() {
    GST_DEBUG_CATEGORY_INIT(udp_input, "udp-input", 0, "UDP input pipeline operations");
}

namespace {
    /**
     * @brief Creates basic UDP input elements (source, capsfilter, jitterbuffer, depay, parser).
     * @param elements UDP input elements structure to populate.
     * @param is_h265 Whether H.265 encoding is used.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool create_basic_udp_elements(udp_input_elements &elements, bool is_h265) {
        elements.source = gst_element_factory_make("udpsrc", "udp-source");
        elements.rtp_capsfilter = gst_element_factory_make("capsfilter", "udp-rtp-capsfilter");
        elements.jitterbuffer = gst_element_factory_make("rtpjitterbuffer", "udp-jitterbuffer");
        elements.depay = gst_element_factory_make(is_h265 ? "rtph265depay" : "rtph264depay", "udp-depay");
        elements.parser = gst_element_factory_make(is_h265 ? "h265parse" : "h264parse", "udp-parser");

        if (!elements.source) {
            sf::error(udp_input, "Failed to create udpsrc element");
            return false;
        }
        if (!elements.rtp_capsfilter) {
            sf::error(udp_input, "Failed to create RTP capsfilter element");
            return false;
        }
        if (!elements.jitterbuffer) {
            sf::error(udp_input, "Failed to create rtpjitterbuffer element");
            return false;
        }
        if (!elements.depay) {
            sf::error(udp_input, "Failed to create RTP depayloader element (%s)",
                      is_h265 ? "rtph265depay" : "rtph264depay");
            return false;
        }
        if (!elements.parser) {
            sf::error(udp_input, "Failed to create parser element (%s)", is_h265 ? "h265parse" : "h264parse");
            return false;
        }

        return true;
    }

    /**
     * @brief Creates H.265 capsfilter if needed.
     * @param elements UDP input elements structure.
     * @param is_h265 Whether H.265 encoding is used.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool create_h265_capsfilter(udp_input_elements &elements, bool is_h265) {
        if (!is_h265) {
            return true;
        }

        elements.capsfilter = gst_element_factory_make("capsfilter", "udp-h265-byte-stream");
        if (!elements.capsfilter) {
            sf::error(udp_input, "Failed to create capsfilter for UDP H265 stream");
            return false;
        }

        // Both nvv4l2decoder (ARM64) and nvh265dec (x86_64) expect H.265 in byte-stream format with AU alignment
        // The alignment=au ensures frames are properly aligned for the decoder
        const sf::unique_gst_caps_ptr h265_caps(gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING,
                                                                    "byte-stream", "alignment", G_TYPE_STRING, "au",
                                                                    nullptr));
        if (!h265_caps) {
            sf::error(udp_input, "Failed to allocate caps for UDP H265 byte-stream negotiation");
            return false;
        }

        g_object_set(elements.capsfilter, "caps", h265_caps.get(), nullptr);
        sf::info(udp_input, "Configured H.265 capsfilter with byte-stream format and AU alignment");

        return true;
    }

    /**
     * @brief Creates decoder element based on architecture and encoder profile.
     *
     * On x86_64 the hardware decoder is nvh265dec, which requires gst.cuda.context on the
     * decoder. The pipeline does not supply that context (NEED_CONTEXT is only logged), so
     * negotiation fails with not-negotiated and the UDP source errors. Kvazaar was fixed by
     * using avdec_h265; the same path is required for UDP whenever nvh265dec would be used
     * (including NVENC), so we always use avdec_h265 + videoconvert on x86 UDP. nvvideoconvert
     * downstream still uploads to NVMM for DeepStream and HW encoders.
     *
     * On ARM64 the decoder is nvv4l2decoder, which does not have the same CUDA-context
     * requirement on the UDP path, so the hardware decoder is kept there.
     *
     * @param config Encoder configuration (encoder element selection).
     * @param elements UDP input elements structure.
     * @return Decoder name or nullptr on failure.
     */
    [[nodiscard]] const char *create_decoder_element([[maybe_unused]] const encoder_config &config,
                                                     udp_input_elements &elements) {
#if !defined(USE_ARM64_DECODER) || USE_ARM64_DECODER != 1
        // x86: avoid nvh265dec on UDP (gst.cuda.context never satisfied -> not-negotiated).
        elements.decoder = gst_element_factory_make("avdec_h265", "udp-hw-decoder");
        if (!elements.decoder) {
            sf::error(udp_input,
                      "Failed to create avdec_h265 for UDP input. nvh265dec requires gst.cuda.context which this "
                      "pipeline does not set; software decode is required for UDP on x86. "
                      "Install gst-libav / ffmpeg decoder plugin.");
            return nullptr;
        }
        sf::info(udp_input, "Using software H.265 decoder avdec_h265 for UDP input (nvh265dec needs gst.cuda.context; "
                            "videoconvert + nvvideoconvert still feed NVMM for HW encoder)");
        return "avdec_h265";
#else
        const char *decoder_name = get_decoder_element_name();
        // RAII_EXEMPT: udp_input_elements stores non-owning raw handles used by gst_bin_add paths.
        elements.decoder = create_h265_decoder("udp-hw-decoder").release();
        if (!elements.decoder) {
            sf::error(
                    udp_input,
                    "Failed to create hardware decoder '%s'. This may indicate CUDA initialization failure or missing "
                    "NVIDIA codec plugin. Check CUDA availability and GStreamer plugin installation.",
                    decoder_name);
            return nullptr;
        }

        sf::info(udp_input, "Successfully created hardware decoder %s, element name: %s", decoder_name,
                 GST_ELEMENT_NAME(elements.decoder));

        return decoder_name;
#endif
    }

    /**
     * @brief Creates converter and queue elements.
     * @param elements UDP input elements structure.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool create_converter_and_queue_elements(udp_input_elements &elements) {
        elements.queue_before_decoder = gst_element_factory_make("queue", "udp-queue-before-decoder");
        if (!elements.queue_before_decoder) {
            sf::error(udp_input, "Failed to create queue element before decoder");
            return false;
        }

        elements.capsfilter_after_decoder = nullptr;
#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
        elements.capsfilter_after_decoder = gst_element_factory_make("capsfilter", "udp-capsfilter-after-decoder");
        if (!elements.capsfilter_after_decoder) {
            sf::error(udp_input, "Failed to create capsfilter element after decoder");
            return false;
        }
#else
        sf::info(udp_input, "Skipping decoder capsfilter on x86 (nvh265dec links directly to nvvideoconvert)");
#endif

        // Software decoder (avdec_h265) outputs system memory; videoconvert feeds nvvideoconvert.
        elements.system_converter = nullptr;
        if (elements.decoder) {
            GstElementFactory *const dec_factory = gst_element_get_factory(elements.decoder);
            const char *const factory_name =
                    dec_factory ? gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(dec_factory)) : nullptr;
            if (factory_name && std::string_view(factory_name) == "avdec_h265") {
                elements.system_converter = gst_element_factory_make("videoconvert", "udp-videoconvert-before-nv");
                if (!elements.system_converter) {
                    sf::error(udp_input, "Failed to create videoconvert for UDP software decode path");
                    return false;
                }
                sf::info(udp_input, "Inserted videoconvert between avdec_h265 and nvvideoconvert for UDP input");
            }
        }

        elements.gpu_converter = gst_element_factory_make("nvvideoconvert", "udp-nvvideoconvert");
        if (!elements.gpu_converter) {
            sf::error(
                    udp_input,
                    "Failed to create nvvideoconvert element. This may indicate CUDA initialization failure or missing "
                    "NVIDIA video plugin. Check CUDA availability and GStreamer plugin installation.");
            return false;
        }

        elements.capsfilter_after_converter = gst_element_factory_make("capsfilter", "udp-capsfilter-after-converter");
        if (!elements.capsfilter_after_converter) {
            sf::error(udp_input, "Failed to create capsfilter element after converter");
            return false;
        }

        elements.queue_before_tee = gst_element_factory_make("queue", "udp-queue-before-tee");
        if (!elements.queue_before_tee) {
            sf::error(udp_input, "Failed to create queue element before tee");
            return false;
        }

        elements.tee = gst_element_factory_make("tee", "input-tee");
        if (!elements.tee) {
            sf::error(udp_input, "Failed to create tee element");
            return false;
        }

        return true;
    }

    /**
     * @brief Configures RTP capsfilter.
     * @param config Encoder configuration.
     * @param elements UDP input elements structure.
     * @return true on success, false otherwise.
     */
    [[nodiscard]] bool configure_rtp_capsfilter(const encoder_config &config, udp_input_elements &elements) {
        // Configure RTP capsfilter before jitterbuffer with flexible caps (no fixed payload type)
        // This ensures proper caps negotiation while allowing payload type to be negotiated from stream
        const std::string encoding_upper = to_upper_copy(config.udp.resolved_encoding());
        std::ostringstream rtp_caps_stream;
        rtp_caps_stream << "application/x-rtp, media=(string)video, encoding-name=(string)" << encoding_upper
                        << ", clock-rate=(int)" << config.udp.resolved_clock_rate();

        const sf::unique_gst_caps_ptr rtp_caps(gst_caps_from_string(rtp_caps_stream.str().c_str()));
        if (!rtp_caps) {
            sf::error(udp_input, "Failed to create RTP caps for capsfilter");
            return false;
        }
        g_object_set(elements.rtp_capsfilter, "caps", rtp_caps.get(), nullptr);
        sf::info(udp_input, "Configured RTP capsfilter with flexible caps (payload type will be negotiated): %s",
                 rtp_caps_stream.str().c_str());

        return true;
    }

    /**
     * @brief Configures rtpjitterbuffer.
     * @param config Encoder configuration.
     * @param elements UDP input elements structure.
     */
    void configure_jitterbuffer(const encoder_config &config, udp_input_elements &elements) {
        // Configure rtpjitterbuffer with latency from config
        // Increased latency and misorder time help handle out-of-order packets and reduces missing reference frame
        // errors
        const auto latency_ms = static_cast<guint>(config.udp.resolved_latency_ms());
        // Use higher latency (at least 500ms) to allow more time for out-of-order packets
        // This reduces "Could not find ref with POC" errors from the decoder
        const guint effective_latency = latency_ms < 500 ? 500 : latency_ms;
        g_object_set(elements.jitterbuffer, "latency", effective_latency, "drop-on-latency", FALSE, "do-retransmission",
                     FALSE, "max-misorder-time", 1000U, nullptr);
        sf::info(udp_input,
                 "Configured rtpjitterbuffer with latency: %u ms (effective: %u ms), max-misorder-time: 5000 ms to "
                 "reduce "
                 "decoder errors",
                 latency_ms, effective_latency);
    }

    /**
     * @brief Configures queue elements.
     * @param elements UDP input elements structure.
     */
    void configure_queue_elements(udp_input_elements &elements) {
        // Configure queue before decoder to buffer frames for decoder processing
        // Increased size and leaky=downstream to prevent blocking when parser passes through frames before parameter
        // sets This ensures frames can flow even if decoder is temporarily slow
        g_object_set(elements.queue_before_decoder, "max-size-buffers", 50, "max-size-time", 0, "leaky", 2, nullptr);
        sf::debug(udp_input, "Configured queue before decoder: max-size-buffers=50, leaky=downstream");

        // Configure queue before tee to decouple tee branches
        // Increased size and leaky=downstream to handle bursts and prevent blocking DeepStream processing
        g_object_set(elements.queue_before_tee, "max-size-buffers", 50, "max-size-time", 0, "leaky", 2, nullptr);
        sf::debug(udp_input, "Configured queue before tee: max-size-buffers=50, leaky=downstream");
    }

    /**
     * @brief Configures parser and converter elements.
     * @param elements UDP input elements structure.
     */
    void configure_parser_and_converter(udp_input_elements &elements) {
        // Configure parser and converter elements
        // For UDP streams, allow passthrough mode so parser doesn't drop frames before parameter sets arrive
        // The parser will still parse when it can, but won't block/drop frames it can't parse yet
        // This prevents pipeline stalls when frames arrive before VPS/SPS/PPS parameter sets
        g_object_set(elements.parser, "disable-passthrough", FALSE, nullptr);
        sf::info(udp_input,
                 "Configured parser with passthrough enabled to handle out-of-order parameter sets in UDP streams");

        // Configure capsfilter to enforce decoder output memory type for the GPU converter.
        if (elements.capsfilter_after_decoder) {
            const char *caps_string = "video/x-raw(memory:NVMM)";
            const sf::unique_gst_caps_ptr caps(gst_caps_from_string(caps_string));
            if (caps) {
                g_object_set(elements.capsfilter_after_decoder, "caps", caps.get(), nullptr);
                sf::debug(udp_input, "Configured capsfilter after decoder to enforce %s output", caps_string);
            }
        }

        // Configure nvvideoconvert to use GPU compute
        g_object_set(elements.gpu_converter, "compute-hw", 1, nullptr);

        // Configure capsfilter_after_converter to force system memory format
        // This ensures NVMM buffers from nvvideoconvert are converted to system memory
        if (elements.capsfilter_after_converter) {
            const sf::unique_gst_caps_ptr caps(
                    gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", nullptr));
            if (caps) {
                g_object_set(elements.capsfilter_after_converter, "caps", caps.get(), nullptr);
                sf::debug(udp_input, "Configured capsfilter after converter to force system memory (I420)");
            }
        }
    }
} // namespace

/**
 * @brief Creates and configures UDP input elements.
 * @param config Encoder configuration with UDP settings.
 * @param elements Output element container.
 * @return True if all required elements were created.
 */
bool create_udp_input_elements(const encoder_config &config, udp_input_elements &elements) {
    const bool is_h265 = using_h265(config);

    if (!create_basic_udp_elements(elements, is_h265)) {
        return false;
    }

    if (!create_h265_capsfilter(elements, is_h265)) {
        return false;
    }

    const char *decoder_name = create_decoder_element(config, elements);
    if (!decoder_name) {
        return false;
    }

    if (!create_converter_and_queue_elements(elements)) {
        return false;
    }

    if (std::string_view(decoder_name) != "avdec_h265") {
        // Hardware decoder outputs GPU memory; software path sets system_converter above
        sf::info(udp_input, "Using hardware decoder %s for UDP input (GPU-accelerated H.265 decoding)", decoder_name);
    }

    if (!configure_udpsrc(config, elements.source)) {
        return false;
    }

    if (!configure_rtp_capsfilter(config, elements)) {
        return false;
    }

    configure_jitterbuffer(config, elements);
    configure_queue_elements(elements);
    configure_parser_and_converter(elements);

    sf::info(udp_input, "%s configured - ready for H.265 decoding", decoder_name);

    if (is_h265 && elements.capsfilter) {
        sf::info(udp_input,
                 "UDP input elements created successfully: udpsrc -> rtp-capsfilter -> rtpjitterbuffer -> %s -> %s -> "
                 "%s -> queue -> decoder -> nvvideoconvert -> capsfilter -> queue -> tee",
                 GST_ELEMENT_NAME(elements.depay), GST_ELEMENT_NAME(elements.parser),
                 GST_ELEMENT_NAME(elements.capsfilter));
    } else {
        sf::info(udp_input,
                 "UDP input elements created successfully: udpsrc -> rtp-capsfilter -> rtpjitterbuffer -> %s -> %s -> "
                 "queue -> decoder -> nvvideoconvert -> capsfilter -> queue -> tee",
                 GST_ELEMENT_NAME(elements.depay), GST_ELEMENT_NAME(elements.parser));
    }

    return true;
}

/**
 * @brief Sets up UDP packet logging via pad probe on the UDP source element.
 *
 * GStreamer's gst_element_get_static_pad and gst_pad_add_probe require non-const pointers,
 * so this function cannot take udpsrc as const even though it only attaches a probe.
 *
 * @param udpsrc The UDP source element to attach the pad probe to (non-const due to GStreamer API requirements).
 * @param data The narrow input/ingress view (UDP runtime counters and bitrate tracker).
 * @return true if pad probe was successfully attached, false otherwise.
 */
bool setup_udp_packet_logging(GstElement *udpsrc, input_ingress_facade &data) {
    if (!udpsrc) {
        sf::error(udp_input, "Cannot setup UDP packet logging: udpsrc element is null");
        return false;
    }

    GstPad *const src_pad = gst_element_get_static_pad(udpsrc, "src");
    if (!src_pad) {
        sf::error(udp_input, "Failed to get src pad from udpsrc element");
        return false;
    }

    // The pad-probe callback reads mutable per-instance UDP counters, which GStreamer requires
    // through a stable raw pointer; the facade's instance() is that escape hatch.
    const auto user_data = reinterpret_cast<gpointer>(&data.instance());

    // Add pad probe to detect UDP packets and track bitrate
    const gulong probe_id = gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, udp_packet_probe, user_data, nullptr);
    if (probe_id == 0) {
        sf::error(udp_input, "Failed to add pad probe to udpsrc for packet logging");
        gst_object_unref(src_pad);
        return false;
    }

    sf::info(udp_input, "UDP packet logging and bitrate tracking enabled: Monitoring packets on port %u",
             data.params.udp.resolved_port());
    gst_object_unref(src_pad);
    return true;
}
