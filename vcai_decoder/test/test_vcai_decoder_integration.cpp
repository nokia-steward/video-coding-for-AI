// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "core/sf_sei_sync_store.h"
#include "elements/sf_h265_sei_extract.h"
#include "elements/sf_pri_reconstruct.h"
#include "meta/sf_sei_meta.h"

#include <glib/gstdio.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/video/video.h>

typedef struct {
    GByteArray *bytes;
    guint bit_offset;
} IntegrationBitWriter;

static void integration_bit_writer_init(IntegrationBitWriter *writer) {
    writer->bytes = g_byte_array_new();
    writer->bit_offset = 0U;
}

static void integration_bit_writer_write_bit(IntegrationBitWriter *writer, const guint bit) {
    guint8 value = (bit != 0U) ? 1U : 0U;

    if (writer->bit_offset == 0U) {
        const guint8 zero = 0U;
        g_byte_array_append(writer->bytes, &zero, 1U);
    }

    writer->bytes->data[writer->bytes->len - 1U] |= static_cast<guint8>(value << (7U - writer->bit_offset));
    writer->bit_offset = (writer->bit_offset + 1U) % 8U;
}

static void integration_bit_writer_write_bits(IntegrationBitWriter *writer, const guint value, const guint num_bits) {
    for (gint bit_index = static_cast<gint>(num_bits) - 1; bit_index >= 0; --bit_index) {
        integration_bit_writer_write_bit(writer, (value >> static_cast<guint>(bit_index)) & 0x01U);
    }
}

static void integration_bit_writer_write_ue(IntegrationBitWriter *writer, const guint value) {
    const guint code_num = value + 1U;
    guint leading_zero_bits = 0U;
    guint temp = code_num;

    while (temp > 1U) {
        temp >>= 1U;
        ++leading_zero_bits;
    }
    for (guint index = 0U; index < leading_zero_bits; ++index) {
        integration_bit_writer_write_bit(writer, 0U);
    }
    integration_bit_writer_write_bits(writer, code_num, leading_zero_bits + 1U);
}

static GByteArray *build_annexb_buffer_with_cancel_pri_and_eoi(void) {
    static const guint8 data[] = {
            0x00, 0x00, 0x01, /* start code */
            0x4E, 0x01, /* nal header: prefix sei */
            0xE2, 0x01, 0x80, /* PRI payload type=226, size=1, cancel=1 */
            0xD7, 0x01, 0x80, /* EOI payload type=215, size=1, cancel=1 */
            0x80 /* rbsp trailing bits */
    };

    return g_byte_array_new_take(static_cast<guint8 *>(g_memdup2(data, sizeof(data))), sizeof(data));
}

static GByteArray *build_pri_payload_with_multi_ratio_target(const guint target_x_units, const guint target_y_units) {
    IntegrationBitWriter writer = {0};

    integration_bit_writer_init(&writer);
    integration_bit_writer_write_bit(&writer, 0U); /* cancel_flag */
    integration_bit_writer_write_bit(&writer, 1U); /* persistence_flag */
    integration_bit_writer_write_ue(&writer, 0U); /* num_regions_minus1 */
    integration_bit_writer_write_bit(&writer, 0U); /* multilayer_flag */
    integration_bit_writer_write_bit(&writer, 0U); /* use_max_dimensions_flag */
    integration_bit_writer_write_bits(&writer, 0U, 4U); /* log2_unit_size */
    integration_bit_writer_write_bits(&writer, 3U, 4U); /* region_size_len_minus1 */
    integration_bit_writer_write_bit(&writer, 0U); /* region_id_present_flag */
    integration_bit_writer_write_bit(&writer, 1U); /* target_pic_params_present_flag */
    /* Target picture 8x8 to match reconstruct pipeline caps (was 16x16 and prevented via_lib). */
    integration_bit_writer_write_bits(&writer, 7U, 16U);
    integration_bit_writer_write_bits(&writer, 7U, 16U);

    integration_bit_writer_write_ue(&writer, 2U); /* num_resampling_ratios_minus1 */
    integration_bit_writer_write_ue(&writer, 0U);
    integration_bit_writer_write_ue(&writer, 0U);
    integration_bit_writer_write_bit(&writer, 1U);
    integration_bit_writer_write_ue(&writer, 1U);
    integration_bit_writer_write_ue(&writer, 1U);
    integration_bit_writer_write_bit(&writer, 1U);

    integration_bit_writer_write_bits(&writer, 0U, 4U); /* packed_x — align with marker at (0,0) */
    integration_bit_writer_write_bits(&writer, 0U, 4U); /* packed_y */
    integration_bit_writer_write_bits(&writer, 1U, 4U); /* width_minus1 */
    integration_bit_writer_write_bits(&writer, 1U, 4U); /* height_minus1 */
    integration_bit_writer_write_bits(&writer, 2U, 2U); /* ratio index */
    integration_bit_writer_write_bits(&writer, target_x_units, 4U);
    integration_bit_writer_write_bits(&writer, target_y_units, 4U);

    return writer.bytes;
}

