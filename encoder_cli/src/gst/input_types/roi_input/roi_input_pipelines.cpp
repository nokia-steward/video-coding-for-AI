// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../encoder_enums.h"
#include "../../output_pipeline/paths.h"
#include "../bypass_path.h"
#include "../common_input.h"
#include "../frame_merger.h"
#include "../frame_router.h"
#include "../input_ingress_facade.h"
#include "../roi_input.h"
#include "roi_input_pipeline_internal.h"

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include "../../../../libvcai/include/logging.h"

GST_DEBUG_CATEGORY_EXTERN(roi_input);

/**
 * @brief Creates the ROI info input pipeline.
 * @param data Global data structure to populate with elements.
 * @return True if the pipeline was created successfully.
 */
bool create_roi_info_input_pipeline(global_data &data) {
    input_ingress_facade facade(data);
    GstElement *filesrc, *parser, *tee;

    if (!create_common_input_elements(facade, filesrc, parser, tee)) {
        sf::error(roi_input, "Failed to create common input elements for ROI info pipeline");
        return false;
    }

    input_sink_branch_elements sink_elements{};
    if (!create_input_sink_branch_elements(facade, "roi-data-sink", "ROI info pipeline", sink_elements)) {
        sf::error(roi_input, "Failed to initialize sink elements for ROI info pipeline");
        return false;
    }

    gst_bin_add_many(GST_BIN(facade.input_pipeline.get()), filesrc, parser, tee, sink_elements.appsink, nullptr);
    if (facade.params.debug.save_frames) {
        gst_bin_add_many(GST_BIN(facade.input_pipeline.get()), sink_elements.queue_original,
                         sink_elements.converter_original, sink_elements.encoder_original, sink_elements.sink_original,
                         nullptr);
    }

    bool result = gst_element_link_many(filesrc, parser, tee, sink_elements.appsink, nullptr);
    if (!result) {
        sf::error(roi_input, "Failed to link ROI info pipeline elements: filesrc -> parser -> tee -> appsink");
        return false;
    }
    result = link_original_frame_saving_branch(tee, sink_elements.queue_original, sink_elements.converter_original,
                                               sink_elements.encoder_original, sink_elements.sink_original);
    if (!result) {
        sf::error(roi_input, "Failed to link original frame saving branch in ROI info pipeline");
        return false;
    }

    return result;
}

/**
 * @brief Creates the ROI detection input pipeline.
 * @param data Global data structure to populate with elements.
 * @return True if the pipeline was created successfully.
 */
bool create_roi_detection_input_pipeline(global_data &data) {
    input_ingress_facade facade(data);
    const roi_input_branch_factory &branch = get_roi_input_branch_factory(facade.params.input_type_value);
    if (!branch.create || !branch.add_to_pipeline || !branch.link) {
        sf::error(roi_input, "Unsupported input type '%s' for ROI detection pipeline",
                  input_type_to_string(facade.params.input_type_value));
        return false;
    }

    roi_input_elements input_elements{};
    if (!branch.create(facade, input_elements)) {
        return false;
    }

    deepstream_elements elements;
    if (!create_deepstream_elements(elements, facade.params)) {
        sf::error(roi_input, "DeepStream elements could not be created.");
        return false;
    }

    if (!configure_deepstream_elements(facade, elements)) {
        sf::error(roi_input, "Failed to configure DeepStream elements for ROI detection pipeline");
        return false;
    }

    if (!configure_tracker(facade.params, elements.tracker)) {
        sf::error(roi_input, "Failed to configure tracker for ROI detection pipeline");
        return false;
    }

    configure_nvvideoconvert_elements(elements, facade);

    if (!facade.params.performance.enable_deepstream_bypass) {
        if (!configure_appsink(elements.appsink, facade)) {
            sf::error(roi_input, "Failed to configure appsink for ROI detection pipeline");
            return false;
        }
    }

    GstElement *queue_original = nullptr, *converter_original = nullptr, *encoder_original = nullptr,
               *sink_original = nullptr;
    if (const std::string model_name = determine_model_name(); !create_original_frame_saving_elements(
                facade.params, model_name, queue_original, converter_original, encoder_original, sink_original)) {
        sf::error(roi_input, "Failed to create original frame saving elements for ROI detection pipeline");
        return false;
    }

    branch.add_to_pipeline(facade, input_elements);

    add_deepstream_elements_to_pipeline(facade, elements, queue_original, converter_original, encoder_original,
                                        sink_original);

    if (!branch.link(facade, input_elements, elements.nvvidconv)) {
        sf::error(roi_input, "Failed to link input elements to DeepStream pipeline");
        return false;
    }

    if (facade.params.input_type_value == input_type::udp) {
        if (!setup_udp_features_for_roi(facade, input_elements.udp_elements)) {
            sf::warning(roi_input, "Some UDP features failed to setup, continuing");
        }
    }

    bool result = link_deepstream_elements(facade, elements, input_elements.tee);
    if (!result) {
        sf::error(roi_input, "Failed to link DeepStream elements");
        return false;
    }
    result = link_original_frame_saving_branch(input_elements.tee, queue_original, converter_original, encoder_original,
                                               sink_original);
    if (!result) {
        sf::error(roi_input, "Failed to link original frame saving branch in ROI detection pipeline");
        return false;
    }

    if (facade.params.performance.enable_deepstream_bypass) {
        frame_merger_config merger_config;
        merger_config.max_buffer_frames = facade.params.performance.merger_buffer_size;
        merger_config.max_latency_ms = facade.params.performance.merger_max_latency_ms;
        merger_config.emergency_drop_threshold = merger_config.max_buffer_frames + 5;

        facade.bypass.merger = std::make_unique<frame_merger>(merger_config);
        facade.bypass.appsink_eos_count.store(0);

        sf::info(roi_input, "Frame merger initialized: buffer_size=%zu, max_latency=%ums",
                 merger_config.max_buffer_frames, merger_config.max_latency_ms);

        facade.bypass.router = std::make_unique<frame_router>(facade.params.performance.bypass_interval);

        facade.bypass.routing_ctx = std::make_unique<routing_probe_context>();
        facade.bypass.routing_ctx->bypass = &facade.bypass;
        facade.bypass.routing_ctx->router = facade.bypass.router.get();

        facade.bypass.routing_probe_id = install_routing_probe(input_elements.tee, facade.bypass.routing_ctx.get());

        if (facade.bypass.routing_probe_id == 0) {
            sf::error(roi_input, "Failed to install routing probe");
            return false;
        }

        sf::info(roi_input, "Bypass routing initialized: bypass_interval=%d",
                 facade.params.performance.bypass_interval);
    }

    return result;
}
