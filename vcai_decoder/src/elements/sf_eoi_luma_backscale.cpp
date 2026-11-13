// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "elements/sf_eoi_luma_backscale.h"

#include "core/sf_frame_identity.h"
#include "core/sf_sei_sync_store.h"
#include "ops/sf_eoi_luma_ops.h"
#include "ops/sf_sei_payload_parse.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst_wrappers.h>

GST_DEBUG_CATEGORY_STATIC(sf_eoi_luma_backscale_category);
#define GST_CAT_DEFAULT sf_eoi_luma_backscale_category

enum { prop_0, prop_enabled, prop_strict_parsing };

struct _sf_eoi_luma_backscale {
    GstBaseTransform parent;
    gboolean enabled;
    gboolean strict_parsing;
    gboolean video_info_valid;
    gboolean warned_missing_identity;
    GstVideoInfo video_info;
    guint64 matched_frames;
    guint64 parse_failures;
    guint64 applied_frames;
    guint64 reused_frames;
    gboolean active_multiplier_valid;
    gdouble active_multiplier;
};

G_DEFINE_TYPE(sf_eoi_luma_backscale, sf_eoi_luma_backscale, GST_TYPE_BASE_TRANSFORM)

static GstStaticPadTemplate sink_template =
        GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate src_template =
        GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static void sf_eoi_luma_backscale_set_property(GObject *object, guint property_id, const GValue *value,
                                               GParamSpec *pspec) {
    sf_eoi_luma_backscale *self = SF_EOI_LUMA_BACKSCALE(object);

    switch (property_id) {
        case prop_enabled:
            self->enabled = g_value_get_boolean(value);
            break;
        case prop_strict_parsing:
            self->strict_parsing = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void sf_eoi_luma_backscale_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    sf_eoi_luma_backscale *self = SF_EOI_LUMA_BACKSCALE(object);

    switch (property_id) {
        case prop_enabled:
            g_value_set_boolean(value, self->enabled);
            break;
        case prop_strict_parsing:
            g_value_set_boolean(value, self->strict_parsing);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static gboolean sf_eoi_luma_backscale_set_caps(GstBaseTransform *base_transform, GstCaps *in_caps, GstCaps *out_caps) {
    sf_eoi_luma_backscale *self = SF_EOI_LUMA_BACKSCALE(base_transform);
    (void) out_caps;

    self->video_info_valid = gst_video_info_from_caps(&self->video_info, in_caps);
    return TRUE;
}

static gboolean sf_eoi_luma_backscale_ensure_video_info(sf_eoi_luma_backscale *self, GstBaseTransform *base_transform) {
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

static GstFlowReturn sf_eoi_luma_backscale_transform_ip(GstBaseTransform *base_transform, GstBuffer *buffer) {
    sf_eoi_luma_backscale *self = SF_EOI_LUMA_BACKSCALE(base_transform);
    sf_frame_identity identity = {0};
    sf_sei_payload_owned payload = {0};
    sf_eoi_payload_info eoi = {0};
    gdouble multiplier = 0.0;
    gboolean multiplier_updated_this_frame = FALSE;
    GstFlowReturn flow_ret = GST_FLOW_OK;

    if (!self->enabled) {
        goto cleanup;
    }

    if (!sf_frame_identity_from_buffer(base_transform, buffer, &identity) || !identity.valid) {
        if (!self->warned_missing_identity) {
            GST_WARNING_OBJECT(base_transform, "Missing frame identity on decoded buffer; EOI sync skipped");
            self->warned_missing_identity = TRUE;
        }
        goto cleanup;
    }

    if (!sf_sei_sync_store_lookup(identity.stream_id, identity.key_kind, identity.key_value, &payload)) {
        goto apply_if_active;
    }

    ++self->matched_frames;
    if (payload.eoi_size == 0U) {
        goto apply_if_active;
    }

    if (!sf_parse_eoi_payload(payload.eoi_data, payload.eoi_size, &eoi) || !eoi.parse_ok) {
        ++self->parse_failures;
        GST_WARNING_OBJECT(base_transform, "Failed to parse EOI payload");
        if (self->strict_parsing) {
            flow_ret = GST_FLOW_ERROR;
        }
        goto apply_if_active;
    }

    if (eoi.cancel_flag) {
        self->active_multiplier_valid = FALSE;
        goto cleanup;
    }

    if (sf_compute_eoi_luma_multiplier(&eoi, &multiplier)) {
        self->active_multiplier = multiplier;
        self->active_multiplier_valid = TRUE;
        multiplier_updated_this_frame = TRUE;
    }

apply_if_active:
    if (self->active_multiplier_valid) {
        if (!sf_eoi_luma_backscale_ensure_video_info(self, base_transform)) {
            flow_ret = self->strict_parsing ? GST_FLOW_ERROR : GST_FLOW_OK;
            goto cleanup;
        }
        if (!sf::is_valid_video_dimensions(static_cast<int>(self->video_info.width),
                                           static_cast<int>(self->video_info.height))) {
            GST_WARNING_OBJECT(base_transform, "Invalid video dimensions for EOI luma backscale");
            if (self->strict_parsing) {
                flow_ret = GST_FLOW_ERROR;
            }
            goto cleanup;
        }
        if (!sf_apply_luma_multiplier_in_place(buffer, &self->video_info, self->active_multiplier)) {
            GST_WARNING_OBJECT(base_transform, "Failed to apply EOI luma backscale");
            if (self->strict_parsing) {
                flow_ret = GST_FLOW_ERROR;
            }
            goto cleanup;
        }

        ++self->applied_frames;
        if (!multiplier_updated_this_frame) {
            ++self->reused_frames;
        }
    }
    if (self->matched_frames == 1U || (self->matched_frames % 200U) == 0U) {
        GST_INFO_OBJECT(base_transform,
                        "EOI luma stats: matched=%" G_GUINT64_FORMAT " parse_fail=%" G_GUINT64_FORMAT
                        " applied=%" G_GUINT64_FORMAT " reused=%" G_GUINT64_FORMAT " last_multiplier=%.4f",
                        self->matched_frames, self->parse_failures, self->applied_frames, self->reused_frames,
                        self->active_multiplier);
    }

cleanup:
    sf_sei_payload_owned_clear(&payload);
    sf_frame_identity_clear(&identity);
    return flow_ret;
}

static void sf_eoi_luma_backscale_class_init(sf_eoi_luma_backscaleClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    object_class->set_property = sf_eoi_luma_backscale_set_property;
    object_class->get_property = sf_eoi_luma_backscale_get_property;

    g_object_class_install_property(
            object_class, prop_enabled,
            g_param_spec_boolean("enabled", "Enabled", "Enable EOI luma backscaling", TRUE,
                                 static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
            object_class, prop_strict_parsing,
            g_param_spec_boolean("strict-parsing", "Strict Parsing", "Fail processing when EOI parsing/backscale fails",
                                 FALSE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(element_class, "VCAI EOI Luma Backscale", "Filter/Video",
                                          "Applies EOI-based luma backscaling on decoded frames", "VCAI");
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    transform_class->set_caps = GST_DEBUG_FUNCPTR(sf_eoi_luma_backscale_set_caps);
    transform_class->transform_ip = GST_DEBUG_FUNCPTR(sf_eoi_luma_backscale_transform_ip);
    transform_class->passthrough_on_same_caps = FALSE;
    transform_class->transform_ip_on_passthrough = FALSE;
}

static void sf_eoi_luma_backscale_init(sf_eoi_luma_backscale *self) {
    GST_DEBUG_CATEGORY_INIT(sf_eoi_luma_backscale_category, "sf-eoi-luma-backscale", 0,
                            "VCAI EOI luma backscale");
    self->enabled = TRUE;
    self->strict_parsing = FALSE;
    self->video_info_valid = FALSE;
    self->warned_missing_identity = FALSE;
    self->matched_frames = 0U;
    self->parse_failures = 0U;
    self->applied_frames = 0U;
    self->reused_frames = 0U;
    self->active_multiplier_valid = FALSE;
    self->active_multiplier = 1.0;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}
