// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_backend.h"

#include <chrono>

#include "../../libvcai/include/logging.h"
#include "../bridge/app/encoder_app_adapter.h"
#include "../bridge/context/encoder_bridge_context.h"
#include "../bridge/orchestration/encoded_bridge_orchestrator.h"
#include "../encoder_cli_debug.h"
#include "../global_data.h"
#include "../gst/input_types/frame_path_meta.h"
#include "../input_analyzer/input_analyzer.h"
#include "../processing/frame_processing_utils.h"

namespace {
    [[nodiscard]] std::optional<frame_properties> get_encoding_appsrc_frame_properties(const global_data &data) {
        frame_properties props{};
        props.width = data.params.width;
        props.height = data.params.height;
        props.format = data.params.format.has_value() ? sf::video_format_to_string(*data.params.format) : "I420";

        GstCaps *caps = nullptr;
        g_object_get(G_OBJECT(data.encoded_ports.encoding_src), "caps", &caps, nullptr);
        if (caps != nullptr) {
            const GstStructure *structure = gst_caps_get_structure(caps, 0);
            if (structure != nullptr) {
                int width_from_caps = 0;
                int height_from_caps = 0;
                if (gst_structure_get_int(structure, "width", &width_from_caps) && width_from_caps > 0) {
                    props.width = width_from_caps;
                }
                if (gst_structure_get_int(structure, "height", &height_from_caps) && height_from_caps > 0) {
                    props.height = height_from_caps;
                }
                if (const gchar *format_from_caps = gst_structure_get_string(structure, "format");
                    format_from_caps != nullptr) {
                    props.format = format_from_caps;
                }
            }
            gst_caps_unref(caps);
        }

        if (props.width <= 0 || props.height <= 0 || props.format == nullptr) {
            return std::nullopt;
        }
        return props;
    }

    [[nodiscard]] encoded_frame_luma_adaptation_state
    build_effective_luma_adaptation_state(global_data &data, sf::unique_gst_buffer_ptr &output_buffer) {
        if (!data.params.enable_luma_adaptation) {
            return {};
        }

        const auto props = get_encoding_appsrc_frame_properties(data);
        if (!props.has_value()) {
            sf::warning(encoder_cli_main,
                        "Unable to infer frame properties for luma downscaling; disabling luma-adaptation EOI for this "
                        "frame");
            return {};
        }

        const luma_factors_for_frame factors = get_luma_factors_for_frame(data, output_buffer.get(), *props);

        if (!apply_luma_scaling_to_buffer(output_buffer, *props, factors.downscale_factor)) {
            sf::warning(encoder_cli_main, "Failed to apply luma downscaling before encoder submission; disabling "
                                          "luma-adaptation EOI for this frame");
            return {};
        }

        return {.enabled = true,
                .downscale_factor = factors.downscale_factor,
                .upscale_multiplier = factors.upscale_multiplier};
    }

    class gst_encoder_backend final : public encoder_backend {
    public:
        [[nodiscard]] encoder_backend_kind kind() const override { return encoder_backend_kind::gst_pipeline; }

        [[nodiscard]] bool requires_gst_encoding_pipeline() const override { return true; }

        [[nodiscard]] bool submit_frame(global_data &data, sf::unique_gst_buffer_ptr output_buffer,
                                        std::optional<qp_map_frame_payload> qp_map_payload) override {
            if (!output_buffer) {
                sf::error(encoder_cli_main, "Output buffer is null, cannot push to GStreamer encoder");
                return false;
            }

            if (qp_map_payload.has_value()) {
                sf::debug(encoder_cli_main,
                          "GStreamer backend received QP-map payload for frame %d (ctb_size=%d, elements=%zu); "
                          "payload ignored on this backend",
                          qp_map_payload->frame_number, qp_map_payload->ctb_size, qp_map_payload->delta_map.size());
            }

            const auto effective_luma_adaptation_state = build_effective_luma_adaptation_state(data, output_buffer);
            const auto frame_contract = get_frame_contract_from_buffer(output_buffer.get());
            if (!frame_contract.has_value()) {
                data.metadata_runtime.record_validation_failure();
                data.request_metadata_contract_fail_stop("missing frame contract metadata before encoder submission");
                sf::error(encoder_cli_main, "Missing frame contract metadata on buffer submitted to encoder");
                return false;
            }
            data.push_pending_encoded_contract_state(frame_contract->sequence_id, effective_luma_adaptation_state);

            if (data.params.input_type_value == input_type::file) {
                data.encoder_submission_queue.push({.buffer = std::move(output_buffer),
                                                    .luma_adaptation = effective_luma_adaptation_state,
                                                    .contract_sequence_id = frame_contract->sequence_id});
                return true;
            }

            auto bridge_context = encoded_bridge_context{.encoding_src = data.encoded_ports.encoding_src,
                                                         .sei_src = data.encoded_ports.sei_src,
                                                         .runtime = &data.egress_bridge_runtime};
            const encoder_app_adapter app_adapter(data);
            const encoded_bridge_push_options options{
                    .update_output_counter = false,
                    .update_sei_injection_counter = false,
                    .update_encoder_submission_counter = true,
                    .rollback_pending_luma_on_failure = true,
            };

            const auto result = encoded_bridge_push_owned_buffer_via_orchestrator(bridge_context, app_adapter,
                                                                                  encoded_bridge_target::encoding_src,
                                                                                  std::move(output_buffer), options);
            if (result.status == encoded_bridge_step_status::ok ||
                result.status == encoded_bridge_step_status::recoverable_drop) {
                return true;
            }

            sf::error(encoder_cli_main, "Failed to push buffer to GStreamer encoder: %s (flow: %d)",
                      gst_flow_get_name(result.flow_return), static_cast<int>(result.flow_return));
            return false;
        }

        void signal_eos(global_data &data) override {
            if (data.params.input_type_value == input_type::file) {
                constexpr auto appsrc_drain_timeout = std::chrono::seconds(120);
                sf::info(encoder_cli_main, "File mode: waiting for encoding appsrc queue to drain before EOS");
                if (!data.wait_for_encoding_appsrc_drain(appsrc_drain_timeout)) {
                    sf::warning(encoder_cli_main,
                                "Proceeding with encoding appsrc EOS while input queue may still hold buffers");
                }
            }

            auto bridge_context = encoded_bridge_context{.encoding_src = data.encoded_ports.encoding_src,
                                                         .sei_src = data.encoded_ports.sei_src,
                                                         .runtime = &data.egress_bridge_runtime};
            const encoder_app_adapter app_adapter(data);
            const auto result = encoded_bridge_signal_eos_via_orchestrator(bridge_context, app_adapter,
                                                                           encoded_bridge_target::encoding_src);
            if (result.status == encoded_bridge_step_status::fatal_stop) {
                sf::warning(encoder_cli_main, "Failed to send EOS to encoder appsrc: %s",
                            gst_flow_get_name(result.flow_return));
            }
        }
    };
} // namespace

std::unique_ptr<encoder_backend> create_gst_encoder_backend() { return std::make_unique<gst_encoder_backend>(); }
