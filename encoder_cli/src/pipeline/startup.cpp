// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "startup.h"
#include "../../libvcai/include/error.h"
#include "../../libvcai/include/gst_raii.h"
#include "../../libvcai/include/logging.h"
#include "../application/thread_management.h"
#include "../encoder_backend/encoder_backend.h"
#include "../encoder_cli_debug.h"
#include "../encoder_enums.h"
#include "../gst/input_pipeline.h"
#include "../gst/output_pipeline.h"
#include "../gst/output_pipeline/rtp_branch.h"
#include "../utils/config_parser.h"
#include "state_management.h"

#include <chrono>
#include <gst/gst.h>
#include <thread>

// Define the debug category (declared as extern in encoder_cli_debug.height)
GST_DEBUG_CATEGORY(encoder_cli_main);

/**
 * @file startup.cpp
 * @brief Implementation of pipeline creation and startup functions.
 */

/**
 * @brief Create all required pipelines for the application.
 *
 * Creates input, encoding, and output pipelines in sequence.
 *
 * @param data The global data structure.
 * @return true if all pipelines were created successfully, false otherwise.
 */
bool create_all_pipelines(lifecycle_facade &data) {
    return create_input_pipeline(data.instance()) && create_encoding_pipeline(data.instance()) &&
           create_output_pipeline(data.instance());
}

void start_all_pipelines(lifecycle_facade &data) {
    // Use sf::info for consistency with rest of codebase
    sf::info(encoder_cli_main, "=== start_all_pipelines called ===");

    const auto start_encoding_pipeline_if_needed = [&data]() {
        if (data.backend.active() && data.backend.requires_gst_encoding_pipeline()) {
            sf::info(encoder_cli_main, "Starting encoding pipeline...");
            sf::check_error(start_pipeline_with_error_check(data.encoding_pipeline.get(), "encoding pipeline"),
                            "Failed to set encoding pipeline to playing");
            sf::info(encoder_cli_main, "Encoding pipeline started successfully");
        } else {
            sf::info(encoder_cli_main, "Skipping encoding pipeline start (direct encoder backend active)");
        }
    };

    try {
        const bool is_webrtc_mode =
                data.params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc;

        // For WebRTC mode, delay starting input/encoding pipelines until connection is ready
        // This prevents frames from being produced before WebRTC is ready to receive them
        if (!is_webrtc_mode) {
            const bool is_file_mode = data.params.input_type_value == input_type::file;
            if (is_file_mode) {
                // File mode: start the encoding pipeline first so the GStreamer encoder is PLAYING
                // before frames arrive, and defer the input pipeline until the file-mode runner has
                // armed the consuming worker threads. Otherwise a finite clip can be EOS-closed before
                // ROI/encoder-submission workers run. The runner starts the input via
                // start_input_pipeline_only() once workers are armed.
                sf::info(encoder_cli_main,
                         "File mode: starting encoding pipeline and deferring input pipeline until workers are armed");
                start_encoding_pipeline_if_needed();
            } else {
                sf::info(encoder_cli_main, "Starting input pipeline...");
                sf::check_error(start_pipeline_with_error_check(data.input_pipeline.get(), "input pipeline"),
                                "Failed to set input pipeline to playing");
                sf::info(encoder_cli_main, "Input pipeline started successfully");

                // Start bypass polling threads if bypass is enabled
                if (data.params.performance.enable_deepstream_bypass) {
                    restart_bypass_polling_threads_if_needed(data.instance());
                }

                start_encoding_pipeline_if_needed();
            }
        } else {
            sf::info(encoder_cli_main,
                     "WebRTC mode detected: Delaying input/encoding pipeline start until WebRTC connection is ready");
        }

        sf::info(encoder_cli_main, "Starting output pipeline...");
        sf::check_error(start_pipeline_with_error_check(data.output_pipeline.get(), "output pipeline"),
                        "Failed to set output pipeline to playing");
        sf::info(encoder_cli_main, "Output pipeline started successfully");

        sf::info(encoder_cli_main, "=== All pipelines set to PLAYING, checking WebRTC ===");

        // Trigger WebRTC negotiation after output pipeline is PLAYING
        // This ensures the webrtcbin is ready and can create an offer
        sf::info(encoder_cli_main, "Checking if WebRTC negotiation trigger is needed: transport_type=%s",
                 (data.params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc
                          ? "webrtc"
                          : "rtp"));
        if (data.params.communication.resolved_transport_type() == sf::communication::transport_type::webrtc) {
            sf::info(encoder_cli_main, "WebRTC transport detected, waiting for pipeline to stabilize...");
            // Wait a bit for pipeline to stabilize
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            sf::info(encoder_cli_main, "Pipeline stabilization wait completed, triggering WebRTC negotiation...");

            // Verify webrtcbin is in correct state before triggering
            if (data.output_pipeline) {
                const sf::unique_gst_element_ptr webrtcbin(
                        gst_bin_get_by_name(GST_BIN(data.output_pipeline.get()), "webrtcbin"));
                if (webrtcbin) {
                    GstState current_state = GST_STATE_NULL;
                    GstState pending = GST_STATE_VOID_PENDING;
                    gst_element_get_state(webrtcbin.get(), &current_state, &pending, 0);
                    sf::info(encoder_cli_main, "webrtcbin state before negotiation trigger: %s (pending: %s)",
                             gst_element_state_get_name(current_state),
                             pending != GST_STATE_VOID_PENDING ? gst_element_state_get_name(pending) : "none");
                } else {
                    sf::warning(encoder_cli_main, "webrtcbin element not found in output pipeline");
                }
            }

            if (!trigger_webrtc_negotiation(data.instance())) {
                sf::warning(encoder_cli_main, "Failed to trigger WebRTC negotiation, but continuing");
            } else {
                sf::info(encoder_cli_main, "WebRTC negotiation trigger completed successfully");
            }
        } else {
            sf::info(encoder_cli_main, "Not WebRTC transport, skipping negotiation trigger");
        }
    } catch (const std::exception &e) {
        sf::error(encoder_cli_main, "Exception in start_all_pipelines: %s", e.what());
        throw; // Re-throw to be caught by main()
    }

    sf::info(encoder_cli_main, "=== start_all_pipelines completed ===");
}

