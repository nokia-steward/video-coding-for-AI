// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "elements/sf_pri_reconstruct.h"

#include "core/sf_frame_identity.h"
#include "core/sf_sei_sync_store.h"

#include <area_copy.h>
#include <gst_raii.h>
#include <packed_regions_info.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdio.h>
#include <string.h>
#include <vector>

GST_DEBUG_CATEGORY_STATIC(sf_pri_reconstruct_category);
#define GST_CAT_DEFAULT sf_pri_reconstruct_category

enum { prop_0, prop_strict_parsing, prop_strict_reconstruction, prop_background_color, prop_roi_trace_path };

struct _sf_pri_reconstruct {
    GstBaseTransform parent;
    gboolean strict_parsing;
    gboolean strict_reconstruction;
    gboolean video_info_valid;
    gboolean warned_missing_identity;
    GstVideoInfo video_info;
    guint64 matched_frames;
    guint64 parse_failures;
    guint64 reconstruction_failures;
    guint64 copied_regions;
    guint64 skipped_regions;
    guint background_color;
    gchar *roi_trace_path;
    FILE *roi_trace_file;
    gboolean warned_trace_open_failure;
    guint64 traced_frames;
    guint64 trace_write_failures;
};

G_DEFINE_TYPE(sf_pri_reconstruct, sf_pri_reconstruct, GST_TYPE_BASE_TRANSFORM)

static GstStaticPadTemplate sink_template =
        GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate src_template =
        GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static const gchar *sf_sei_key_kind_to_string(const sf_sei_key_kind key_kind) {
    switch (key_kind) {
        case SF_SEI_KEY_KIND_PTS:
            return "pts";
        case SF_SEI_KEY_KIND_DTS:
            return "dts";
        case SF_SEI_KEY_KIND_OFFSET:
            return "offset";
        default:
            return "unknown";
    }
}

/** @brief Escapes a string for JSON (backslash, quote, newline, etc.). Caller frees result. */
static gchar *sf_escape_json_string(const gchar *value) {
    GString *escaped = nullptr;
    const gchar *cursor = value != nullptr ? value : "";

    escaped = g_string_new("");
    if (escaped == nullptr) {
        return nullptr;
    }

    while (*cursor != '\0') {
        const gchar ch = *cursor++;
        if (ch == '\\' || ch == '"') {
            g_string_append_c(escaped, '\\');
            g_string_append_c(escaped, ch);
        } else if (ch == '\n') {
            g_string_append(escaped, "\\n");
        } else if (ch == '\r') {
            g_string_append(escaped, "\\r");
        } else if (ch == '\t') {
            g_string_append(escaped, "\\t");
        } else {
            g_string_append_c(escaped, ch);
        }
    }

    return g_string_free(escaped, FALSE);
}

/** @brief Opens roi_trace_path for append if not already open. @return TRUE if file is ready. */
static gboolean sf_pri_reconstruct_ensure_trace_file(sf_pri_reconstruct *self, GstBaseTransform *base_transform) {
    if (self->roi_trace_path == nullptr || self->roi_trace_path[0] == '\0') {
        return FALSE;
    }
    if (self->roi_trace_file != nullptr) {
        return TRUE;
    }

    self->roi_trace_file = fopen(self->roi_trace_path, "a");
    if (self->roi_trace_file == nullptr) {
        if (!self->warned_trace_open_failure) {
            GST_WARNING_OBJECT(base_transform, "Failed to open PRI ROI trace file '%s'", self->roi_trace_path);
            self->warned_trace_open_failure = TRUE;
        }
        return FALSE;
    }

    return TRUE;
}

/** @brief Converts PRI units to pixels (same rule as @c sf::read_packed_regions_info region mapping). */
static gboolean sf_pri_units_to_pixels(const guint value_units, const guint log2_unit_size, guint *out_pixels) {
    if (out_pixels == nullptr || log2_unit_size > 31U) {
        return FALSE;
    }
    const guint64 pixels = static_cast<guint64>(value_units) << log2_unit_size;
    if (pixels > static_cast<guint64>(G_MAXUINT)) {
        return FALSE;
    }
    *out_pixels = static_cast<guint>(pixels);
    return TRUE;
}

