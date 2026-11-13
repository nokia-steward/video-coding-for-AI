// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "elements/sf_tet_apply.h"

#include "core/sf_frame_identity.h"
#include "core/sf_sei_sync_store.h"
#include "ops/sf_tet_tiles.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst_wrappers.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC(sf_tet_apply_category);
#define GST_CAT_DEFAULT sf_tet_apply_category

enum { prop_0, prop_enabled, prop_tile_file, prop_qp, prop_dynamic_qp, prop_strict_apply };

struct _sf_tet_apply {
    GstBaseTransform parent;
    gboolean enabled;
    gchar *tile_file;
    guint qp;
    gboolean dynamic_qp;
    gboolean strict_apply;
    gboolean video_info_valid;
    gboolean tiles_loaded;
    gboolean tiles_failed;
    gboolean warned_non_i420;
    gboolean warned_dynamic_qp;
    GstVideoInfo video_info;
    sf_tet_tile_set tiles;
    guint64 applied_frames;
    guint64 apply_failures;
};

G_DEFINE_TYPE(sf_tet_apply, sf_tet_apply, GST_TYPE_BASE_TRANSFORM)

static GstStaticPadTemplate sink_template =
        GST_STATIC_PAD_TEMPLATE("sink", GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static GstStaticPadTemplate src_template =
        GST_STATIC_PAD_TEMPLATE("src", GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS("video/x-raw"));

static void sf_tet_apply_reset_tiles(sf_tet_apply *self) {
    sf_tet_tile_set_clear(&self->tiles);
    self->tiles_loaded = FALSE;
    self->tiles_failed = FALSE;
}

static gboolean sf_tet_apply_ensure_tiles(sf_tet_apply *self) {
    if (self->tile_file == nullptr || self->tile_file[0] == '\0') {
        return FALSE;
    }
    if (self->tiles_loaded) {
        return TRUE;
    }
    if (self->tiles_failed) {
        return FALSE;
    }

    if (!sf_tet_tile_set_load(self->tile_file, &self->tiles)) {
        self->tiles_failed = TRUE;
        return FALSE;
    }
    self->tiles_loaded = TRUE;
    return TRUE;
}

static void sf_tet_apply_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    sf_tet_apply *self = SF_TET_APPLY(object);

    switch (property_id) {
        case prop_enabled:
            self->enabled = g_value_get_boolean(value);
            break;
        case prop_tile_file:
            g_free(self->tile_file);
            self->tile_file = g_value_dup_string(value);
            sf_tet_apply_reset_tiles(self);
            break;
        case prop_qp:
            self->qp = g_value_get_uint(value);
            break;
        case prop_dynamic_qp:
            self->dynamic_qp = g_value_get_boolean(value);
            break;
        case prop_strict_apply:
            self->strict_apply = g_value_get_boolean(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void sf_tet_apply_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    sf_tet_apply *self = SF_TET_APPLY(object);

    switch (property_id) {
        case prop_enabled:
            g_value_set_boolean(value, self->enabled);
            break;
        case prop_tile_file:
            g_value_set_string(value, self->tile_file);
            break;
        case prop_qp:
            g_value_set_uint(value, self->qp);
            break;
        case prop_dynamic_qp:
            g_value_set_boolean(value, self->dynamic_qp);
            break;
        case prop_strict_apply:
            g_value_set_boolean(value, self->strict_apply);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
            break;
    }
}

static void sf_tet_apply_finalize(GObject *object) {
    sf_tet_apply *self = SF_TET_APPLY(object);
    g_free(self->tile_file);
    self->tile_file = nullptr;
    sf_tet_tile_set_clear(&self->tiles);
    G_OBJECT_CLASS(sf_tet_apply_parent_class)->finalize(object);
}

static gboolean sf_tet_apply_set_caps(GstBaseTransform *base_transform, GstCaps *in_caps, GstCaps *out_caps) {
    sf_tet_apply *self = SF_TET_APPLY(base_transform);
    (void) out_caps;

    self->video_info_valid = gst_video_info_from_caps(&self->video_info, in_caps);
    return TRUE;
}

static gboolean sf_tet_apply_ensure_video_info(sf_tet_apply *self, GstBaseTransform *base_transform) {
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

static void sf_tet_apply_consume_sync_entry(const sf_frame_identity *identity) {
    if (identity != nullptr && identity->valid) {
        (void) sf_sei_sync_store_consume(identity->stream_id, identity->key_kind, identity->key_value, nullptr);
    }
}

static GstFlowReturn sf_tet_apply_transform_ip(GstBaseTransform *base_transform, GstBuffer *buffer) {
    sf_tet_apply *self = SF_TET_APPLY(base_transform);
    sf_frame_identity identity = {0};
    GstFlowReturn flow_ret = GST_FLOW_OK;
    guint tile_index = 0U;
    guint qp_for_tile = self->qp;

    if (!sf_frame_identity_from_buffer(base_transform, buffer, &identity)) {
        goto cleanup;
    }

    if (!self->enabled) {
        goto cleanup;
    }

    if (self->dynamic_qp && identity.valid && sf_sei_sync_store_get_stream_base_qp(identity.stream_id, &qp_for_tile)) {
        /* use SPS-derived QP from sync store */
    } else if (self->dynamic_qp && identity.valid && !self->warned_dynamic_qp) {
        GST_WARNING_OBJECT(base_transform, "dynamic-qp enabled but no stream base QP yet; using fixed qp=%u", self->qp);
        self->warned_dynamic_qp = TRUE;
    }
    if (self->tile_file == nullptr || self->tile_file[0] == '\0') {
        goto cleanup;
    }

    if (!sf_tet_apply_ensure_video_info(self, base_transform)) {
        if (self->strict_apply) {
            flow_ret = GST_FLOW_ERROR;
        }
        goto cleanup;
    }
    if (!sf::is_valid_video_dimensions(static_cast<int>(self->video_info.width),
                                       static_cast<int>(self->video_info.height))) {
        if (self->strict_apply) {
            flow_ret = GST_FLOW_ERROR;
        }
        goto cleanup;
    }

    if (GST_VIDEO_INFO_FORMAT(&self->video_info) != GST_VIDEO_FORMAT_I420) {
        if (!self->warned_non_i420) {
            GST_WARNING_OBJECT(base_transform, "TET apply currently supports I420 only; passing frame through");
            self->warned_non_i420 = TRUE;
        }
        goto cleanup;
    }

    if (!sf_tet_apply_ensure_tiles(self)) {
        ++self->apply_failures;
        GST_WARNING_OBJECT(base_transform, "Failed to load TET tile file '%s'", self->tile_file);
        if (self->strict_apply) {
            flow_ret = GST_FLOW_ERROR;
        }
        goto cleanup;
    }

    tile_index = sf_tet_find_tile_index_for_qp(&self->tiles, qp_for_tile);
    if (!sf_tet_apply_i420_in_place(buffer, &self->video_info, &self->tiles, tile_index)) {
        ++self->apply_failures;
        GST_WARNING_OBJECT(base_transform, "Failed to apply TET tile for qp=%u", qp_for_tile);
        if (self->strict_apply) {
            flow_ret = GST_FLOW_ERROR;
        }
        goto cleanup;
    }

    ++self->applied_frames;
    if (self->applied_frames == 1U || (self->applied_frames % 200U) == 0U) {
        GST_INFO_OBJECT(base_transform,
                        "TET apply stats: applied=%" G_GUINT64_FORMAT " failures=%" G_GUINT64_FORMAT
                        " qp=%u tile_index=%u",
                        self->applied_frames, self->apply_failures, qp_for_tile, tile_index);
    }

cleanup:
    sf_tet_apply_consume_sync_entry(&identity);
    sf_frame_identity_clear(&identity);
    return flow_ret;
}

static void sf_tet_apply_class_init(sf_tet_applyClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    object_class->set_property = sf_tet_apply_set_property;
    object_class->get_property = sf_tet_apply_get_property;
    object_class->finalize = sf_tet_apply_finalize;

    g_object_class_install_property(
            object_class, prop_enabled,
            g_param_spec_boolean("enabled", "Enabled", "Enable TET tile application", TRUE,
                                 static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
            object_class, prop_tile_file,
            g_param_spec_string("tile-file", "Tile File", "Path to TET tile file (.tet) used for luma modulation", "",
                                static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
            object_class, prop_qp,
            g_param_spec_uint("qp", "QP", "Quantization parameter for TET tile selection", 0U, 63U, 27U,
                              static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
            object_class, prop_dynamic_qp,
            g_param_spec_boolean("dynamic-qp", "Dynamic QP",
                                 "Use base QP from SPS when available; otherwise use fixed qp property", FALSE,
                                 static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
            object_class, prop_strict_apply,
            g_param_spec_boolean("strict-apply", "Strict Apply", "Fail processing when TET loading or apply fails",
                                 FALSE, static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(element_class, "VCAI TET Apply", "Filter/Video",
                                          "Applies TET tiles on decoded I420 frames", "VCAI");
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);

    transform_class->set_caps = GST_DEBUG_FUNCPTR(sf_tet_apply_set_caps);
    transform_class->transform_ip = GST_DEBUG_FUNCPTR(sf_tet_apply_transform_ip);
    transform_class->passthrough_on_same_caps = FALSE;
    transform_class->transform_ip_on_passthrough = FALSE;
}

static void sf_tet_apply_init(sf_tet_apply *self) {
    GST_DEBUG_CATEGORY_INIT(sf_tet_apply_category, "sf-tet-apply", 0, "VCAI TET apply");
    self->enabled = TRUE;
    self->tile_file = g_strdup("");
    self->qp = 27U;
    self->dynamic_qp = FALSE;
    self->strict_apply = FALSE;
    self->video_info_valid = FALSE;
    self->tiles_loaded = FALSE;
    self->tiles_failed = FALSE;
    self->warned_non_i420 = FALSE;
    self->warned_dynamic_qp = FALSE;
    self->applied_frames = 0U;
    self->apply_failures = 0U;
    memset(&self->tiles, 0, sizeof(self->tiles));
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(self), TRUE);
}
