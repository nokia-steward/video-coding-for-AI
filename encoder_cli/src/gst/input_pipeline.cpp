// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "input_pipeline.h"
#include "../application/input_modes/input_mode_factory.h"
#include "../reporting/performance_monitor.h"
#include "input_pipeline_debug.h"
#include "input_types/camera_input.h"
#include "input_types/file_input.h"
#include "input_types/roi_input.h"

#include <algorithm>
#include <cstdint>
#include <glib.h>
#include <gst/gst.h>
#include <gst/gstregistry.h>
#include <optional>
#include "../../libvcai/include/area_copy.h"
#include "../../libvcai/include/logging.h"
#include "../encoder_enums.h"

/**
 * @file input_pipeline.cpp
 * @brief Implementation of input pipeline creation and management functions.
 */

/// GStreamer debug category for input pipeline management (non-static for multi-TU use).
GST_DEBUG_CATEGORY(input_pipeline);

void init_input_pipeline_debug_category() {
    GST_DEBUG_CATEGORY_INIT(input_pipeline, "input-pipeline", 0, "Input pipeline creation and management operations");
}

namespace {
    /**
     * @brief Sets up pad probes for input decoding performance monitoring.
     * @param data The global data structure.
     */
    void setup_input_decoding_performance_probes(global_data &data) {
        if (!data.input_pipeline || !data.monitoring.perf_monitor) {
            return;
        }

        // For UDP input, monitor decoder-only latency (RTP packetization breaks PTS matching)
        if (data.params.input_type_value == input_type::udp) {
            // Find the decoder element (named "udp-hw-decoder")
            GstElement *decoder = gst_bin_get_by_name(GST_BIN(data.input_pipeline.get()), "udp-hw-decoder");
            if (!decoder) {
                sf::warning(input_pipeline,
                            "Could not find decoder element for UDP input decoding performance monitoring");
                return;
            }
            sf::info(input_pipeline, "Found decoder element for UDP input decoding monitoring: %s",
                     GST_ELEMENT_NAME(decoder));

            // Add probe on decoder sink pad (start timing for input decoding)
            GstPad *decoder_sink = gst_element_get_static_pad(decoder, "sink");
            if (decoder_sink) {
                gst_pad_add_probe(
                        decoder_sink, GST_PAD_PROBE_TYPE_BUFFER,
                        []([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info,
                           gpointer user_data) -> GstPadProbeReturn {
                            if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
                                auto *global_data_ptr = static_cast<global_data *>(user_data);
                                if (global_data_ptr && global_data_ptr->monitoring.perf_monitor) {
                                    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                                    if (buffer) {
                                        const GstClockTime pts = GST_BUFFER_PTS(buffer);
                                        // Record start time for input decoding using actual PTS
                                        if (pts != GST_CLOCK_TIME_NONE) {
                                            global_data_ptr->monitoring.perf_monitor->record_start_time(
                                                    monitoring_stage::input_decoding, pts);
                                        }
                                    }
                                }
                            }
                            return GST_PAD_PROBE_OK;
                        },
                        &data, nullptr);
                gst_object_unref(decoder_sink);
            }

            // Add probe on decoder src pad (end timing for input decoding)
            GstPad *decoder_src = gst_element_get_static_pad(decoder, "src");
            if (decoder_src) {
                gst_pad_add_probe(
                        decoder_src, GST_PAD_PROBE_TYPE_BUFFER,
                        []([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info,
                           gpointer user_data) -> GstPadProbeReturn {
                            if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
                                auto *global_data_ptr = static_cast<global_data *>(user_data);
                                if (global_data_ptr && global_data_ptr->monitoring.perf_monitor) {
                                    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                                    if (buffer) {
                                        const GstClockTime pts = GST_BUFFER_PTS(buffer);
                                        if (pts != GST_CLOCK_TIME_NONE) {
                                            // Record end time for input decoding (PTS-based matching)
                                            global_data_ptr->monitoring.perf_monitor->record_end_time(
                                                    monitoring_stage::input_decoding, pts);
                                            // Record START time for total pipeline (same PTS will be at output)
                                            global_data_ptr->monitoring.perf_monitor->record_start_time(
                                                    monitoring_stage::total_pipeline, pts);
                                        }
                                    }
                                }
                            }
                            return GST_PAD_PROBE_OK;
                        },
                        &data, nullptr);
                gst_object_unref(decoder_src);
            }

            gst_object_unref(decoder);
            return;
        }

        // For file/camera input, monitor from source to appsink
        // Find the source element (filesrc/v4l2src)
        GstElement *source_element = nullptr;
        const char *source_names[] = {"file-source", "camera-source"};
        for (const char *name: source_names) {
            source_element = gst_bin_get_by_name(GST_BIN(data.input_pipeline.get()), name);
            if (source_element) {
                break;
            }
        }

        if (!source_element) {
            sf::warning(input_pipeline, "Could not find input source element for performance monitoring");
            return;
        }
        sf::info(input_pipeline, "Found source element for performance monitoring: %s",
                 GST_ELEMENT_NAME(source_element));

        // Add probe on source element src pad (start timing for input decoding and total pipeline)
        GstPad *source_src = gst_element_get_static_pad(source_element, "src");
        if (source_src) {
            gst_pad_add_probe(
                    source_src, GST_PAD_PROBE_TYPE_BUFFER,
                    []([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
                            auto *global_data_ptr = static_cast<global_data *>(user_data);
                            if (global_data_ptr && global_data_ptr->monitoring.perf_monitor) {
                                GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                                if (buffer) {
                                    const GstClockTime pts = GST_BUFFER_PTS(buffer);
                                    // Record start time for input decoding using actual PTS
                                    if (pts != GST_CLOCK_TIME_NONE) {
                                        global_data_ptr->monitoring.perf_monitor->record_start_time(
                                                monitoring_stage::input_decoding, pts);
                                    }
                                }
                            }
                        }
                        return GST_PAD_PROBE_OK;
                    },
                    &data, nullptr);
            gst_object_unref(source_src);
        }

        gst_object_unref(source_element);

        // Find the appsink element (name varies by input type)
        // For file/camera input: "input-sink"
        // For ROI input mode: "roi-detection-sink"
        GstElement *appsink = gst_bin_get_by_name(GST_BIN(data.input_pipeline.get()), "input-sink");
        if (!appsink) {
            appsink = gst_bin_get_by_name(GST_BIN(data.input_pipeline.get()), "roi-detection-sink");
        }
        if (!appsink) {
            sf::warning(input_pipeline, "Could not find appsink element for input decoding performance monitoring");
            return;
        }

        // Add probe on appsink sink pad (end timing for input decoding)
        GstPad *appsink_sink = gst_element_get_static_pad(appsink, "sink");
        if (appsink_sink) {
            gst_pad_add_probe(
                    appsink_sink, GST_PAD_PROBE_TYPE_BUFFER,
                    []([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info, gpointer user_data) -> GstPadProbeReturn {
                        if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER) {
                            auto *global_data_ptr = static_cast<global_data *>(user_data);
                            if (global_data_ptr && global_data_ptr->monitoring.perf_monitor) {
                                GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
                                if (buffer) {
                                    const GstClockTime pts = GST_BUFFER_PTS(buffer);
                                    if (pts != GST_CLOCK_TIME_NONE) {
                                        // Record end time for input decoding (PTS-based matching)
                                        global_data_ptr->monitoring.perf_monitor->record_end_time(
                                                monitoring_stage::input_decoding, pts);
                                        // Also record total pipeline start here (PTS is now consistent)
                                        global_data_ptr->monitoring.perf_monitor->record_start_time(
                                                monitoring_stage::total_pipeline, pts);
                                    }
                                }
                            }
                        }
                        return GST_PAD_PROBE_OK;
                    },
                    &data, nullptr);
            gst_object_unref(appsink_sink);
        }

        gst_object_unref(appsink);
    }
} // namespace

