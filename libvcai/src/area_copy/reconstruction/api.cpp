// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/packed_regions_info.h"
#include "../nv12/reconstruction.h"
#include "../yuv420/reconstruction.h"
#include "pri_extraction.h"

#include <gst/video/video-info.h>
#include <gst/video/video.h>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        [[nodiscard]] std::optional<GstVideoInfo> get_input_video_info(const unique_gst_sample_ptr &packed_sample) {
            auto *const caps_in = gst_sample_get_caps(packed_sample.get());
            if (!caps_in) {
                error(area_copy, "Input sample is missing caps.");
                return std::nullopt;
            }

            GstVideoInfo video_info_in;
            if (!gst_video_info_from_caps(&video_info_in, caps_in)) {
                error(area_copy, "Failed to parse video info from input caps.");
                return std::nullopt;
            }

            return video_info_in;
        }

        [[nodiscard]] std::optional<GstVideoInfo> create_output_video_info(const packed_regions_info &pri_info,
                                                                           const GstVideoFormat input_format) {
            if (!pri_info.target_pic_width_minus1 || !pri_info.target_pic_height_minus1) {
                error(area_copy, "PRI message is missing target picture dimensions.");
                return std::nullopt;
            }

            const auto reconstructed_width = *pri_info.target_pic_width_minus1 + 1;
            const auto reconstructed_height = *pri_info.target_pic_height_minus1 + 1;

            GstVideoInfo video_info_out;
            gst_video_info_init(&video_info_out);
            if (!gst_video_info_set_format(&video_info_out, input_format, static_cast<guint>(reconstructed_width),
                                           static_cast<guint>(reconstructed_height))) {
                error(area_copy, "Failed to set output video info format.");
                return std::nullopt;
            }

            return video_info_out;
        }

        [[nodiscard]] std::optional<video_format> to_area_copy_format(const GstVideoFormat format) {
            if (format == GST_VIDEO_FORMAT_NV12) {
                return video_format::nv12;
            }
            if (format == GST_VIDEO_FORMAT_I420) {
                return video_format::i420;
            }

            const char *format_string = gst_video_format_to_string(format);
            if (!format_string) {
                error(area_copy, "Failed to get format string for video format.");
                return std::nullopt;
            }
            return string_to_video_format(format_string);
        }

        [[nodiscard]] std::optional<unique_gst_buffer_ptr>
        create_empty_output_buffer(const GstVideoInfo &video_info_out, const background_color background_color) {
            const auto format_opt = to_area_copy_format(video_info_out.finfo->format);
            if (!format_opt) {
                return std::nullopt;
            }

            auto buffer_out =
                    create_empty_buffer(video_info_out.width, video_info_out.height, *format_opt, background_color);
            if (!buffer_out) {
                error(area_copy, "Failed to create empty output buffer.");
                return std::nullopt;
            }
            return buffer_out;
        }

        [[nodiscard]] bool reconstruct_i420_frame_with_mapped_buffers(const GstBuffer *buffer_in,
                                                                      unique_gst_buffer_ptr &buffer_out,
                                                                      const GstVideoInfo &video_info_in,
                                                                      const GstVideoInfo &video_info_out,
                                                                      const std::vector<rectangle> &packed_rects,
                                                                      const std::vector<rectangle> &target_rects) {
            GstMapInfo in_map_info{};
            GstMapInfo out_map_info{};

            if (!gst_buffer_map(const_cast<GstBuffer *>(buffer_in), &in_map_info, GST_MAP_READ)) {
                error(area_copy, "Failed to map input buffer.");
                return false;
            }
            const unique_gst_buffer_unmapper in_unmapper(const_cast<GstBuffer *>(buffer_in), &in_map_info);

            if (!gst_buffer_map(buffer_out.get(), &out_map_info, GST_MAP_WRITE)) {
                error(area_copy, "Failed to map output buffer.");
                return false;
            }
            const unique_gst_buffer_unmapper out_unmapper(buffer_out.get(), &out_map_info);

            debug(area_copy, "Reconstructing I420 frame");
            reconstruct_yuv420_frame(in_map_info, out_map_info, video_info_in, video_info_out, packed_rects,
                                     target_rects);
            return true;
        }

        [[nodiscard]] bool process_reconstruction_by_format(const GstBuffer *buffer_in,
                                                            unique_gst_buffer_ptr &buffer_out,
                                                            const GstVideoInfo &video_info_in,
                                                            const GstVideoInfo &video_info_out,
                                                            const std::vector<rectangle> &packed_rects,
                                                            const std::vector<rectangle> &target_rects) {
            const GstVideoFormat format = video_info_in.finfo->format;
            switch (format) {
                case GST_VIDEO_FORMAT_NV12:
                    debug(area_copy, "Reconstructing NV12 frame using GstVideoFrame API");
                    reconstruct_nv12_frame(buffer_in, buffer_out, const_cast<GstVideoInfo *>(&video_info_in),
                                           const_cast<GstVideoInfo *>(&video_info_out), packed_rects, target_rects);
                    return true;
                case GST_VIDEO_FORMAT_I420:
                    return reconstruct_i420_frame_with_mapped_buffers(buffer_in, buffer_out, video_info_in,
                                                                      video_info_out, packed_rects, target_rects);
                default:
                    error(area_copy, "Unsupported video format for reconstruction: %s",
                          gst_video_format_to_string(format));
                    return false;
            }
        }

        void copy_buffer_timestamps(const GstBuffer *const src, GstBuffer *const dst) {
            GST_BUFFER_PTS(dst) = GST_BUFFER_PTS(src);
            GST_BUFFER_DTS(dst) = GST_BUFFER_DTS(src);
            GST_BUFFER_DURATION(dst) = GST_BUFFER_DURATION(src);
        }

        [[nodiscard]] std::optional<unique_gst_sample_ptr>
        create_output_sample(unique_gst_buffer_ptr &buffer_out, const GstVideoInfo &video_info_out,
                             const unique_gst_sample_ptr &packed_sample, GstBuffer *buffer_in) {
            auto caps_out = unique_gst_caps_ptr(gst_video_info_to_caps(&video_info_out));
            if (!caps_out) {
                error(area_copy, "Failed to create output caps.");
                return std::nullopt;
            }

            const auto *structure = gst_sample_get_info(packed_sample.get());
            unique_gst_structure_ptr info_copy(structure ? gst_structure_copy(structure) : nullptr);

            auto sample_out = unique_gst_sample_ptr(gst_sample_new(buffer_out.get(), caps_out.get(),
                                                                   gst_sample_get_segment(packed_sample.get()),
                                                                   info_copy.release()));
            if (!sample_out) {
                error(area_copy, "Failed to create output sample.");
                return std::nullopt;
            }

            buffer_out.reset();
            caps_out.reset();

            auto *const out_buf = gst_sample_get_buffer(sample_out.get());
            if (!out_buf) {
                error(area_copy, "Output sample is missing buffer.");
                return std::nullopt;
            }
            copy_buffer_timestamps(buffer_in, out_buf);

            return sample_out;
        }
    } // namespace

    /**
     * @brief Reconstructs a full frame from a packed frame using PRI SEI metadata.
     *
     * This function is used on the decoding side. It takes a packed video frame
     * and the corresponding Packed Regions Information (PRI) and reconstructs the
     * original, full-resolution frame by copying the regions to their target
     * locations.
     *
     * @param packed_sample The GstSample containing the packed frame.
     * @param pri_info The PRI metadata describing how to reconstruct the frame.
     * @param background_color Background color value for empty areas (default: 0).
     * @return A new GstSample containing the reconstructed full frame, or nullptr on failure.
     */
    [[nodiscard]] unique_gst_sample_ptr reconstruct_frame(unique_gst_sample_ptr &&packed_sample,
                                                          const packed_regions_info &pri_info,
                                                          background_color background_color) {
        auto *const buffer_in = gst_sample_get_buffer(packed_sample.get());
        if (!buffer_in) {
            error(area_copy, "Input sample is missing buffer.");
            return nullptr;
        }

        const auto video_info_in_opt = get_input_video_info(packed_sample);
        if (!video_info_in_opt) {
            return nullptr;
        }
        const GstVideoInfo &video_info_in = *video_info_in_opt;

        const auto video_info_out_opt = create_output_video_info(pri_info, video_info_in.finfo->format);
        if (!video_info_out_opt) {
            return nullptr;
        }
        GstVideoInfo video_info_out = *video_info_out_opt;
        video_info_out.colorimetry = video_info_in.colorimetry;

        debug(area_copy, "Reconstructing frame: input format=%s (%dx%d), output format=%s (%dx%d)",
              gst_video_format_to_string(video_info_in.finfo->format), video_info_in.width, video_info_in.height,
              gst_video_format_to_string(video_info_out.finfo->format), video_info_out.width, video_info_out.height);

        auto buffer_out_opt = create_empty_output_buffer(video_info_out, background_color);
        if (!buffer_out_opt) {
            return nullptr;
        }
        unique_gst_buffer_ptr buffer_out = std::move(buffer_out_opt.value());

        const auto [packed_rects, target_rects] = extract_rectangles_from_pri(pri_info);

        if (!process_reconstruction_by_format(buffer_in, buffer_out, video_info_in, video_info_out, packed_rects,
                                              target_rects)) {
            return nullptr;
        }

        auto sample_out_opt = create_output_sample(buffer_out, video_info_out, packed_sample, buffer_in);
        if (!sample_out_opt) {
            return nullptr;
        }

        return std::move(*sample_out_opt);
    }
} // namespace sf
