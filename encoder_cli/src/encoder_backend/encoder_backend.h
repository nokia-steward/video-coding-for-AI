// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "gst_raii.h"
#include "qp_map_frame_payload.h"

#include <memory>
#include <optional>
#include <string>

struct global_data;
struct encoder_config;

/**
 * @file encoder_backend.height
 * @brief Encoder backend abstraction for encoded frame production.
 */

enum class encoder_backend_kind { gst_pipeline, nvenc_api };

class encoder_backend {
public:
    virtual ~encoder_backend() = default;

    [[nodiscard]] virtual encoder_backend_kind kind() const = 0;
    [[nodiscard]] virtual bool requires_gst_encoding_pipeline() const = 0;

    [[nodiscard]] virtual bool submit_frame(global_data &data, sf::unique_gst_buffer_ptr output_buffer,
                                            std::optional<qp_map_frame_payload> qp_map_payload) = 0;

    virtual void signal_eos(global_data &data) = 0;
};

struct encoder_backend_factory_result {
    std::unique_ptr<encoder_backend> backend;
    bool qp_map_direct_supported = false;
    std::string reason{};
};

[[nodiscard]] bool validate_qp_map_frame_payload(const qp_map_frame_payload &payload, std::string *reason);
[[nodiscard]] std::unique_ptr<encoder_backend> create_gst_encoder_backend();
[[nodiscard]] encoder_backend_factory_result try_create_nvenc_api_backend(const encoder_config &params);