static GstElement *create_extract_pipeline(GstElement **out_appsrc, GstElement **out_extract, GstElement **out_sink) {
    GstElement *pipeline = gst_pipeline_new("extract-pipeline");
    GstElement *appsrc = gst_element_factory_make("appsrc", "src");
    GstElement *extract = GST_ELEMENT(g_object_new(SF_TYPE_H265_SEI_EXTRACT, nullptr));
    GstElement *appsink = gst_element_factory_make("appsink", "sink");
    GstCaps *caps = gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream", "alignment",
                                        G_TYPE_STRING, "au", nullptr);

    g_assert_nonnull(pipeline);
    g_assert_nonnull(appsrc);
    g_assert_nonnull(extract);
    g_assert_nonnull(appsink);
    g_assert_nonnull(caps);

    g_object_set(appsrc, "caps", caps, "format", GST_FORMAT_TIME, "is-live", FALSE, nullptr);
    g_object_set(appsink, "sync", FALSE, nullptr);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(pipeline), appsrc, extract, appsink, nullptr);
    g_assert_true(gst_element_link_many(appsrc, extract, appsink, nullptr));

    *out_appsrc = appsrc;
    *out_extract = extract;
    *out_sink = appsink;
    return pipeline;
}

static GstElement *create_reconstruct_pipeline(GstElement **out_appsrc, GstElement **out_reconstruct,
                                               GstElement **out_sink) {
    GstElement *pipeline = gst_pipeline_new("reconstruct-pipeline");
    GstElement *appsrc = gst_element_factory_make("appsrc", "raw-src");
    GstElement *reconstruct = GST_ELEMENT(g_object_new(SF_TYPE_PRI_RECONSTRUCT, nullptr));
    GstElement *appsink = gst_element_factory_make("appsink", "raw-sink");
    GstCaps *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", "width", G_TYPE_INT, 8,
                                        "height", G_TYPE_INT, 8, "framerate", GST_TYPE_FRACTION, 30, 1, nullptr);

    g_assert_nonnull(pipeline);
    g_assert_nonnull(appsrc);
    g_assert_nonnull(reconstruct);
    g_assert_nonnull(appsink);
    g_assert_nonnull(caps);

    g_object_set(appsrc, "caps", caps, "format", GST_FORMAT_TIME, "is-live", FALSE, nullptr);
    g_object_set(appsink, "sync", FALSE, nullptr);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(pipeline), appsrc, reconstruct, appsink, nullptr);
    g_assert_true(gst_element_link_many(appsrc, reconstruct, appsink, nullptr));

    *out_appsrc = appsrc;
    *out_reconstruct = reconstruct;
    *out_sink = appsink;
    return pipeline;
}

static GstBuffer *create_i420_buffer_with_markers(guint8 source_value, guint8 target_value, guint8 alternate_value) {
    GstVideoInfo info = {0};
    GstBuffer *buffer = nullptr;
    GstMapInfo map = {0};
    guint8 *y_plane = nullptr;
    guint y_stride = 0U;

    gst_video_info_init(&info);
    g_assert_true(gst_video_info_set_format(&info, GST_VIDEO_FORMAT_I420, 8U, 8U));

    buffer = gst_buffer_new_allocate(nullptr, info.size, nullptr);
    g_assert_nonnull(buffer);
    g_assert_true(gst_buffer_map(buffer, &map, GST_MAP_WRITE));

    y_plane = map.data + GST_VIDEO_INFO_PLANE_OFFSET(&info, 0);
    y_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 0));
    memset(y_plane, 10, static_cast<gsize>(y_stride) * 8U);

    y_plane[(0U * y_stride) + 0U] = source_value; /* source area (after even alignment) */
    y_plane[(6U * y_stride) + 6U] = target_value; /* intended target */
    y_plane[(6U * y_stride) + 2U] = alternate_value; /* wrong target if ratio-index bits are misread */

    memset(map.data + GST_VIDEO_INFO_PLANE_OFFSET(&info, 1), 128,
           static_cast<gsize>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 1)) * 4U);
    memset(map.data + GST_VIDEO_INFO_PLANE_OFFSET(&info, 2), 128,
           static_cast<gsize>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 2)) * 4U);

    gst_buffer_unmap(buffer, &map);
    return buffer;
}

