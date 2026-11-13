// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../reporting/performance_monitor.h"
#include "../bypass_path.h"
#include "../input_ingress_facade.h"
#include "../roi_input.h"
#include "roi_input_internal.h"

#include <glib.h>
#include <gst/gst.h>

#include "../../../../libvcai/include/gst_raii.h"
#include "../../../../libvcai/include/logging.h"

GST_DEBUG_CATEGORY_EXTERN(roi_input);

namespace {
    /**
     * @brief Helper structure to pass stage, timing and the monitoring slice to pad probe callbacks.
     */
    struct probe_user_data {
        performance_monitor *perf_monitor;
        monitoring_stage stage;
        bool is_start;
    };

    /**
     * @brief Pad probe callback for performance monitoring.
     */
    GstPadProbeReturn inference_probe_callback([[maybe_unused]] GstPad *pad, GstPadProbeInfo *info,
                                               gpointer user_data) {
        if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER)) {
            return GST_PAD_PROBE_OK;
        }

        auto *probe_data = static_cast<probe_user_data *>(user_data);
        if (!probe_data || !probe_data->perf_monitor) {
            return GST_PAD_PROBE_OK;
        }

        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        if (buffer) {
            const GstClockTime pts = GST_BUFFER_PTS(buffer);
            if (pts != GST_CLOCK_TIME_NONE) {
                if (probe_data->is_start) {
                    probe_data->perf_monitor->record_start_time(probe_data->stage, pts);
                } else {
                    probe_data->perf_monitor->record_end_time(probe_data->stage, pts);
                }
            }
        }

        return GST_PAD_PROBE_OK;
    }
} // namespace

/**
 * @brief Sets up performance monitoring probes for inference stage and sub-stages.
 * @param data Narrow input/ingress view.
 * @param elements DeepStream elements.
 */
