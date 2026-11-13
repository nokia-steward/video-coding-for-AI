// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <string_view>
#include "gst_raii.h"

/**
 * @file gpu_elements.h
 * @brief GPU encoder/decoder element helpers for GStreamer pipelines.
 *
 * Supported H.265 decoders: nvh265dec (NVMM output) and openhevcdec (software, system memory).
 * Post-decoder conversion must match decoder memory type (see create_decoded_postprocess_convert).
 */

struct rtp_server_config;

void init_gpu_elements_debug_category();

[[nodiscard]] const char *get_encoder_element_name();

/**
 * @brief Decoder factory name (desktop: nvh265dec).
 */
[[nodiscard]] const char *get_default_decoder_element_name();

[[nodiscard]] sf::unique_gst_element_ptr create_h265_encoder(const char *name, int bitrate_bps);

/**
 * @brief Creates H.265 decoder by factory name (must be registered).
 * @return Owning `sf::unique_gst_element_ptr`; release only when transferring ownership to GstBin.
 * @throws std::runtime_error if unsupported or creation fails.
 */
[[nodiscard]] sf::unique_gst_element_ptr create_h265_decoder(const char *name, std::string_view factory_name);

[[nodiscard]] std::string resolve_decoder_element_factory_name(const rtp_server_config &config);

/**
 * @brief Returns true if factory_name is a registered decoder for rtp_server.
 */
[[nodiscard]] bool is_registered_decoder_element(std::string_view factory_name);

/**
 * @throws std::runtime_error if name unsupported or plugin missing.
 */
void validate_decoder_element_or_throw(const rtp_server_config &config);

[[nodiscard]] sf::unique_gst_element_ptr create_gpu_video_convert(const char *name);

[[nodiscard]] sf::unique_gst_element_ptr create_nvosd_element(const char *name);

[[nodiscard]] sf::unique_gst_element_ptr create_gpu_to_cpu_download(const char *name);

/**
 * @brief True when the decoder outputs raw video in system memory (not NVMM).
 * Used to select videoconvert vs nvvideoconvert after decode.
 */
[[nodiscard]] bool decoder_outputs_system_memory(std::string_view decoder_factory_name);

/**
 * @brief Creates the element after the H.265 decoder: NVMM download for nvh265dec, videoconvert for software decoders.
 * @throws std::runtime_error if the required converter cannot be created.
 */
[[nodiscard]] sf::unique_gst_element_ptr create_decoded_postprocess_convert(const char *name,
                                                                            std::string_view decoder_factory_name);

/**
 * @brief Validates encoder + resolved decoder + post-decoder converter exist.
 * @param config When non-null, decoder_element from config is used; otherwise default nvh265dec.
 * When using openhevcdec, nvvideoconvert is not required for the processing chain; display branch may still need it.
 */
[[nodiscard]] bool check_gpu_elements_available(const rtp_server_config *config);

/**
 * @brief Same as check_gpu_elements_available(nullptr).
 */
[[nodiscard]] bool check_gpu_elements_available();