static void test_extract_attaches_meta_and_publishes_sync_store(void) {
    GstElement *pipeline = nullptr;
    GstElement *appsrc = nullptr;
    GstElement *extract = nullptr;
    GstElement *appsink = nullptr;
    GByteArray *annexb = nullptr;
    GstBuffer *buffer = nullptr;
    GstSample *sample = nullptr;
    GstPad *extract_sink_pad = nullptr;
    gchar *stream_id = nullptr;
    sf_sei_payload_owned lookup_payload = {0};
    const guint64 pts = 42U;

    sf_sei_sync_store_clear();
    pipeline = create_extract_pipeline(&appsrc, &extract, &appsink);
    g_assert_cmpint(gst_element_set_state(pipeline, GST_STATE_PLAYING), !=, GST_STATE_CHANGE_FAILURE);

    annexb = build_annexb_buffer_with_cancel_pri_and_eoi();
    g_assert_nonnull(annexb);
    buffer = gst_buffer_new_allocate(nullptr, annexb->len, nullptr);
    g_assert_nonnull(buffer);
    g_assert_cmpint(gst_buffer_fill(buffer, 0U, annexb->data, annexb->len), ==, static_cast<gint>(annexb->len));
    GST_BUFFER_PTS(buffer) = pts;

    g_assert_cmpint(gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer), ==, GST_FLOW_OK);
    g_assert_cmpint(gst_app_src_end_of_stream(GST_APP_SRC(appsrc)), ==, GST_FLOW_OK);

    sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND);
    g_assert_nonnull(sample);
    g_assert_nonnull(sf_buffer_get_sei_meta(gst_sample_get_buffer(sample)));
    g_assert_cmpuint(sf_buffer_get_sei_meta(gst_sample_get_buffer(sample))->pri_size, ==, 1U);
    g_assert_cmpuint(sf_buffer_get_sei_meta(gst_sample_get_buffer(sample))->eoi_size, ==, 1U);

    extract_sink_pad = gst_element_get_static_pad(extract, "sink");
    g_assert_nonnull(extract_sink_pad);
    stream_id = gst_pad_get_stream_id(extract_sink_pad);
    g_assert_true(sf_sei_sync_store_lookup((stream_id != nullptr) ? stream_id : "", SF_SEI_KEY_KIND_PTS, pts,
                                           &lookup_payload));
    g_assert_cmpuint(lookup_payload.pri_size, ==, 1U);
    g_assert_cmpuint(lookup_payload.eoi_size, ==, 1U);

    sf_sei_payload_owned_clear(&lookup_payload);
    g_free(stream_id);
    gst_object_unref(extract_sink_pad);
    gst_sample_unref(sample);
    g_byte_array_unref(annexb);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

static void test_extract_attaches_meta_for_initially_nonwritable_input(void) {
    GstElement *pipeline = nullptr;
    GstElement *appsrc = nullptr;
    GstElement *extract = nullptr;
    GstElement *appsink = nullptr;
    GByteArray *annexb = nullptr;
    GstBuffer *buffer = nullptr;
    GstBuffer *held_ref = nullptr;
    GstSample *sample = nullptr;

    sf_sei_sync_store_clear();
    pipeline = create_extract_pipeline(&appsrc, &extract, &appsink);
    (void) extract;
    g_assert_cmpint(gst_element_set_state(pipeline, GST_STATE_PLAYING), !=, GST_STATE_CHANGE_FAILURE);

    annexb = build_annexb_buffer_with_cancel_pri_and_eoi();
    g_assert_nonnull(annexb);
    buffer = gst_buffer_new_allocate(nullptr, annexb->len, nullptr);
    g_assert_nonnull(buffer);
    g_assert_cmpint(gst_buffer_fill(buffer, 0U, annexb->data, annexb->len), ==, static_cast<gint>(annexb->len));
    GST_BUFFER_PTS(buffer) = 84U;
    held_ref = gst_buffer_ref(buffer); /* keep second ref so extractor input starts non-writable */
    g_assert_false(gst_buffer_is_writable(buffer));

    g_assert_cmpint(gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer), ==, GST_FLOW_OK);
    g_assert_cmpint(gst_app_src_end_of_stream(GST_APP_SRC(appsrc)), ==, GST_FLOW_OK);

    sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND);
    g_assert_nonnull(sample);
    g_assert_nonnull(sf_buffer_get_sei_meta(gst_sample_get_buffer(sample)));

    gst_sample_unref(sample);
    gst_buffer_unref(held_ref);
    g_byte_array_unref(annexb);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

