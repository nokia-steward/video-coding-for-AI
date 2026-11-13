// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "state_management.h"
#include "../../libvcai/include/gst_raii.h"
#include "../../libvcai/include/logging.h"
#include "../encoder_cli_debug.h"

#include <chrono>
#include <cstring>
#include <gst/gst.h>
#include <thread>

/**
 * @file state_management.cpp
 * @brief Implementation of GStreamer pipeline state management functions.
 */

namespace {
    /**
     * @brief Formats state change return code as string.
     * @param state_ret State change return code.
     * @return String representation.
     */
    [[nodiscard]] const char *format_state_result(const GstStateChangeReturn state_ret) {
        if (state_ret == GST_STATE_CHANGE_SUCCESS) {
            return "SUCCESS";
        }
        if (state_ret == GST_STATE_CHANGE_ASYNC) {
            return "ASYNC";
        }
        if (state_ret == GST_STATE_CHANGE_NO_PREROLL) {
            return "NO_PREROLL";
        }
        if (state_ret == GST_STATE_CHANGE_FAILURE) {
            return "FAILURE";
        }
        return "UNKNOWN";
    }

    /**
     * @brief Checks bus for error messages.
     * @param pipeline Pipeline to check.
     * @param timeout_ms Timeout in milliseconds.
     * @return true if error found, false otherwise.
     */
    [[nodiscard]] bool check_bus_for_errors(GstElement *pipeline, const GstClockTime timeout_ms) {
        const sf::unique_gst_bus_ptr bus(gst_element_get_bus(pipeline));
        if (!bus) {
            return false;
        }

        const sf::unique_gst_message_ptr msg(gst_bus_timed_pop_filtered(bus.get(), timeout_ms, GST_MESSAGE_ERROR));
        if (!msg) {
            return false;
        }

        GError *err_raw = nullptr;
        gchar *dbg_info_raw = nullptr;
        gst_message_parse_error(msg.get(), &err_raw, &dbg_info_raw);
        const auto err = sf::unique_g_error_ptr(err_raw);
        const auto dbg_info = sf::unique_g_char_ptr(dbg_info_raw);
        sf::error(encoder_cli_main, "Error from element %s: %s", GST_OBJECT_NAME(msg->src), err->message);
        if (dbg_info) {
            sf::error(encoder_cli_main, "Debug info: %s", dbg_info.get());
        }
        return true;
    }

    /**
     * @brief Checks bus for warning messages.
     * @param pipeline Pipeline to check.
     * @param timeout_ms Timeout in milliseconds.
     */
    void check_bus_for_warnings(GstElement *pipeline, const GstClockTime timeout_ms) {
        const sf::unique_gst_bus_ptr bus(gst_element_get_bus(pipeline));
        if (!bus) {
            return;
        }

        const sf::unique_gst_message_ptr warning_msg(
                gst_bus_timed_pop_filtered(bus.get(), timeout_ms, GST_MESSAGE_WARNING));
        if (!warning_msg) {
            return;
        }

        GError *warn_raw = nullptr;
        gchar *warn_dbg_info_raw = nullptr;
        gst_message_parse_warning(warning_msg.get(), &warn_raw, &warn_dbg_info_raw);
        const auto warn = sf::unique_g_error_ptr(warn_raw);
        const auto warn_dbg_info = sf::unique_g_char_ptr(warn_dbg_info_raw);
        sf::warning(encoder_cli_main, "Warning during state change from element %s: %s",
                    GST_OBJECT_NAME(warning_msg->src), warn->message);
        if (warn_dbg_info) {
            sf::warning(encoder_cli_main, "Debug info: %s", warn_dbg_info.get());
        }
    }