bool start_input_pipeline_only(lifecycle_facade &data) {
    sf::info(encoder_cli_main, "=== start_input_pipeline_only called ===");

    try {
        if (!data.input_pipeline) {
            sf::warning(encoder_cli_main, "Input pipeline missing; cannot start it");
            return false;
        }

        sf::info(encoder_cli_main, "Starting input pipeline (worker threads armed)...");
        if (!start_pipeline_with_error_check(data.input_pipeline.get(), "input pipeline")) {
            sf::error(encoder_cli_main, "Failed to set input pipeline to playing");
            return false;
        }
        sf::info(encoder_cli_main, "Input pipeline started successfully");

        // Start bypass polling threads if bypass is enabled
        if (data.params.performance.enable_deepstream_bypass) {
            restart_bypass_polling_threads_if_needed(data.instance());
        }

        return true;
    } catch (const std::exception &e) {
        sf::error(encoder_cli_main, "Exception in start_input_pipeline_only: %s", e.what());
        return false;
    }
}

bool start_input_and_encoding_pipelines(lifecycle_facade &data) {
    sf::info(encoder_cli_main, "=== start_input_and_encoding_pipelines called ===");

    try {
        sf::info(encoder_cli_main, "Starting input pipeline...");
        if (!start_pipeline_with_error_check(data.input_pipeline.get(), "input pipeline")) {
            sf::error(encoder_cli_main, "Failed to set input pipeline to playing");
            return false;
        }
        sf::info(encoder_cli_main, "Input pipeline started successfully");

        // Start bypass polling threads if bypass is enabled
        if (data.params.performance.enable_deepstream_bypass) {
            restart_bypass_polling_threads_if_needed(data.instance());
        }

        if (data.backend.active() && data.backend.requires_gst_encoding_pipeline()) {
            sf::info(encoder_cli_main, "Starting encoding pipeline...");
            if (!start_pipeline_with_error_check(data.encoding_pipeline.get(), "encoding pipeline")) {
                sf::error(encoder_cli_main, "Failed to set encoding pipeline to playing");
                return false;
            }
            sf::info(encoder_cli_main, "Encoding pipeline started successfully");
        } else {
            sf::info(encoder_cli_main, "Skipping encoding pipeline start (direct encoder backend active)");
        }

        sf::info(encoder_cli_main, "=== start_input_and_encoding_pipelines completed ===");
        return true;
    } catch (const std::exception &e) {
        sf::error(encoder_cli_main, "Exception in start_input_and_encoding_pipelines: %s", e.what());
        return false;
    }
}

void configure_deepstream_elements(lifecycle_facade &data) {
    sf::info(encoder_cli_main, "=== configure_deepstream_elements called ===");
    const sf::unique_gst_element_ptr pgie(
            gst_bin_get_by_name(GST_BIN(data.input_pipeline.get()), "primary-nvinference-engine"));
    if (pgie) {
        data.pgie_class_ids_to_filter = get_class_ids(pgie.get(), data.params.labels);
        sf::info(encoder_cli_main, "configure_deepstream_elements: Found pgie element");
    } else {
        sf::warning(encoder_cli_main, "Could not find pgie element in pipeline to get labels.");
    }
    sf::info(encoder_cli_main, "=== configure_deepstream_elements completed ===");
}
