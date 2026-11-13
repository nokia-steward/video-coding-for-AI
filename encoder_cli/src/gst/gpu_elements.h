// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <optional>
#include <string>
#include <string_view>
#include "../../libvcai/include/gst_raii.h"

/**
 * @file gpu_elements.height
 * @brief GPU encoder/decoder element helpers for GStreamer pipelines.
 */

// Forward declaration to avoid pulling encoder_config into all gpu_elements users.
struct encoder_config;

/**
 * @brief Upload/memory caps profile required upstream of the encoder.
 */
enum class encoder_caps_profile {
    cuda, ///< CUDA memory path (e.g. nvh265enc)
    nvmm, ///< NVMM memory path (e.g. nvv4l2h265enc)
    system ///< System memory (e.g. kvazaarenc; no nvvideoconvert upload)
};

/**
 * @brief Which GObject property sets GOP size for this encoder.
 */
enum class encoder_gop_property {
    gop_size, ///< nvh265enc: gop-size
    iframeinterval, ///< nvv4l2h265enc: iframeinterval
    intra_period ///< kvazaarenc: intra-period
};

/**
 * @brief Appsrc configuration profile for the encoding pipeline.
 */
struct encoder_appsrc_profile {
    const char *format; ///< appsrc caps format (e.g. I420, NV12)
    bool block; ///< whether appsrc block=TRUE is required
};

/**
 * @brief Registered encoder factory: caps, GOP, appsrc, and platform support.
 *
 * New encoders are added by appending a descriptor. unsupported names exit via
 * validation or create_h265_encoder throwing std::runtime_error.
 */
struct encoder_descriptor {
    const char *factory_name;
    bool supported_when_arm64_encoder; ///< USE_ARM64_ENCODER == 1
    bool supported_when_x86_encoder; ///< USE_ARM64_ENCODER == 0
    encoder_caps_profile caps_profile;
    encoder_gop_property gop_property;
    encoder_appsrc_profile appsrc;
};

void init_gpu_elements_debug_category();

[[nodiscard]] const char *get_default_encoder_element_name();
[[nodiscard]] const char *get_encoder_element_name();
[[nodiscard]] std::string resolve_encoder_element_factory_name(const encoder_config &config);
[[nodiscard]] const encoder_descriptor *find_encoder_descriptor(std::string_view factory_name);
/** @brief Whether factory_name is allowed on this build (x86 vs Jetson encoder path). */
[[nodiscard]] bool encoder_supported_on_current_build(std::string_view factory_name);
void validate_encoder_element_or_throw(const encoder_config &config);
[[nodiscard]] const char *get_decoder_element_name();

/**
 * @brief Formats nvv4l2h265enc `qp-range` as MinQpP,MaxQpP:MinQpI,MaxQpI:MinQpB,MaxQpB.
 *
 * Constant QP uses the same value for min and max on I, P, and B. Do not use the
 * nonexistent `quant-range` property — GLib ignores it and the encoder stays at
 * default VBR (~4 Mbps on Orin).
 */
[[nodiscard]] std::string format_nvv4l2_qp_range(int qp);

/**
 * @brief Creates an H.265 encoder element using the registered descriptor for factory_name.
 * kvazaarenc uses bitrate in kbps and intra-period for GOP; nvh265enc uses gop-size in frames.
 * @return Owning `sf::unique_gst_element_ptr`; release only when transferring ownership to GstBin.
 */
[[nodiscard]] sf::unique_gst_element_ptr create_h265_encoder(const char *name, std::string_view factory_name,
                                                             int bitrate_bps,
                                                             std::optional<int> const_qp = std::nullopt);
[[nodiscard]] sf::unique_gst_element_ptr create_h265_decoder(const char *name);
[[nodiscard]] sf::unique_gst_element_ptr create_gpu_video_convert(const char *name);
[[nodiscard]] sf::unique_gst_element_ptr create_nvosd_element(const char *name);
[[nodiscard]] sf::unique_gst_caps_ptr create_nvmm_caps(const char *format);
[[nodiscard]] sf::unique_gst_caps_ptr create_cuda_caps(const char *format);
[[nodiscard]] sf::unique_gst_element_ptr create_cpu_to_gpu_upload(const char *name);
[[nodiscard]] sf::unique_gst_element_ptr create_gpu_to_cpu_download(const char *name);
[[nodiscard]] bool check_gpu_elements_available();
