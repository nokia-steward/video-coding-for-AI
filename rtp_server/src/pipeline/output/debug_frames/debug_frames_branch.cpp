#include "debug_frames_branch.h"

#include "../../../gst/gpu_elements.h"
#include "../../pipeline.h"
#include "../../pipeline/utils.h"
#include "logging.h"

#include <cstdlib>
#include <filesystem>
#include <gst/gst.h>
#include <string>

GST_DEBUG_CATEGORY_STATIC(debug_frames_branch_category);
#define GST_CAT_DEFAULT debug_frames_branch_category

namespace {
    [[nodiscard]] bool use_pre_osd_capture_for_ah0187_semseg();

    [[nodiscard]] bool is_branch_enabled(const rtp_pipeline *pipeline) {
        return pipeline != nullptr && pipeline->server_config != nullptr && pipeline->server_config->save_frames;
    }

    [[nodiscard]] guint resolve_debug_queue_max_buffers() {
        constexpr guint default_max_size_buffers = 120;
        constexpr guint ah0187_max_size_buffers = 512;
        return use_pre_osd_capture_for_ah0187_semseg() ? ah0187_max_size_buffers : default_max_size_buffers;
    }

    void configure_queue(GstElement *queue) {
        const guint max_size_buffers = resolve_debug_queue_max_buffers();
        constexpr guint max_size_bytes = 0;
        constexpr guint64 max_size_time = 0;
        g_object_set(queue, "max-size-buffers", max_size_buffers, "max-size-bytes", max_size_bytes, "max-size-time",
                     max_size_time, nullptr);
    }

    [[nodiscard]] bool prepare_debug_directory(const rtp_pipeline &pipeline, const std::filesystem::path &debug_dir) {
        try {
            if (std::filesystem::exists(debug_dir)) {
                for (const auto &entry: std::filesystem::directory_iterator(debug_dir)) {
                    if (std::filesystem::is_regular_file(entry.path())) {
                        std::filesystem::remove(entry.path());
                    }
                }
            }
            std::filesystem::create_directories(debug_dir);
            sf::info(debug_frames_branch_category, "Debug frames directory ready for port %u: %s", pipeline.get_port(),
                     debug_dir.c_str());
            return true;
        } catch (const std::filesystem::filesystem_error &error) {
            sf::error(debug_frames_branch_category, "Failed to prepare debug frame directory for port %u: %s",
                      pipeline.get_port(), error.what());
            return false;
        }
    }

    [[nodiscard]] bool use_pre_osd_capture_for_ah0187_semseg() {
        const char *flag = std::getenv("SF_TET_COMPARISON_SEMSEG_MODE");
        if (flag == nullptr) {
            return false;
        }
        const std::string value(flag);
        return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
    }
} // namespace