/** @brief True when PRI has at least one non-layer region with packed geometry (reconstruction candidate). */
static gboolean sf_packed_pri_has_drawable_regions(const sf::packed_regions_info &pri) {
    if (pri.cancel_flag) {
        return FALSE;
    }
    for (const sf::region &reg: pri.regions) {
        if (reg.is_a_layer_flag.value_or(false)) {
            continue;
        }
        if (reg.top_left_in_units_x.has_value() && reg.top_left_in_units_y.has_value() &&
            reg.width_in_units_minus1.has_value() && reg.height_in_units_minus1.has_value()) {
            return TRUE;
        }
    }
    return FALSE;
}

/** @brief Writes one JSON line per frame/identity/pri to the ROI trace file (if open). */
static void sf_pri_reconstruct_dump_pri_trace(sf_pri_reconstruct *self, GstBaseTransform *base_transform,
                                              const sf_frame_identity *identity, const sf::packed_regions_info &pri) {
    GHashTable *pair_keys = nullptr;
    gchar *escaped_stream_id = nullptr;
    guint duplicate_pair_count = 0U;

    if (identity == nullptr) {
        return;
    }
    if (!sf_pri_reconstruct_ensure_trace_file(self, base_transform)) {
        return;
    }

    pair_keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
    if (pair_keys == nullptr) {
        return;
    }

    escaped_stream_id = sf_escape_json_string(identity->stream_id);
    if (escaped_stream_id == nullptr) {
        g_hash_table_unref(pair_keys);
        return;
    }

    if (fprintf(self->roi_trace_file,
                "{\"matched_frame_index\":%" G_GUINT64_FORMAT ",\"stream_id\":\"%s\",\"key_kind\":\"%s\","
                "\"key_value\":%" G_GUINT64_FORMAT ",\"cancel_flag\":%s,\"regions\":[",
                self->matched_frames, escaped_stream_id, sf_sei_key_kind_to_string(identity->key_kind),
                identity->key_value, pri.cancel_flag ? "true" : "false") < 0) {
        ++self->trace_write_failures;
        goto cleanup;
    }

    {
        const guint log2_unit_size = static_cast<guint>(pri.log2_unit_size.value_or(0U));
        const bool target_present = pri.target_pic_params_present_flag.value_or(false);
        guint trace_index = 0U;

        for (const sf::region &reg: pri.regions) {
            if (reg.is_a_layer_flag.value_or(false)) {
                continue;
            }
            if (!reg.top_left_in_units_x.has_value() || !reg.top_left_in_units_y.has_value() ||
                !reg.width_in_units_minus1.has_value() || !reg.height_in_units_minus1.has_value()) {
                continue;
            }

            const guint packed_x_units = static_cast<guint>(*reg.top_left_in_units_x);
            const guint packed_y_units = static_cast<guint>(*reg.top_left_in_units_y);
            const guint width_units_minus1 = static_cast<guint>(*reg.width_in_units_minus1);
            const guint height_units_minus1 = static_cast<guint>(*reg.height_in_units_minus1);
            const guint width_units = width_units_minus1 + 1U;
            const guint height_units = height_units_minus1 + 1U;

            guint target_x_units = packed_x_units;
            guint target_y_units = packed_y_units;
            if (target_present) {
                if (!reg.target_region_top_left_in_units_x.has_value() ||
                    !reg.target_region_top_left_in_units_y.has_value()) {
                    continue;
                }
                target_x_units = static_cast<guint>(*reg.target_region_top_left_in_units_x);
                target_y_units = static_cast<guint>(*reg.target_region_top_left_in_units_y);
            }

            guint packed_x = 0U;
            guint packed_y = 0U;
            guint width_px = 0U;
            guint height_px = 0U;
            guint target_x = 0U;
            guint target_y = 0U;
            if (!sf_pri_units_to_pixels(packed_x_units, log2_unit_size, &packed_x) ||
                !sf_pri_units_to_pixels(packed_y_units, log2_unit_size, &packed_y) ||
                !sf_pri_units_to_pixels(width_units, log2_unit_size, &width_px) ||
                !sf_pri_units_to_pixels(height_units, log2_unit_size, &height_px) ||
                !sf_pri_units_to_pixels(target_x_units, log2_unit_size, &target_x) ||
                !sf_pri_units_to_pixels(target_y_units, log2_unit_size, &target_y)) {
                continue;
            }

            gchar *pair_key = g_strdup_printf("%u:%u:%u:%u|%u:%u:%u:%u", packed_x, packed_y, width_px, height_px,
                                              target_x, target_y, width_px, height_px);
            if (pair_key == nullptr) {
                ++self->trace_write_failures;
                break;
            }
            if (g_hash_table_contains(pair_keys, pair_key)) {
                ++duplicate_pair_count;
            } else {
                g_hash_table_add(pair_keys, g_strdup(pair_key));
            }

            if (trace_index > 0U && fprintf(self->roi_trace_file, ",") < 0) {
                ++self->trace_write_failures;
                g_free(pair_key);
                break;
            }
            if (fprintf(self->roi_trace_file,
                        "{\"packed\":{\"x\":%u,\"y\":%u,\"w\":%u,\"h\":%u},\"target\":{\"x\":%u,\"y\":%u,\"w\":%u,"
                        "\"h\":%u}"
                        ","
                        "\"pair_key\":\"%s\"}",
                        packed_x, packed_y, width_px, height_px, target_x, target_y, width_px, height_px,
                        pair_key) < 0) {
                ++self->trace_write_failures;
                g_free(pair_key);
                break;
            }

            g_free(pair_key);
            ++trace_index;
        }
    }

    if (fprintf(self->roi_trace_file, "],\"duplicate_pair_count\":%u}\n", duplicate_pair_count) < 0) {
        ++self->trace_write_failures;
        goto cleanup;
    }

    if (fflush(self->roi_trace_file) != 0) {
        ++self->trace_write_failures;
        goto cleanup;
    }

    ++self->traced_frames;
    if (self->traced_frames == 1U || (self->traced_frames % 200U) == 0U) {
        GST_INFO_OBJECT(base_transform,
                        "PRI ROI trace stats: traced=%" G_GUINT64_FORMAT " write_failures=%" G_GUINT64_FORMAT
                        " path=%s",
                        self->traced_frames, self->trace_write_failures, self->roi_trace_path);
    }

cleanup:
    g_free(escaped_stream_id);
    g_hash_table_unref(pair_keys);
}