static void test_reconstruct_far_separated_target_clears_source_in_new_buffer(void) {
    GstElement *pipeline = nullptr;
    GstElement *appsrc = nullptr;
    GstElement *reconstruct = nullptr;
    GstElement *appsink = nullptr;
    GstBuffer *warmup_buffer = nullptr;
    GstBuffer *test_buffer = nullptr;
    GstSample *warmup_sample = nullptr;
    GstSample *test_sample = nullptr;
    GstPad *sink_pad = nullptr;
    gchar *stream_id = nullptr;
    GByteArray *pri_payload = nullptr;
    GstMapInfo map = {0};
    GstVideoInfo info = {0};
    guint8 *y_plane = nullptr;
    guint y_stride = 0U;
    const guint64 pts = 77U;
    gchar *trace_path = nullptr;
    gchar *trace_contents = nullptr;
    gsize trace_size = 0U;

    sf_sei_sync_store_clear();
    sf_sei_sync_store_set_limits(64U, 10 * G_USEC_PER_SEC);
    pipeline = create_reconstruct_pipeline(&appsrc, &reconstruct, &appsink);
    trace_path = g_strdup_printf("%s/vcai_pri_reconstruct_trace_%u.jsonl", g_get_tmp_dir(), g_random_int());
    g_assert_nonnull(trace_path);
    g_object_set(reconstruct, "roi-trace-path", trace_path, nullptr);
    g_assert_cmpint(gst_element_set_state(pipeline, GST_STATE_PLAYING), !=, GST_STATE_CHANGE_FAILURE);

    /* Warmup frame triggers stream-start/stream-id establishment. */
    warmup_buffer = create_i420_buffer_with_markers(9U, 9U, 9U);
    GST_BUFFER_PTS(warmup_buffer) = 1U;
    g_assert_cmpint(gst_app_src_push_buffer(GST_APP_SRC(appsrc), warmup_buffer), ==, GST_FLOW_OK);
    warmup_sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND);
    g_assert_nonnull(warmup_sample);
    gst_sample_unref(warmup_sample);

    sink_pad = gst_element_get_static_pad(reconstruct, "sink");
    g_assert_nonnull(sink_pad);
    stream_id = gst_pad_get_stream_id(sink_pad);
    g_assert_nonnull(stream_id);

    pri_payload = build_pri_payload_with_multi_ratio_target(6U, 6U);
    g_assert_nonnull(pri_payload);
    g_assert_true(sf_sei_sync_store_put(stream_id, SF_SEI_KEY_KIND_PTS, pts, pri_payload->data, pri_payload->len,
                                        nullptr, 0U));

    test_buffer = create_i420_buffer_with_markers(220U, 11U, 33U);
    GST_BUFFER_PTS(test_buffer) = pts;
    g_assert_cmpint(gst_app_src_push_buffer(GST_APP_SRC(appsrc), test_buffer), ==, GST_FLOW_OK);
    g_assert_cmpint(gst_app_src_end_of_stream(GST_APP_SRC(appsrc)), ==, GST_FLOW_OK);

    test_sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 2 * GST_SECOND);
    g_assert_nonnull(test_sample);

    gst_video_info_init(&info);
    g_assert_true(gst_video_info_set_format(&info, GST_VIDEO_FORMAT_I420, 8U, 8U));
    g_assert_true(gst_buffer_map(gst_sample_get_buffer(test_sample), &map, GST_MAP_READ));
    y_plane = map.data + GST_VIDEO_INFO_PLANE_OFFSET(&info, 0);
    y_stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 0));

    g_assert_cmpuint(y_plane[(6U * y_stride) + 6U], ==, 220U); /* copied ROI target */
    g_assert_cmpuint(y_plane[(0U * y_stride) + 0U], ==, 0U); /* packed source area cleared to background */
    g_assert_cmpuint(y_plane[(6U * y_stride) + 2U], ==, 0U); /* non-ROI area uses background in new buffer path */

    gst_buffer_unmap(gst_sample_get_buffer(test_sample), &map);
    gst_sample_unref(test_sample);
    g_assert_true(g_file_get_contents(trace_path, &trace_contents, &trace_size, nullptr));
    g_assert_nonnull(g_strstr_len(trace_contents, (gssize) trace_size, "\"regions\":["));
    g_assert_nonnull(g_strstr_len(trace_contents, (gssize) trace_size, "\"pair_key\":"));
    g_assert_nonnull(g_strstr_len(trace_contents, (gssize) trace_size, "\"duplicate_pair_count\":0"));

    g_free(trace_contents);
    g_remove(trace_path);
    g_free(trace_path);
    g_byte_array_unref(pri_payload);
    g_free(stream_id);
    gst_object_unref(sink_pad);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
}

void register_vcai_decoder_integration_tests(void) {
    g_test_add_func("/integration/extract_meta_attach_and_sync_store",
                    test_extract_attaches_meta_and_publishes_sync_store);
    g_test_add_func("/integration/extract_meta_attach_nonwritable_input",
                    test_extract_attaches_meta_for_initially_nonwritable_input);
    g_test_add_func("/integration/reconstruct_far_separated_target_clears_source_in_new_buffer",
                    test_reconstruct_far_separated_target_clears_source_in_new_buffer);
}