output_branch_functions create_debug_frames_branch_functions() {
    GST_DEBUG_CATEGORY_INIT(debug_frames_branch_category, "debug-frames-branch", 0, "Debug frames output branch");

    return {.is_enabled = [](const rtp_pipeline *pipeline) { return is_branch_enabled(pipeline); },
            .create_elements = [](rtp_pipeline *pipeline) -> bool {
                if (!is_branch_enabled(pipeline)) {
                    return true;
                }

                pipeline->queue_debug_frames =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("queue", "q-debug-frames"));
                // debug_frames_download is nvvideoconvert (NVMM -> system) when tee feeds NVMM. If NVMM plugins
                // are unavailable or upstream is already system memory (e.g. software decode path), fall back
                // to videoconvert so the branch can still link.
                try {
                    // RAII_EXEMPT: GstBin takes ownership of this raw GstElement pointer.
                    pipeline->debug_frames_download = sf::unique_gst_element_no_delete_ptr(
                            create_gpu_to_cpu_download("debug-frames-download").release());
                } catch (const std::exception &) {
                    pipeline->debug_frames_download = sf::unique_gst_element_no_delete_ptr(
                            gst_element_factory_make("videoconvert", "debug-frames-download"));
                }
                pipeline->debug_frames_convert = sf::unique_gst_element_no_delete_ptr(
                        gst_element_factory_make("videoconvert", "debug-frames-convert"));
                pipeline->debug_frames_png_encoder =
                        sf::unique_gst_element_no_delete_ptr(gst_element_factory_make("pngenc", "debug-frames-pngenc"));
                pipeline->debug_frames_sink = sf::unique_gst_element_no_delete_ptr(
                        gst_element_factory_make("multifilesink", "debug-frames-sink"));

                if (!pipeline->queue_debug_frames || !pipeline->debug_frames_download ||
                    !pipeline->debug_frames_convert || !pipeline->debug_frames_png_encoder ||
                    !pipeline->debug_frames_sink) {
                    sf::error(debug_frames_branch_category, "Failed to create debug frame elements for port %u",
                              pipeline->get_port());
                    return false;
                }

                return true;
            },
            .configure_elements = [](rtp_pipeline *pipeline) -> bool {
                if (!is_branch_enabled(pipeline)) {
                    return true;
                }

                configure_queue(pipeline->queue_debug_frames.get());
                sf::info(debug_frames_branch_category, "Debug frames queue max-size-buffers=%u on port %u",
                         resolve_debug_queue_max_buffers(), pipeline->get_port());
                g_object_set(pipeline->debug_frames_sink.get(), "async", FALSE, "max-files", 0, nullptr);

                const std::filesystem::path debug_dir = get_debug_frames_port_directory(*pipeline);
                if (!prepare_debug_directory(*pipeline, debug_dir)) {
                    return false;
                }

                const std::filesystem::path location_pattern = debug_dir / "frame_%06d.png";
                const std::string location_string = location_pattern.string();
                g_object_set(pipeline->debug_frames_sink.get(), "location", location_string.c_str(), nullptr);

                return true;
            },
            .link_branch = [](rtp_pipeline *pipeline) -> bool {
                if (!is_branch_enabled(pipeline)) {
                    return true;
                }

                return gst_element_link_many(pipeline->queue_debug_frames.get(), pipeline->debug_frames_download.get(),
                                             pipeline->debug_frames_convert.get(),
                                             pipeline->debug_frames_png_encoder.get(),
                                             pipeline->debug_frames_sink.get(), nullptr);
            },
            .link_to_tee = [](rtp_pipeline *pipeline, GstElement *tee_element) -> bool {
                if (!is_branch_enabled(pipeline)) {
                    return true;
                }

                GstElement *source_tee = tee_element;
                if (pipeline->post_osd_tee && !use_pre_osd_capture_for_ah0187_semseg()) {
                    // Capture after nvosd so saved PNGs include display overlays/borders,
                    // while avoiding PRI-based drawing in the debug branch.
                    source_tee = pipeline->post_osd_tee.get();
                } else if (pipeline->post_osd_tee && use_pre_osd_capture_for_ah0187_semseg()) {
                    sf::info(debug_frames_branch_category,
                             "AH0187 semseg mode: using pre-OSD tee for clean frame capture on port %u",
                             pipeline->get_port());
                }
                if (!source_tee) {
                    sf::error(debug_frames_branch_category, "No source tee available for debug frames on %u",
                              pipeline->get_port());
                    return false;
                }

                const sf::unique_gst_pad_ptr tee_src_pad(gst_element_request_pad_simple(source_tee, "src_%u"));
                const sf::unique_gst_pad_ptr sink_pad(
                        gst_element_get_static_pad(pipeline->queue_debug_frames.get(), "sink"));
                if (!tee_src_pad || !sink_pad) {
                    sf::error(debug_frames_branch_category, "Failed to get tee/link pads for debug frames branch on %u",
                              pipeline->get_port());
                    return false;
                }

                return gst_pad_link(tee_src_pad.get(), sink_pad.get()) == GST_PAD_LINK_OK;
            },
            .add_to_pipeline = [](rtp_pipeline *pipeline, GstElement *output_pipeline) -> bool {
                if (!is_branch_enabled(pipeline)) {
                    return true;
                }

                gst_bin_add_many(GST_BIN(output_pipeline), pipeline->queue_debug_frames.get(),
                                 pipeline->debug_frames_download.get(), pipeline->debug_frames_convert.get(),
                                 pipeline->debug_frames_png_encoder.get(), pipeline->debug_frames_sink.get(), nullptr);
                return true;
            },
            .cleanup = [](rtp_pipeline *pipeline) { (void) pipeline; },
            .get_branch_name = []() { return "debug-frames"; }};
}

output_branch_context create_debug_frames_branch_context() {
    return {.functions = create_debug_frames_branch_functions()};
}