static void sf_pri_reconstruct_set_property(GObject *object, guint property_id, const GValue *value,
                                            GParamSpec *pspec) {
    sf_pri_reconstruct *self = SF_PRI_RECONSTRUCT(object);

    switch (property_id) {
        case prop_strict_parsing:
            self->strict_parsing = g_value_get_boolean(value);
            break;
        case prop_strict_reconstruction:
            self->strict_reconstruction = g_value_get_boolean(value);
            break;
        case prop_background_color:
            self->background_color = g_value_get_uint(value);
            break;
        case prop_roi_trace_path:
            g_free(self->roi_trace_path);
            self->roi_trace_path = g_value_dup_string(value);
            if (self->roi_trace_file != nullptr) {
                fclose(self->roi_trace_file);
                self->roi_trace_file = nullptr;
            }
            self->warned_trace_open_failure = FALSE;
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void sf_pri_reconstruct_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    sf_pri_reconstruct *self = SF_PRI_RECONSTRUCT(object);

    switch (property_id) {
        case prop_strict_parsing:
            g_value_set_boolean(value, self->strict_parsing);
            break;
        case prop_strict_reconstruction:
            g_value_set_boolean(value, self->strict_reconstruction);
            break;
        case prop_background_color:
            g_value_set_uint(value, self->background_color);
            break;
        case prop_roi_trace_path:
            g_value_set_string(value, self->roi_trace_path);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void sf_pri_reconstruct_finalize(GObject *object) {
    sf_pri_reconstruct *self = SF_PRI_RECONSTRUCT(object);
    if (self->roi_trace_file != nullptr) {
        fclose(self->roi_trace_file);
        self->roi_trace_file = nullptr;
    }
    g_free(self->roi_trace_path);
    self->roi_trace_path = nullptr;
    G_OBJECT_CLASS(sf_pri_reconstruct_parent_class)->finalize(object);
}

static gboolean sf_pri_reconstruct_set_caps(GstBaseTransform *base_transform, GstCaps *in_caps, GstCaps *out_caps) {
    sf_pri_reconstruct *self = SF_PRI_RECONSTRUCT(base_transform);
    (void) out_caps;

    self->video_info_valid = gst_video_info_from_caps(&self->video_info, in_caps);
    return TRUE;
}

static gboolean sf_pri_reconstruct_ensure_video_info(sf_pri_reconstruct *self, GstBaseTransform *base_transform) {
    GstCaps *caps = nullptr;
    gboolean ok = FALSE;

    if (self->video_info_valid) {
        return TRUE;
    }

    caps = gst_pad_get_current_caps(GST_BASE_TRANSFORM_SINK_PAD(base_transform));
    if (caps == nullptr) {
        return FALSE;
    }
    ok = gst_video_info_from_caps(&self->video_info, caps);
    gst_caps_unref(caps);
    self->video_info_valid = ok;
    return ok;
}

static gboolean sf_pri_reconstruct_copy_input_to_output(GstBuffer *input_buffer, GstBuffer *output_buffer) {
    GstMapInfo input_map = {0};
    GstMapInfo output_map = {0};
    gboolean copy_ok = FALSE;

    if (input_buffer == nullptr || output_buffer == nullptr) {
        return FALSE;
    }
    if (!gst_buffer_map(input_buffer, &input_map, GST_MAP_READ)) {
        return FALSE;
    }
    if (!gst_buffer_map(output_buffer, &output_map, GST_MAP_WRITE)) {
        gst_buffer_unmap(input_buffer, &input_map);
        return FALSE;
    }

    if (output_map.size >= input_map.size) {
        memcpy(output_map.data, input_map.data, input_map.size);
        if (output_map.size > input_map.size) {
            memset(output_map.data + input_map.size, 0, output_map.size - input_map.size);
        }
        copy_ok = TRUE;
    }

    gst_buffer_unmap(output_buffer, &output_map);
    gst_buffer_unmap(input_buffer, &input_map);
    return copy_ok;
}

static void sf_pri_reconstruct_copy_timing_fields(GstBuffer *input_buffer, GstBuffer *output_buffer) {
    GST_BUFFER_FLAGS(output_buffer) = GST_BUFFER_FLAGS(input_buffer);
    GST_BUFFER_PTS(output_buffer) = GST_BUFFER_PTS(input_buffer);
    GST_BUFFER_DTS(output_buffer) = GST_BUFFER_DTS(input_buffer);
    GST_BUFFER_DURATION(output_buffer) = GST_BUFFER_DURATION(input_buffer);
    GST_BUFFER_OFFSET(output_buffer) = GST_BUFFER_OFFSET(input_buffer);
    GST_BUFFER_OFFSET_END(output_buffer) = GST_BUFFER_OFFSET_END(input_buffer);
}

/** Tries libvcai reconstruct_frame for to-output path when output dimensions match PRI target. */
static gboolean sf_pri_reconstruct_via_lib(GstBuffer *input_buffer, GstBuffer *output_buffer,
                                           const GstVideoInfo *video_info, const sf::packed_regions_info &pri_info,
                                           guint32 background_color) {
    if (input_buffer == nullptr || output_buffer == nullptr || video_info == nullptr) {
        return FALSE;
    }
    if (pri_info.cancel_flag || !pri_info.target_pic_width_minus1 || !pri_info.target_pic_height_minus1) {
        return FALSE;
    }
    const int target_w = static_cast<int>(*pri_info.target_pic_width_minus1) + 1;
    const int target_h = static_cast<int>(*pri_info.target_pic_height_minus1) + 1;
    if (video_info->width != static_cast<guint>(target_w) || video_info->height != static_cast<guint>(target_h)) {
        return FALSE;
    }
    GstCaps *caps = gst_video_info_to_caps(video_info);
    if (caps == nullptr) {
        return FALSE;
    }
    GstSample *raw_sample = gst_sample_new(input_buffer, caps, nullptr, nullptr);
    gst_caps_unref(caps);
    if (raw_sample == nullptr) {
        return FALSE;
    }
    sf::unique_gst_sample_ptr packed_sample(raw_sample);
    (void) background_color; /* lib only supports green for now */
    sf::unique_gst_sample_ptr out_sample =
            sf::reconstruct_frame(std::move(packed_sample), pri_info, sf::background_color::green);
    if (out_sample == nullptr) {
        return FALSE;
    }
    GstBuffer *recon_buf = gst_sample_get_buffer(out_sample.get());
    if (recon_buf == nullptr) {
        return FALSE;
    }
    GstMapInfo imap = {};
    GstMapInfo omap = {};
    if (!gst_buffer_map(recon_buf, &imap, GST_MAP_READ) || !gst_buffer_map(output_buffer, &omap, GST_MAP_WRITE)) {
        if (imap.memory != nullptr) {
            gst_buffer_unmap(recon_buf, &imap);
        }
        return FALSE;
    }
    const sf::unique_gst_buffer_unmapper unmap_recon(recon_buf, &imap);
    const sf::unique_gst_buffer_unmapper unmap_out(output_buffer, &omap);
    if (omap.size != imap.size) {
        return FALSE;
    }
    memcpy(omap.data, imap.data, omap.size);
    return TRUE;
}

static GstFlowReturn sf_pri_reconstruct_transform(GstBaseTransform *base_transform, GstBuffer *input_buffer,
                                                  GstBuffer *output_buffer) {
    sf_pri_reconstruct *self = SF_PRI_RECONSTRUCT(base_transform);
    sf_frame_identity identity = {0};
    sf_sei_payload_owned payload = {0};
    gboolean reconstructed = FALSE;
    GstFlowReturn flow_ret = GST_FLOW_OK;

    sf_pri_reconstruct_copy_timing_fields(input_buffer, output_buffer);

    if (!sf_frame_identity_from_buffer(base_transform, input_buffer, &identity) || !identity.valid) {
        if (!self->warned_missing_identity) {
            GST_WARNING_OBJECT(base_transform, "Missing frame identity on decoded buffer; PRI sync skipped");
            self->warned_missing_identity = TRUE;
        }
        goto cleanup;
    }

    if (!sf_sei_sync_store_lookup(identity.stream_id, identity.key_kind, identity.key_value, &payload)) {
        goto cleanup;
    }

    ++self->matched_frames;
    if (payload.pri_size == 0U) {
        goto cleanup;
    }

    {
        bool lib_pri_ok = false;
        const std::vector<uint8_t> pri_vec(payload.pri_data, payload.pri_data + payload.pri_size);
        const sf::packed_regions_info pri_info = sf::read_packed_regions_info(pri_vec, &lib_pri_ok);

        if (!lib_pri_ok) {
            ++self->parse_failures;
            GST_WARNING_OBJECT(base_transform, "Failed to parse PRI payload for matched decoded frame");
            if (self->strict_parsing) {
                flow_ret = GST_FLOW_ERROR;
            }
            goto cleanup;
        }

        sf_pri_reconstruct_dump_pri_trace(self, base_transform, &identity, pri_info);

        if (pri_info.cancel_flag || !sf_packed_pri_has_drawable_regions(pri_info)) {
            goto cleanup;
        }
        if (!sf_pri_reconstruct_ensure_video_info(self, base_transform)) {
            ++self->reconstruction_failures;
            flow_ret = self->strict_reconstruction ? GST_FLOW_ERROR : GST_FLOW_OK;
            goto cleanup;
        }

        if (pri_info.target_pic_params_present_flag.value_or(false)) {
            if (!pri_info.target_pic_width_minus1.has_value() || !pri_info.target_pic_height_minus1.has_value()) {
                goto cleanup;
            }
            const guint target_w = static_cast<guint>(*pri_info.target_pic_width_minus1) + 1U;
            const guint target_h = static_cast<guint>(*pri_info.target_pic_height_minus1) + 1U;
            if (target_w != self->video_info.width || target_h != self->video_info.height) {
                GST_WARNING_OBJECT(base_transform,
                                   "PRI target size %ux%u differs from decoded frame %ux%u; reconstruction skipped",
                                   target_w, target_h, self->video_info.width, self->video_info.height);
                if (self->strict_reconstruction) {
                    ++self->reconstruction_failures;
                    flow_ret = GST_FLOW_ERROR;
                }
                goto cleanup;
            }
        }

        if (sf_pri_reconstruct_via_lib(input_buffer, output_buffer, &self->video_info, pri_info,
                                       self->background_color)) {
            reconstructed = TRUE;
        } else {
            ++self->reconstruction_failures;
            GST_WARNING_OBJECT(base_transform, "PRI reconstruction via lib failed; passthrough");
            if (self->strict_reconstruction) {
                flow_ret = GST_FLOW_ERROR;
            }
            goto cleanup;
        }
    }
    if (self->matched_frames == 1U || (self->matched_frames % 200U) == 0U) {
        GST_INFO_OBJECT(base_transform,
                        "PRI reconstruction stats: matched=%" G_GUINT64_FORMAT " parse_fail=%" G_GUINT64_FORMAT
                        " recon_fail=%" G_GUINT64_FORMAT " copied_regions=%" G_GUINT64_FORMAT
                        " skipped_regions=%" G_GUINT64_FORMAT,
                        self->matched_frames, self->parse_failures, self->reconstruction_failures, self->copied_regions,
                        self->skipped_regions);
    }

cleanup:
    if (flow_ret == GST_FLOW_OK && !reconstructed) {
        if (!sf_pri_reconstruct_copy_input_to_output(input_buffer, output_buffer)) {
            GST_WARNING_OBJECT(base_transform, "Failed to copy decoded frame for PRI passthrough");
            flow_ret = GST_FLOW_ERROR;
        }
    }
    sf_sei_payload_owned_clear(&payload);
    sf_frame_identity_clear(&identity);
    return flow_ret;
}

static void sf_pri_reconstruct_class_init(sf_pri_reconstructClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    object_class->set_property = sf_pri_reconstruct_set_property;
    object_class->get_property = sf_pri_reconstruct_get_property;
    object_class->finalize = sf_pri_reconstruct_finalize;

    g_object_class_install_property(
            object_class, prop_strict_parsing,
            g_param_spec_boolean("strict-parsing", "Strict Parsing", "Fail processing when PRI payload parsing fails",
                                 FALSE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    g_object_class_install_property(
            object_class, prop_strict_reconstruction,
            g_param_spec_boolean("strict-reconstruction", "Strict Reconstruction",
                                 "Fail processing when PRI reconstruction cannot be applied safely", FALSE,
                                 static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
            object_class, prop_background_color,
            g_param_spec_uint("background-color", "Background Color",
                              "Raw background color enum value used when initializing reconstructed output", 0U,
                              G_MAXUINT, 0U, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
            object_class, prop_roi_trace_path,
            g_param_spec_string("roi-trace-path", "ROI Trace Path",
                                "Optional JSONL path to dump parsed PRI packed/target region pairs", "",
                                static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(element_class, "VCAI PRI Reconstructor", "Filter/Video",
                                          "Applies PRI region reconstruction on decoded raw frames", "VCAI");
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    transform_class->set_caps = GST_DEBUG_FUNCPTR(sf_pri_reconstruct_set_caps);
    transform_class->transform = GST_DEBUG_FUNCPTR(sf_pri_reconstruct_transform);
    transform_class->passthrough_on_same_caps = FALSE;
    transform_class->transform_ip_on_passthrough = FALSE;
}

static void sf_pri_reconstruct_init(sf_pri_reconstruct *self) {
    GST_DEBUG_CATEGORY_INIT(sf_pri_reconstruct_category, "sf-pri-reconstruct", 0, "VCAI PRI reconstruction");
    self->strict_parsing = FALSE;
    self->strict_reconstruction = FALSE;
    self->video_info_valid = FALSE;
    self->warned_missing_identity = FALSE;
    self->matched_frames = 0U;
    self->parse_failures = 0U;
    self->reconstruction_failures = 0U;
    self->copied_regions = 0U;
    self->skipped_regions = 0U;
    self->background_color = 0U;
    self->roi_trace_path = g_strdup("");
    self->roi_trace_file = nullptr;
    self->warned_trace_open_failure = FALSE;
    self->traced_frames = 0U;
    self->trace_write_failures = 0U;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), FALSE);
}
