// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "roi_input_pipeline_internal.h"

#include "../../../encoder_enums.h"
#include "../camera_input.h"
#include "../common_input.h"
#include "../input_ingress_facade.h"
#include "../roi_input.h"
#include "../rtp_timestamp_overlay.h"
#include "../udp_input.h"

#include <vector>

#include "../../../../libvcai/include/logging.h"

GST_DEBUG_CATEGORY_EXTERN(roi_input);

namespace {

    [[nodiscard]] bool create_file_branch(input_ingress_facade &data, roi_input_elements &elements) {
        GstElement *parser = nullptr;
        if (!create_common_input_elements(data, elements.input_src, parser, elements.tee)) {
            sf::error(roi_input, "Failed to create common input elements for ROI detection pipeline");
            return false;
        }
        elements.input_filter = parser;
        return true;
    }

    void add_file_branch_to_pipeline(input_ingress_facade &data, const roi_input_elements &elements) {
        gst_bin_add_many(GST_BIN(data.input_pipeline.get()), elements.input_src, elements.input_filter, elements.tee,
                         nullptr);
    }

    [[nodiscard]] bool link_file_branch(input_ingress_facade &data, const roi_input_elements &elements,
                                        GstElement *nvvidconv) {
        (void) data;
        return gst_element_link_many(elements.input_src, elements.input_filter, elements.tee, nvvidconv, nullptr);
    }

    [[nodiscard]] bool create_camera_branch(input_ingress_facade &data, roi_input_elements &elements) {
        if (!create_camera_input_elements(data.params, elements.input_src, elements.queue, elements.input_filter,
                                          elements.tee)) {
            sf::error(roi_input, "Failed to create camera input elements for ROI detection pipeline");
            return false;
        }

        elements.jpegdec = gst_element_factory_make("jpegdec", "camera-jpeg-decoder");
        if (!elements.jpegdec) {
            sf::error(roi_input, "Failed to create software JPEG decoder element (jpegdec)");
            return false;
        }

        sf::info(roi_input, "Using software JPEG decoder (jpegdec) for camera input");
        elements.videoconvert = gst_element_factory_make("nvvideoconvert", "camera-video-convert");

        if (!elements.videoconvert) {
            sf::error(roi_input, "Camera video converter element could not be created.");
            return false;
        }
        return true;
    }

    void add_camera_branch_to_pipeline(input_ingress_facade &data, const roi_input_elements &elements) {
        gst_bin_add_many(GST_BIN(data.input_pipeline.get()), elements.input_src, elements.queue, elements.input_filter,
                         elements.jpegdec, elements.videoconvert, elements.tee, nullptr);
    }

    [[nodiscard]] bool link_camera_branch(input_ingress_facade &data, const roi_input_elements &elements,
                                          GstElement *nvvidconv) {
        (void) data;
        return gst_element_link_many(elements.input_src, elements.queue, elements.input_filter, elements.jpegdec,
                                     elements.videoconvert, elements.tee, nvvidconv, nullptr);
    }

    [[nodiscard]] bool create_udp_branch(input_ingress_facade &data, roi_input_elements &elements) {
        if (!create_udp_input_elements(data.params, elements.udp_elements)) {
            sf::error(roi_input, "Failed to create UDP input elements for ROI detection pipeline");
            return false;
        }
        elements.input_src = elements.udp_elements.source;
        elements.tee = elements.udp_elements.tee;
        return true;
    }

    void add_udp_branch_to_pipeline(input_ingress_facade &data, const roi_input_elements &elements) {
        gst_bin_add_many(GST_BIN(data.input_pipeline.get()), elements.udp_elements.source,
                         elements.udp_elements.rtp_capsfilter, elements.udp_elements.jitterbuffer,
                         elements.udp_elements.depay, elements.udp_elements.parser, nullptr);
        if (elements.udp_elements.capsfilter) {
            gst_bin_add(GST_BIN(data.input_pipeline.get()), elements.udp_elements.capsfilter);
        }
        gst_bin_add_many(GST_BIN(data.input_pipeline.get()), elements.udp_elements.queue_before_decoder,
                         elements.udp_elements.decoder, nullptr);
        if (elements.udp_elements.capsfilter_after_decoder) {
            gst_bin_add(GST_BIN(data.input_pipeline.get()), elements.udp_elements.capsfilter_after_decoder);
        }
        if (elements.udp_elements.system_converter) {
            gst_bin_add(GST_BIN(data.input_pipeline.get()), elements.udp_elements.system_converter);
        }
        gst_bin_add_many(GST_BIN(data.input_pipeline.get()), elements.udp_elements.gpu_converter, nullptr);
        if (elements.udp_elements.capsfilter_after_converter) {
            gst_bin_add(GST_BIN(data.input_pipeline.get()), elements.udp_elements.capsfilter_after_converter);
        }
        gst_bin_add_many(GST_BIN(data.input_pipeline.get()), elements.udp_elements.queue_before_tee,
                         elements.udp_elements.tee, nullptr);
    }