    /**
     * @brief Gets current pipeline state for diagnostics.
     * @param pipeline Pipeline to query.
     * @param pipeline_name Pipeline name for logging.
     * @return true if state query succeeded, false otherwise.
     */
    [[nodiscard]] bool get_pipeline_state_diagnostics(GstElement *pipeline, const char *pipeline_name) {
        GstState current_state = GST_STATE_NULL;
        GstState pending = GST_STATE_NULL;
        const GstStateChangeReturn get_ret =
                gst_element_get_state(pipeline, &current_state, &pending, 100 * GST_MSECOND);
        if (get_ret == GST_STATE_CHANGE_SUCCESS) {
            sf::error(encoder_cli_main, "Pipeline %s current state: %s, pending: %s", pipeline_name,
                      gst_element_state_get_name(current_state), gst_element_state_get_name(pending));
            return true;
        }
        return false;
    }

    /**
     * @brief Handles state change failure.
     * @param pipeline Pipeline that failed.
     * @param pipeline_name Pipeline name.
     * @return false (always returns false to indicate failure).
     */
    [[nodiscard]] bool handle_state_change_failure(GstElement *pipeline, const char *pipeline_name) {
        sf::error(encoder_cli_main, "Failed to initiate state change to PLAYING for %s", pipeline_name);
        (void) check_bus_for_errors(pipeline, 100 * GST_MSECOND);
        (void) get_pipeline_state_diagnostics(pipeline, pipeline_name);
        return false;
    }

    /**
     * @brief Waits for pipeline state transition to complete.
     * @param pipeline Pipeline to wait for.
     * @param pipeline_name Pipeline name for logging.
     * @return State query result and current state.
     */
    [[nodiscard]] std::pair<GstStateChangeReturn, std::pair<GstState, GstState>>
    wait_for_state_transition(GstElement *pipeline, const char *pipeline_name) {
        sf::info(encoder_cli_main, "Waiting for %s to transition to PLAYING state (timeout: 5 seconds)...",
                 pipeline_name);
        GstState current_state = GST_STATE_NULL;
        GstState pending = GST_STATE_NULL;
        constexpr GstClockTime timeout_ns = 5 * GST_SECOND;
        const GstStateChangeReturn get_ret = gst_element_get_state(pipeline, &current_state, &pending, timeout_ns);

        const char *query_result_str = format_state_result(get_ret);
        sf::info(encoder_cli_main, "Pipeline %s state query completed: result=%s (code=%d), current=%s, pending=%s",
                 pipeline_name, query_result_str, static_cast<int>(get_ret), gst_element_state_get_name(current_state),
                 gst_element_state_get_name(pending));

        return {get_ret, {current_state, pending}};
    }

    /**
     * @brief Handles pipeline not fully in PLAYING state.
     * @param pipeline Pipeline to check.
     * @param pipeline_name Pipeline name.
     * @param current_state Current state.
     * @param pending Pending state.
     * @return true if pipeline should continue, false otherwise.
     */
    [[nodiscard]] bool handle_not_fully_playing(GstElement *pipeline, const char *pipeline_name,
                                                const GstState current_state, const GstState pending) {
        sf::warning(encoder_cli_main, "Pipeline %s not fully in PLAYING state (current: %s, pending: %s)",
                    pipeline_name, gst_element_state_get_name(current_state), gst_element_state_get_name(pending));

        const sf::unique_gst_bus_ptr bus(gst_element_get_bus(pipeline));
        if (bus) {
            if (check_bus_for_errors(pipeline, 100 * GST_MSECOND)) {
                return false;
            }
            check_bus_for_warnings(pipeline, 100 * GST_MSECOND);
        }

        if (strcmp(pipeline_name, "input pipeline") == 0) {
            sf::warning(encoder_cli_main,
                        "Input pipeline not in PLAYING state, but continuing to allow UDP packet reception and "
                        "engine rebuild");
            return true;
        }
        if (strcmp(pipeline_name, "encoding pipeline") == 0) {
            sf::warning(encoder_cli_main,
                        "Encoding pipeline not in PLAYING state, but continuing - will transition to PLAYING once "
                        "input data is available");
            return true;
        }
        if (strcmp(pipeline_name, "output pipeline") == 0) {
            sf::warning(encoder_cli_main,
                        "Output pipeline not in PLAYING state, but continuing - will transition to PLAYING once "
                        "encoded data is available");
            return true;
        }

        return false;
    }