void setup_inference_performance_probes(input_ingress_facade &data, deepstream_elements &elements) {
    if (!data.monitoring.perf_monitor) {
        return;
    }

    performance_monitor *const perf_monitor = data.monitoring.perf_monitor.get();
    const auto add_inference_probe = [perf_monitor](GstPad *pad, const monitoring_stage stage, const bool is_start) {
        if (!pad) {
            return;
        }

        auto *probe_data = g_new(probe_user_data, 1);
        *probe_data = probe_user_data{perf_monitor, stage, is_start};
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, inference_probe_callback, probe_data,
                          [](gpointer user_data) { g_free(user_data); });
    };

    // 1. Total inference timing: valve_inference src pad (start) -> videoconvert_inference src pad (end)
    if (elements.valve_inference) {
        GstPad *valve_src = gst_element_get_static_pad(elements.valve_inference, "src");
        if (valve_src) {
            add_inference_probe(valve_src, monitoring_stage::inference, true);
            gst_object_unref(valve_src);
        }
    }

    if (elements.videoconvert_inference) {
        GstPad *inference_end = gst_element_get_static_pad(elements.videoconvert_inference, "src");
        if (inference_end) {
            add_inference_probe(inference_end, monitoring_stage::inference, false);
            gst_object_unref(inference_end);
        }
    }

    // 2. Streammux sub-stage: streammux sink_0 pad (start) -> streammux src pad (end)
    // Measure time spent INSIDE streammux processing
    if (elements.streammux) {
        constexpr gchar pad_name_sink[16] = "sink_0";
        GstPad *streammux_sink = gst_element_get_static_pad(elements.streammux, pad_name_sink);
        if (streammux_sink) {
            add_inference_probe(streammux_sink, monitoring_stage::inference_streammux, true);
            gst_object_unref(streammux_sink);
        }

        GstPad *streammux_src = gst_element_get_static_pad(elements.streammux, "src");
        if (streammux_src) {
            add_inference_probe(streammux_src, monitoring_stage::inference_streammux, false);
            gst_object_unref(streammux_src);
        }
    }

    // 3. Nvinfer sub-stage: streammux src pad (start) -> pgie src pad (end)
    if (elements.streammux && elements.pgie) {
        GstPad *streammux_src = gst_element_get_static_pad(elements.streammux, "src");
        if (streammux_src) {
            add_inference_probe(streammux_src, monitoring_stage::inference_nvinfer, true);
            gst_object_unref(streammux_src);
        }

        GstPad *pgie_src = gst_element_get_static_pad(elements.pgie, "src");
        if (pgie_src) {
            add_inference_probe(pgie_src, monitoring_stage::inference_nvinfer, false);
            gst_object_unref(pgie_src);
        }
    }

    // 4. Tracker sub-stage: pgie src pad (start) -> tracker src pad (end)
    if (elements.pgie && elements.tracker) {
        GstPad *pgie_src = gst_element_get_static_pad(elements.pgie, "src");
        if (pgie_src) {
            add_inference_probe(pgie_src, monitoring_stage::inference_tracker, true);
            gst_object_unref(pgie_src);
        }

        GstPad *tracker_src = gst_element_get_static_pad(elements.tracker, "src");
        if (tracker_src) {
            add_inference_probe(tracker_src, monitoring_stage::inference_tracker, false);
            gst_object_unref(tracker_src);
        }
    }

    // 5. Conversion sub-stage: tracker src pad (or pgie src pad if tracker disabled) -> nvvidconv2 src pad (end)
    // Measure GPU conversion time in nvvidconv2 (not the final appsink converter).
    if (elements.pgie && elements.nvvidconv2) {
        GstPad *conversion_start_pad = nullptr;
        if (elements.tracker) {
            // Tracker enabled: measure from tracker src pad
            conversion_start_pad = gst_element_get_static_pad(elements.tracker, "src");
        } else {
            // Tracker disabled: measure from pgie src pad
            conversion_start_pad = gst_element_get_static_pad(elements.pgie, "src");
        }
        if (conversion_start_pad) {
            add_inference_probe(conversion_start_pad, monitoring_stage::inference_conversion, true);
            gst_object_unref(conversion_start_pad);
        }

        GstPad *nvvidconv2_src = gst_element_get_static_pad(elements.nvvidconv2, "src");
        if (nvvidconv2_src) {
            add_inference_probe(nvvidconv2_src, monitoring_stage::inference_conversion, false);
            gst_object_unref(nvvidconv2_src);
        }
    }
}
bool link_deepstream_elements(input_ingress_facade &data, deepstream_elements &elements, GstElement *tee) {
    // Link nvvidconv → valve_inference → nvvidconv_pre_inference (if bypass enabled)
    // Otherwise link nvvidconv → nvvidconv_pre_inference directly
    if (data.params.performance.enable_deepstream_bypass && elements.valve_inference) {
        // Link: nvvidconv -> valve_inference -> nvvidconv_pre_inference
        if (!gst_element_link(elements.nvvidconv, elements.valve_inference)) {
            sf::error(roi_input, "Failed to link nvvidconv to inference valve. Exiting.");
            return false;
        }
        if (!gst_element_link(elements.valve_inference, elements.nvvidconv_pre_inference)) {
            sf::error(roi_input, "Failed to link inference valve to nvvidconv_pre_inference. Exiting.");
            return false;
        }

        // Link bypass path internal elements
        if (!link_bypass_path_elements(elements.bypass)) {
            sf::error(roi_input, "Failed to link bypass path elements");
            return false;
        }

        // Link tee to bypass path
        if (tee) {
            if (!link_tee_to_bypass_path(tee, elements.bypass)) {
                sf::error(roi_input, "Failed to link tee to bypass path");
                return false;
            }
        } else {
            sf::error(roi_input, "Tee element is null but bypass is enabled");
            return false;
        }
    } else {
        // Original linking without bypass: nvvidconv -> nvvidconv_pre_inference
        if (!gst_element_link(elements.nvvidconv, elements.nvvidconv_pre_inference)) {
            sf::error(roi_input, "Failed to link nvvidconv to nvvidconv_pre_inference. Exiting.");
            return false;
        }
    }

    // Link nvvidconv_pre_inference to streammux
    constexpr gchar pad_name_sink[16] = "sink_0";
    const sf::unique_gst_pad_ptr sinkpad(gst_element_request_pad_simple(elements.streammux, pad_name_sink));
    if (!sinkpad) {
        sf::error(roi_input, "Streammux request sink pad failed. Exiting.");
        return false;
    }

    const sf::unique_gst_pad_ptr srcpad(gst_element_get_static_pad(elements.nvvidconv_pre_inference, "src"));
    if (!srcpad) {
        sf::error(roi_input, "nvvidconv_pre_inference request src pad failed. Exiting.");
        return false;
    }

    if (gst_pad_link(srcpad.get(), sinkpad.get()) != GST_PAD_LINK_OK) {
        sf::error(roi_input, "Failed to link nvvidconv_pre_inference to stream muxer. Exiting.");
        return false;
    }

    // Add pad probe to preserve PTS before streammux (which resets PTS)
    // Store original PTS only for inference frames, then restore it after inference path
    if (data.params.performance.enable_deepstream_bypass && elements.valve_inference) {
        if (!setup_pts_preservation_probes(data, elements)) {
            sf::warning(roi_input, "Failed to setup PTS preservation probes, frames may be out of order");
        }
    } else if (elements.valve_inference && data.monitoring.perf_monitor) {
        // Setup performance monitoring probes even if bypass is disabled
        setup_inference_performance_probes(data, elements);
    }

    // Link the main DeepStream pipeline based on whether inference engine is enabled
    // Add capsfilter and videoconvert to convert NVMM to system memory before appsink
    if (elements.pgie && elements.tracker) {
        // Full inference pipeline with tracker: streammux -> pgie -> tracker -> nvvidconv2 -> capsfilter_inference ->
        // videoconvert_inference -> appsink
        sf::info(roi_input, "Linking inference pipeline with tracker");
        return gst_element_link_many(elements.streammux, elements.pgie, elements.tracker, elements.nvvidconv2,
                                     elements.capsfilter_inference, elements.videoconvert_inference, elements.appsink,
                                     nullptr);
    }

    if (elements.pgie && !elements.tracker) {
        // Inference pipeline without tracker: streammux -> pgie -> nvvidconv2 -> capsfilter_inference ->
        // videoconvert_inference -> appsink
        sf::info(roi_input,
                 "Linking inference pipeline without tracker (use roi_accumulation_mode=overlap for ROI matching)");
        return gst_element_link_many(elements.streammux, elements.pgie, elements.nvvidconv2,
                                     elements.capsfilter_inference, elements.videoconvert_inference, elements.appsink,
                                     nullptr);
    }

    // Simplified pipeline without inference: streammux -> nvvidconv2 -> capsfilter_inference -> videoconvert_inference
    // -> appsink
    sf::info(roi_input, "Linking simplified pipeline without inference elements");
    return gst_element_link_many(elements.streammux, elements.nvvidconv2, elements.capsfilter_inference,
                                 elements.videoconvert_inference, elements.appsink, nullptr);
}
void add_deepstream_elements_to_pipeline(const input_ingress_facade &data, deepstream_elements &elements,
                                         GstElement *queue_original, GstElement *converter_original,
                                         GstElement *encoder_original, GstElement *sink_original) {
    // Add core elements
    gst_bin_add_many(GST_BIN(data.input_pipeline.get()), elements.nvvidconv, elements.nvvidconv_pre_inference,
                     elements.streammux, elements.nvvidconv2, elements.capsfilter_inference,
                     elements.videoconvert_inference, elements.appsink, nullptr);

    // Add inference elements only if they exist (inference engine enabled)
    if (elements.pgie) {
        gst_bin_add(GST_BIN(data.input_pipeline.get()), elements.pgie);
    }
    if (elements.tracker) {
        gst_bin_add(GST_BIN(data.input_pipeline.get()), elements.tracker);
    }

    // Add bypass path elements if bypass feature is enabled
    if (data.params.performance.enable_deepstream_bypass) {
        gst_bin_add(GST_BIN(data.input_pipeline.get()), elements.valve_inference);
        if (!add_bypass_path_to_pipeline(data.input_pipeline.get(), elements.bypass)) {
            sf::error(roi_input, "Failed to add bypass path to pipeline");
        }
    }

    if (data.params.debug.save_frames) {
        gst_bin_add_many(GST_BIN(data.input_pipeline.get()), queue_original, converter_original, encoder_original,
                         sink_original, nullptr);
    }
}