/**
 * @brief Builds the input pipeline based on configuration.
 * @param data Global data structure to populate with elements.
 * @return True if the pipeline was created successfully.
 */
bool create_input_pipeline(global_data &data) {
    sf::debug(input_pipeline, "Creating input pipeline for input_type='%s', inference_engine=%s",
              input_type_to_string(data.params.input_type_value), data.params.inference_engine ? "true" : "false");

    // Verify GStreamer is initialized, reinitialize if needed
    if (!gst_is_initialized()) {
        sf::warning(input_pipeline, "GStreamer is not initialized. Attempting to initialize...");
        gst_init(nullptr, nullptr);

        if (!gst_is_initialized()) {
            sf::error(input_pipeline, "GStreamer initialization failed. Cannot create input pipeline.");
            return false;
        }
        sf::info(input_pipeline, "GStreamer initialized successfully");
    }

    // Update registry to ensure plugins are available
    GstRegistry *registry = gst_registry_get();
    if (registry) {
        gst_update_registry();
    }

    data.input_pipeline.reset(gst_pipeline_new("input-pipeline"));
    if (!data.input_pipeline.get()) {
        sf::error(input_pipeline,
                  "Input pipeline could not be created. gst_pipeline_new returned nullptr - this should never happen "
                  "if GStreamer is properly initialized.");

        // Enhanced diagnostics
        registry = gst_registry_get();
        if (registry) {
            GList *plugins = gst_registry_get_plugin_list(registry);
            if (plugins) {
                g_list_free(plugins);
            }

            // Check if pipeline plugin is available
            GstPlugin *pipeline_plugin = gst_registry_find_plugin(registry, "coreelements");
            if (pipeline_plugin) {
                gst_object_unref(pipeline_plugin);
            }
        }

        // Try to update registry
        gst_update_registry();

        // Try creating pipeline again after registry update
        data.input_pipeline.reset(gst_pipeline_new("input-pipeline"));
        if (!data.input_pipeline.get()) {
            return false;
        }
    }

    const auto pipeline_creator = select_input_pipeline_creator(data);

    sf::debug(input_pipeline, "Calling pipeline creator function for input_type='%s'",
              input_type_to_string(data.params.input_type_value));
    const bool success = pipeline_creator(data);

    if (!success) {
        sf::error(input_pipeline, "Input pipeline creation failed for input_type='%s', inference_engine=%s",
                  input_type_to_string(data.params.input_type_value), data.params.inference_engine ? "true" : "false");
    } else {
        sf::debug(input_pipeline, "Input pipeline created successfully for input_type='%s'",
                  input_type_to_string(data.params.input_type_value));
    }

    if (data.monitoring.status_reporter) {
        data.monitoring.status_reporter->report_pipeline_status("input",
                                                                success ? "created successfully" : "creation failed");
    }

    // Set up input decoding performance probes if monitoring is enabled
    if (success && data.monitoring.perf_monitor) {
        setup_input_decoding_performance_probes(data);
    }

    return success;
}

std::function<bool(global_data &)> select_input_pipeline_creator(const global_data &data) {
    // Create and store contexts vector to ensure it lives long enough
    const auto contexts = create_input_mode_contexts();
    const auto *active_mode = select_active_input_mode(data, contexts);

    if (!active_mode) {
        sf::error(input_pipeline, "No input mode matched configuration, defaulting to ROI detection pipeline");
        return create_roi_detection_input_pipeline;
    }

    const auto creator = active_mode->functions.get_pipeline_creator(data);
    if (creator.has_value()) {
        sf::debug(input_pipeline, "Selected pipeline creator for input mode: %s",
                  active_mode->functions.get_mode_name());
        // Return the function - contexts vector is still alive here, and the returned function
        // doesn't depend on the vector anymore (it's a copy of the std::function)
        return creator.value();
    }

    // Fallback to ROI detection pipeline
    sf::warning(input_pipeline, "Input mode %s did not provide pipeline creator, defaulting to ROI detection pipeline",
                active_mode->functions.get_mode_name());
    return create_roi_detection_input_pipeline;
}