    /**
     * @brief Initiates state change for input pipeline (async).
     * @param pipeline Pipeline to start.
     * @return State change return code.
     */
    [[nodiscard]] GstStateChangeReturn initiate_input_pipeline_state_change(GstElement *pipeline) {
        const GstStateChangeReturn state_ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        if (state_ret == GST_STATE_CHANGE_ASYNC || state_ret == GST_STATE_CHANGE_SUCCESS) {
            sf::info(encoder_cli_main, "Input pipeline state change initiated (async): %s (return code: %d)",
                     state_ret == GST_STATE_CHANGE_SUCCESS ? "SUCCESS" : "ASYNC", static_cast<int>(state_ret));
        }
        return state_ret;
    }

    /**
     * @brief Initiates state change for non-input pipelines (sync).
     * @param pipeline Pipeline to start.
     * @param pipeline_name Pipeline name for logging.
     * @return State change return code.
     */
    [[nodiscard]] GstStateChangeReturn initiate_non_input_pipeline_state_change(GstElement *pipeline,
                                                                                const char *pipeline_name) {
        const GstStateChangeReturn state_ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
        const char *state_result_str = format_state_result(state_ret);
        sf::info(encoder_cli_main, "Pipeline %s state change initiated: %s (return code: %d)", pipeline_name,
                 state_result_str, static_cast<int>(state_ret));
        return state_ret;
    }
} // namespace

bool start_pipeline_with_error_check(GstElement *pipeline, const char *pipeline_name) {
    sf::info(encoder_cli_main, "Setting %s to PLAYING state...", pipeline_name);

    const bool is_input_pipeline = strcmp(pipeline_name, "input pipeline") == 0;
    GstStateChangeReturn state_ret;

    if (is_input_pipeline) {
        state_ret = initiate_input_pipeline_state_change(pipeline);
        if (state_ret == GST_STATE_CHANGE_ASYNC || state_ret == GST_STATE_CHANGE_SUCCESS) {
            // For async state changes, verify the state after a short delay
            if (state_ret == GST_STATE_CHANGE_ASYNC) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                GstState current_state = GST_STATE_NULL;
                GstState pending = GST_STATE_VOID_PENDING;
                const GstStateChangeReturn get_ret = gst_element_get_state(pipeline, &current_state, &pending, 0);
                if (get_ret != GST_STATE_CHANGE_FAILURE) {
                    sf::info(encoder_cli_main, "Input pipeline state after async transition: %s (pending: %s)",
                             gst_element_state_get_name(current_state),
                             pending != GST_STATE_VOID_PENDING ? gst_element_state_get_name(pending) : "none");
                }
            }
            return true;
        }
    } else {
        state_ret = initiate_non_input_pipeline_state_change(pipeline, pipeline_name);
    }

    if (state_ret == GST_STATE_CHANGE_FAILURE) {
        return handle_state_change_failure(pipeline, pipeline_name);
    }

    const auto [get_ret, states] = wait_for_state_transition(pipeline, pipeline_name);
    const auto [current_state, pending] = states;

    if (get_ret == GST_STATE_CHANGE_FAILURE) {
        sf::error(encoder_cli_main, "Failed to get state for %s after setting to PLAYING", pipeline_name);
        return false;
    }

    if (current_state != GST_STATE_PLAYING) {
        return handle_not_fully_playing(pipeline, pipeline_name, current_state, pending);
    }

    sf::info(encoder_cli_main, "Pipeline %s successfully started and in PLAYING state", pipeline_name);
    return true;
}