    [[nodiscard]] bool link_udp_branch(input_ingress_facade &data, const roi_input_elements &elements,
                                       GstElement *nvvidconv) {
        (void) data;
        std::vector<GstElement *> link_chain{elements.udp_elements.source, elements.udp_elements.rtp_capsfilter,
                                             elements.udp_elements.jitterbuffer, elements.udp_elements.depay,
                                             elements.udp_elements.parser};
        if (elements.udp_elements.capsfilter) {
            link_chain.push_back(elements.udp_elements.capsfilter);
        }
        link_chain.push_back(elements.udp_elements.queue_before_decoder);
        link_chain.push_back(elements.udp_elements.decoder);
        if (elements.udp_elements.capsfilter_after_decoder) {
            link_chain.push_back(elements.udp_elements.capsfilter_after_decoder);
        }
        if (elements.udp_elements.system_converter) {
            link_chain.push_back(elements.udp_elements.system_converter);
        }
        link_chain.push_back(elements.udp_elements.gpu_converter);
        if (elements.udp_elements.capsfilter_after_converter) {
            link_chain.push_back(elements.udp_elements.capsfilter_after_converter);
        }
        link_chain.push_back(elements.udp_elements.queue_before_tee);
        link_chain.push_back(elements.udp_elements.tee);
        link_chain.push_back(nvvidconv);

        for (size_t i = 0; i + 1 < link_chain.size(); ++i) {
            if (!gst_element_link(link_chain[i], link_chain[i + 1])) {
                sf::error(roi_input, "Failed to link UDP input elements: %s -> %s", GST_ELEMENT_NAME(link_chain[i]),
                          GST_ELEMENT_NAME(link_chain[i + 1]));
                return false;
            }
            sf::debug(roi_input, "Successfully linked UDP elements: %s -> %s", GST_ELEMENT_NAME(link_chain[i]),
                      GST_ELEMENT_NAME(link_chain[i + 1]));
        }

        if (elements.udp_elements.capsfilter) {
            sf::info(roi_input,
                     "UDP input pipeline linked successfully: udpsrc -> rtp-capsfilter -> rtpjitterbuffer -> depay "
                     "-> parser -> capsfilter -> queue -> decoder -> capsfilter -> converter -> capsfilter -> "
                     "queue -> tee -> nvvidconv");
        } else {
            sf::info(roi_input,
                     "UDP input pipeline linked successfully: udpsrc -> rtp-capsfilter -> rtpjitterbuffer -> depay "
                     "-> parser -> queue -> decoder -> capsfilter -> converter -> capsfilter -> queue -> tee -> "
                     "nvvidconv");
        }

        return true;
    }

    const roi_input_branch_factory k_file_factory = {create_file_branch, add_file_branch_to_pipeline, link_file_branch};

    const roi_input_branch_factory k_camera_factory = {create_camera_branch, add_camera_branch_to_pipeline,
                                                       link_camera_branch};

    const roi_input_branch_factory k_udp_factory = {create_udp_branch, add_udp_branch_to_pipeline, link_udp_branch};

} // namespace

const roi_input_branch_factory &get_roi_input_branch_factory(const input_type type) {
    switch (type) {
        case input_type::file:
            return k_file_factory;
        case input_type::camera:
            return k_camera_factory;
        case input_type::udp:
            return k_udp_factory;
        default:
            break;
    }
    static const roi_input_branch_factory k_invalid{};
    return k_invalid;
}

bool setup_udp_features_for_roi(input_ingress_facade &data, const udp_input_elements &udp_elements) {
    if (!setup_udp_packet_logging(udp_elements.source, data)) {
        sf::warning(roi_input, "Failed to setup UDP packet logging, continuing without it");
    } else {
        sf::info(roi_input, "UDP input pipeline ready: listening on %s:%u for %s RTP stream",
                 data.params.udp.resolved_bind_address().empty() ? "0.0.0.0"
                                                                 : data.params.udp.resolved_bind_address().c_str(),
                 data.params.udp.resolved_port(), data.params.udp.resolved_encoding().c_str());
    }

    if (!setup_udp_frame_tracking_probes(udp_elements, data)) {
        sf::warning(roi_input, "Failed to setup UDP frame tracking probes, continuing without them");
    }

    if (data.params.debug.draw_rtp_timestamp) {
        sf::info(roi_input,
                 "Setting up RTP timestamp extraction and attachment (jitterbuffer=%p, decoder=%p, "
                 "clock_rate=%u)",
                 udp_elements.jitterbuffer, udp_elements.decoder, data.params.udp.resolved_clock_rate());
        if (!setup_rtp_timestamp_extraction(udp_elements.jitterbuffer, data.params.udp.resolved_clock_rate())) {
            sf::warning(roi_input, "Failed to setup RTP timestamp extraction, continuing without it");
        }
        if (!setup_rtp_timestamp_attachment(udp_elements.decoder)) {
            sf::warning(roi_input, "Failed to setup RTP timestamp attachment, continuing without it");
        }
    } else {
        sf::debug(roi_input, "RTP timestamp overlay disabled (draw_rtp_timestamp=false)");
    }

    return true;
}
