// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "core/sf_sei_sync_store.h"
#include "meta/sf_sei_meta.h"
#include "ops/sf_eoi_luma_ops.h"
#include "ops/sf_pri_reconstruct_ops.h"
#include "ops/sf_tet_tiles.h"

#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <packed_regions_info.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vector>

void register_vcai_decoder_integration_tests(void);

typedef struct {
    gchar magic[4];
    guint32 version;
    guint32 num_qps;
    guint32 num_channels;
    guint32 width;
    guint32 height;
    guint32 normalization_type;
    gfloat scale_factor;
    gfloat data_min;
    gfloat data_max;
} sf_tet_header_raw;

typedef struct {
    GByteArray *bytes;
    guint bit_offset;
} TestBitWriter;

static void test_bit_writer_init(TestBitWriter *writer) {
    writer->bytes = g_byte_array_new();
    writer->bit_offset = 0U;
}

static void test_bit_writer_write_bit(TestBitWriter *writer, const guint bit) {
    guint8 value = (bit != 0U) ? 1U : 0U;

    if (writer->bit_offset == 0U) {
        const guint8 zero = 0U;
        g_byte_array_append(writer->bytes, &zero, 1U);
    }

    writer->bytes->data[writer->bytes->len - 1U] |= static_cast<guint8>(value << (7U - writer->bit_offset));
    writer->bit_offset = (writer->bit_offset + 1U) % 8U;
}

static void test_bit_writer_write_bits(TestBitWriter *writer, const guint value, const guint num_bits) {
    for (gint bit_index = static_cast<gint>(num_bits) - 1; bit_index >= 0; --bit_index) {
        const guint bit = (value >> static_cast<guint>(bit_index)) & 0x01U;
        test_bit_writer_write_bit(writer, bit);
    }
}

static void test_bit_writer_write_ue(TestBitWriter *writer, const guint value) {
    const guint code_num = value + 1U;
    guint leading_zero_bits = 0U;
    guint temp = code_num;

    while (temp > 1U) {
        temp >>= 1U;
        ++leading_zero_bits;
    }

    for (guint index = 0U; index < leading_zero_bits; ++index) {
        test_bit_writer_write_bit(writer, 0U);
    }
    test_bit_writer_write_bits(writer, code_num, leading_zero_bits + 1U);
}

static GByteArray *build_pri_payload_with_multi_ratio_target(const guint target_x_units, const guint target_y_units) {
    TestBitWriter writer = {0};

    test_bit_writer_init(&writer);

    test_bit_writer_write_bit(&writer, 0U); /* cancel_flag */
    test_bit_writer_write_bit(&writer, 1U); /* persistence_flag */
    test_bit_writer_write_ue(&writer, 0U); /* num_regions_minus1 */
    test_bit_writer_write_bit(&writer, 0U); /* multilayer_flag */
    test_bit_writer_write_bit(&writer, 0U); /* use_max_dimensions_flag */
    test_bit_writer_write_bits(&writer, 0U, 4U); /* log2_unit_size */
    test_bit_writer_write_bits(&writer, 3U, 4U); /* region_size_len_minus1 => 4-bit fields */
    test_bit_writer_write_bit(&writer, 0U); /* region_id_present_flag */
    test_bit_writer_write_bit(&writer, 1U); /* target_pic_params_present_flag */
    /* 8x8 target picture (matches typical decoded frame caps in tests). */
    test_bit_writer_write_bits(&writer, 7U, 16U); /* target_width_minus1 */
    test_bit_writer_write_bits(&writer, 7U, 16U); /* target_height_minus1 */

    test_bit_writer_write_ue(&writer, 2U); /* num_resampling_ratios_minus1 */
    test_bit_writer_write_ue(&writer, 0U); /* ratio[0] width_num_minus1 */
    test_bit_writer_write_ue(&writer, 0U); /* ratio[0] width_denom_minus1 */
    test_bit_writer_write_bit(&writer, 1U); /* ratio[0] fixed_aspect_ratio_flag */
    test_bit_writer_write_ue(&writer, 1U); /* ratio[1] width_num_minus1 */
    test_bit_writer_write_ue(&writer, 1U); /* ratio[1] width_denom_minus1 */
    test_bit_writer_write_bit(&writer, 1U); /* ratio[1] fixed_aspect_ratio_flag */

    test_bit_writer_write_bits(&writer, 0U, 4U); /* packed_x */
    test_bit_writer_write_bits(&writer, 0U, 4U); /* packed_y */
    test_bit_writer_write_bits(&writer, 1U, 4U); /* width_minus1 */
    test_bit_writer_write_bits(&writer, 1U, 4U); /* height_minus1 */
    test_bit_writer_write_bits(&writer, 2U, 2U); /* resampling_ratio_idx; requires ceil(log2(3)) == 2 bits */
    test_bit_writer_write_bits(&writer, target_x_units, 4U); /* target_x */
    test_bit_writer_write_bits(&writer, target_y_units, 4U); /* target_y */

    return writer.bytes;
}

static GstBuffer *create_i420_buffer(guint width, guint height, guint8 fill_y, guint8 fill_u, guint8 fill_v,
                                     GstVideoInfo *out_info) {
    GstVideoInfo info = {0};
    GstBuffer *buffer = nullptr;
    GstMapInfo map = {0};
    guint8 *y = nullptr;
    guint8 *u = nullptr;
    guint8 *v = nullptr;

    gst_video_info_init(&info);
    if (!gst_video_info_set_format(&info, GST_VIDEO_FORMAT_I420, width, height)) {
        return nullptr;
    }

    buffer = gst_buffer_new_allocate(nullptr, info.size, nullptr);
    if (buffer == nullptr) {
        return nullptr;
    }
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        return nullptr;
    }

    y = map.data + GST_VIDEO_INFO_PLANE_OFFSET(&info, 0);
    u = map.data + GST_VIDEO_INFO_PLANE_OFFSET(&info, 1);
    v = map.data + GST_VIDEO_INFO_PLANE_OFFSET(&info, 2);
    memset(y, fill_y,
           static_cast<gsize>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 0)) *
                   static_cast<gsize>(GST_VIDEO_INFO_COMP_HEIGHT(&info, 0)));
    memset(u, fill_u,
           static_cast<gsize>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 1)) *
                   static_cast<gsize>(GST_VIDEO_INFO_COMP_HEIGHT(&info, 1)));
    memset(v, fill_v,
           static_cast<gsize>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 2)) *
                   static_cast<gsize>(GST_VIDEO_INFO_COMP_HEIGHT(&info, 2)));

    gst_buffer_unmap(buffer, &map);
    if (out_info != nullptr) {
        *out_info = info;
    }
    return buffer;
}

static void test_sync_store_roundtrip_and_consume(void) {
    const guint8 pri_data[] = {0x11, 0x22, 0x33};
    const guint8 eoi_data[] = {0x44, 0x55};
    sf_sei_payload_owned payload = {0};
    sf_sei_payload_owned consumed = {0};

    sf_sei_sync_store_clear();
    sf_sei_sync_store_set_limits(8U, 10 * G_USEC_PER_SEC);
    g_assert_true(sf_sei_sync_store_put("stream-a", SF_SEI_KEY_KIND_PTS, 1000U, pri_data, sizeof(pri_data), eoi_data,
                                        sizeof(eoi_data)));

    g_assert_true(sf_sei_sync_store_lookup("stream-a", SF_SEI_KEY_KIND_PTS, 1000U, &payload));
    g_assert_cmpuint(payload.pri_size, ==, sizeof(pri_data));
    g_assert_cmpuint(payload.eoi_size, ==, sizeof(eoi_data));
    g_assert_cmpmem(payload.pri_data, payload.pri_size, pri_data, sizeof(pri_data));
    g_assert_cmpmem(payload.eoi_data, payload.eoi_size, eoi_data, sizeof(eoi_data));

    g_assert_true(sf_sei_sync_store_consume("stream-a", SF_SEI_KEY_KIND_PTS, 1000U, &consumed));
    g_assert_cmpmem(consumed.pri_data, consumed.pri_size, pri_data, sizeof(pri_data));
    g_assert_false(sf_sei_sync_store_lookup("stream-a", SF_SEI_KEY_KIND_PTS, 1000U, nullptr));

    sf_sei_payload_owned_clear(&payload);
    sf_sei_payload_owned_clear(&consumed);
}

static void test_sync_store_size_eviction(void) {
    sf_sei_sync_store_clear();
    sf_sei_sync_store_set_limits(1U, 10 * G_USEC_PER_SEC);

    g_assert_true(sf_sei_sync_store_put("stream-a", SF_SEI_KEY_KIND_PTS, 1U, nullptr, 0U, nullptr, 0U));
    g_usleep(100U);
    g_assert_true(sf_sei_sync_store_put("stream-a", SF_SEI_KEY_KIND_PTS, 2U, nullptr, 0U, nullptr, 0U));

    g_assert_false(sf_sei_sync_store_lookup("stream-a", SF_SEI_KEY_KIND_PTS, 1U, nullptr));
    g_assert_true(sf_sei_sync_store_lookup("stream-a", SF_SEI_KEY_KIND_PTS, 2U, nullptr));
}

static void test_sync_store_supports_dts_and_offset_keys(void) {
    const guint8 pri_data[] = {0xAB};
    const guint8 eoi_data[] = {0xCD, 0xEF};
    sf_sei_payload_owned payload = {0};

    sf_sei_sync_store_clear();
    sf_sei_sync_store_set_limits(8U, 10 * G_USEC_PER_SEC);

    g_assert_true(sf_sei_sync_store_put("stream-b", SF_SEI_KEY_KIND_DTS, 7U, pri_data, sizeof(pri_data), eoi_data,
                                        sizeof(eoi_data)));
    g_assert_true(sf_sei_sync_store_lookup("stream-b", SF_SEI_KEY_KIND_DTS, 7U, &payload));
    g_assert_cmpuint(payload.pri_size, ==, sizeof(pri_data));
    g_assert_cmpuint(payload.eoi_size, ==, sizeof(eoi_data));
    sf_sei_payload_owned_clear(&payload);

    g_assert_true(
            sf_sei_sync_store_put("stream-c", SF_SEI_KEY_KIND_OFFSET, 99U, pri_data, sizeof(pri_data), nullptr, 0U));
    g_assert_true(sf_sei_sync_store_consume("stream-c", SF_SEI_KEY_KIND_OFFSET, 99U, &payload));
    g_assert_cmpuint(payload.pri_size, ==, sizeof(pri_data));
    g_assert_cmpuint(payload.eoi_size, ==, 0U);
    sf_sei_payload_owned_clear(&payload);
}

static void test_sei_meta_attach_succeeds_on_writable_buffer(void) {
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, 8U, nullptr);
    const guint8 pri_data[] = {0xAA, 0xBB};
    const guint8 eoi_data[] = {0xCC};
    sf_sei_meta *meta = nullptr;

    g_assert_nonnull(buffer);
    g_assert_true(gst_buffer_is_writable(buffer));

    meta = sf_buffer_add_sei_meta(buffer, pri_data, sizeof(pri_data), eoi_data, sizeof(eoi_data));
    g_assert_nonnull(meta);
    g_assert_cmpuint(meta->pri_size, ==, sizeof(pri_data));
    g_assert_cmpuint(meta->eoi_size, ==, sizeof(eoi_data));
    g_assert_cmpmem(meta->pri_data, meta->pri_size, pri_data, sizeof(pri_data));
    g_assert_cmpmem(meta->eoi_data, meta->eoi_size, eoi_data, sizeof(eoi_data));

    gst_buffer_unref(buffer);
}

static void test_sei_meta_attach_succeeds_after_make_writable(void) {
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, 8U, nullptr);
    GstBuffer *held_ref = nullptr;
    GstBuffer *writable = nullptr;
    const guint8 pri_data[] = {0x5A};

    g_assert_nonnull(buffer);
    held_ref = gst_buffer_ref(buffer);
    g_assert_nonnull(held_ref);
    g_assert_false(gst_buffer_is_writable(buffer));

    writable = gst_buffer_make_writable(buffer);
    g_assert_nonnull(writable);
    g_assert_true(gst_buffer_is_writable(writable));
    g_assert_nonnull(sf_buffer_add_sei_meta(writable, pri_data, sizeof(pri_data), nullptr, 0U));

    gst_buffer_unref(held_ref);
    gst_buffer_unref(writable);
}

static void test_sei_meta_attach_allows_empty_payload_without_false_positive_bytes(void) {
    GstBuffer *buffer = gst_buffer_new_allocate(nullptr, 8U, nullptr);
    sf_sei_meta *meta = nullptr;

    g_assert_nonnull(buffer);
    meta = sf_buffer_add_sei_meta(buffer, nullptr, 0U, nullptr, 0U);
    g_assert_nonnull(meta);
    g_assert_cmpuint(meta->pri_size, ==, 0U);
    g_assert_cmpuint(meta->eoi_size, ==, 0U);
    g_assert_null(meta->pri_data);
    g_assert_null(meta->eoi_data);

    gst_buffer_unref(buffer);
}

static void test_pri_parse_paths_absent_cancel_and_malformed(void) {
    sf_pri_payload_info info = {0};
    const guint8 cancel_payload[] = {0x80}; /* cancel_flag = 1 */
    const guint8 malformed_payload[] = {0x00};

    g_assert_true(sf_parse_pri_payload(nullptr, 0U, &info));
    g_assert_false(info.present);
    g_assert_false(info.parse_ok);
    g_assert_cmpuint(info.region_count, ==, 0U);

    g_assert_true(sf_parse_pri_payload(cancel_payload, sizeof(cancel_payload), &info));
    g_assert_true(info.present);
    g_assert_true(info.parse_ok);
    g_assert_true(info.cancel_flag);
    g_assert_cmpuint(info.region_count, ==, 0U);

    g_assert_false(sf_parse_pri_payload(malformed_payload, sizeof(malformed_payload), &info));
}

static void test_pri_parse_multi_ratio_keeps_target_coordinates_for_roi_reconstruction(void) {
    const guint expected_target_x = 6U;
    const guint expected_target_y = 7U;
    GByteArray *payload = build_pri_payload_with_multi_ratio_target(expected_target_x, expected_target_y);

    g_assert_nonnull(payload);
    {
        bool lib_ok = false;
        const std::vector<uint8_t> buf(payload->data, payload->data + payload->len);
        const sf::packed_regions_info pri = sf::read_packed_regions_info(buf, &lib_ok);
        g_assert_true(lib_ok);
        g_assert_false(pri.cancel_flag);
        g_assert_cmpuint(pri.regions.size(), ==, 1U);
        const sf::region &reg = pri.regions[0];
        g_assert_true(reg.target_region_top_left_in_units_x.has_value());
        g_assert_true(reg.target_region_top_left_in_units_y.has_value());
        g_assert_cmpuint(*reg.target_region_top_left_in_units_x, ==, expected_target_x);
        g_assert_cmpuint(*reg.target_region_top_left_in_units_y, ==, expected_target_y);
    }

    g_byte_array_unref(payload);
}

static void test_pri_reconstruct_i420(void) {
    GstVideoInfo info = {0};
    GstBuffer *buffer = create_i420_buffer(8U, 8U, 0U, 128U, 64U, &info);
    GstMapInfo map = {0};
    sf::packed_regions_info pri = {};
    sf::region region = {};
    sf_pri_reconstruct_stats stats = {0};
    guint8 before_value = 0U;
    guint8 after_value = 0U;
    guint stride = 0;

    g_assert_nonnull(buffer);
    g_assert_true(gst_buffer_map(buffer, &map, GST_MAP_WRITE));
    stride = static_cast<guint>(GST_VIDEO_INFO_PLANE_STRIDE(&info, 0));
    map.data[0] = 77U;
    map.data[stride + 1U] = 33U;
    gst_buffer_unmap(buffer, &map);

    pri.cancel_flag = false;
    pri.log2_unit_size = static_cast<uint8_t>(0U);
    /* Distinct target top-left (4,4) vs packed (0,0) requires signaled target picture params (bitstream semantics). */
    pri.target_pic_params_present_flag = true;
    pri.target_pic_width_minus1 = 7U;
    pri.target_pic_height_minus1 = 7U;
    region.top_left_in_units_x = 0U;
    region.top_left_in_units_y = 0U;
    region.width_in_units_minus1 = 3U;
    region.height_in_units_minus1 = 3U;
    region.target_region_top_left_in_units_x = 4U;
    region.target_region_top_left_in_units_y = 4U;
    pri.regions.push_back(region);

    g_assert_true(gst_buffer_map(buffer, &map, GST_MAP_READ));
    before_value = map.data[0];
    gst_buffer_unmap(buffer, &map);

    g_assert_true(sf_pri_reconstruct_in_place(buffer, &info, pri, &stats));
    g_assert_cmpuint(stats.copied_regions, ==, 1U);

    g_assert_true(gst_buffer_map(buffer, &map, GST_MAP_READ));
    after_value = map.data[(4U * stride) + 4U];
    gst_buffer_unmap(buffer, &map);
    g_assert_cmpuint(after_value, ==, before_value);

    gst_buffer_unref(buffer);
}

static void test_eoi_parse_paths_absent_cancel_and_malformed(void) {
    sf_eoi_payload_info eoi = {0};
    const guint8 cancel_payload[] = {0x80}; /* cancel_flag = 1 */
    const guint8 malformed_payload[] = {0x00}; /* cancel_flag = 0 but too short for mandatory fields */

    g_assert_true(sf_parse_eoi_payload(nullptr, 0U, &eoi));
    g_assert_false(eoi.present);
    g_assert_false(eoi.parse_ok);

    g_assert_true(sf_parse_eoi_payload(cancel_payload, sizeof(cancel_payload), &eoi));
    g_assert_true(eoi.present);
    g_assert_true(eoi.parse_ok);
    g_assert_true(eoi.cancel_flag);

    g_assert_false(sf_parse_eoi_payload(malformed_payload, sizeof(malformed_payload), &eoi));
}

static void test_eoi_luma_multiplier_apply(void) {
    sf_eoi_payload_info eoi = {0};
    GstVideoInfo info = {0};
    GstBuffer *buffer = create_i420_buffer(4U, 4U, 20U, 128U, 64U, &info);
    GstMapInfo map = {0};
    guint8 value = 0U;
    gdouble multiplier = 0.0;

    g_assert_nonnull(buffer);

    eoi.present = TRUE;
    eoi.parse_ok = TRUE;
    eoi.cancel_flag = FALSE;
    eoi.has_luma_adaptation = TRUE;
    eoi.luma_adaptation_idc = 1U;
    eoi.ratio_luma_sign_flag = TRUE;
    eoi.ratio_luma_value = 50U;
    eoi.backscale_ratio_value = 100U;

    g_assert_true(sf_compute_eoi_luma_multiplier(&eoi, &multiplier));
    g_assert_cmpfloat(multiplier, ==, 2.0);
    g_assert_true(sf_apply_luma_multiplier_in_place(buffer, &info, multiplier));

    g_assert_true(gst_buffer_map(buffer, &map, GST_MAP_READ));
    value = map.data[0];
    gst_buffer_unmap(buffer, &map);
    g_assert_cmpuint(value, ==, 40U);

    gst_buffer_unref(buffer);
}

static void test_eoi_luma_multiplier_rejects_cancel_and_unsupported_idc(void) {
    sf_eoi_payload_info cancel_eoi = {0};
    sf_eoi_payload_info unsupported_eoi = {0};
    gdouble multiplier = 0.0;

    cancel_eoi.present = TRUE;
    cancel_eoi.parse_ok = TRUE;
    cancel_eoi.cancel_flag = TRUE;
    cancel_eoi.has_luma_adaptation = TRUE;
    cancel_eoi.luma_adaptation_idc = 1U;
    cancel_eoi.ratio_luma_sign_flag = TRUE;
    cancel_eoi.ratio_luma_value = 10U;
    cancel_eoi.backscale_ratio_value = 100U;
    g_assert_false(sf_compute_eoi_luma_multiplier(&cancel_eoi, &multiplier));

    unsupported_eoi.present = TRUE;
    unsupported_eoi.parse_ok = TRUE;
    unsupported_eoi.cancel_flag = FALSE;
    unsupported_eoi.has_luma_adaptation = TRUE;
    unsupported_eoi.luma_adaptation_idc = 0U;
    unsupported_eoi.ratio_luma_sign_flag = TRUE;
    unsupported_eoi.ratio_luma_value = 10U;
    unsupported_eoi.backscale_ratio_value = 100U;
    g_assert_false(sf_compute_eoi_luma_multiplier(&unsupported_eoi, &multiplier));
}

static gchar *create_temp_tet_file(void) {
    gchar *tmp_path = nullptr;
    gint fd = g_file_open_tmp("sf_tet_XXXXXX.bin", &tmp_path, nullptr);
    FILE *file = nullptr;
    sf_tet_header_raw header = {{'T', 'E', 'T', '\0'}, 1U, 2U, 1U, 2U, 2U, 0U, 1.0f, -1.0f, 1.0f};
    guint32 qp_values[2] = {22U, 32U};
    gfloat tiles[8] = {
            0.1f, 0.1f, 0.1f, 0.1f, /* qp 22 */
            0.2f, 0.2f, 0.2f, 0.2f /* qp 32 */
    };

    g_assert_cmpint(fd, >=, 0);
    file = fdopen(fd, "wb");
    g_assert_nonnull(file);

    g_assert_cmpuint(fwrite(&header, sizeof(header), 1U, file), ==, 1U);
    g_assert_cmpuint(fwrite(qp_values, sizeof(guint32), 2U, file), ==, 2U);
    g_assert_cmpuint(fwrite(tiles, sizeof(gfloat), 8U, file), ==, 8U);
    g_assert_cmpint(fclose(file), ==, 0);
    return tmp_path;
}

static void test_tet_load_and_apply(void) {
    gchar *tile_path = create_temp_tet_file();
    sf_tet_tile_set tiles = {0};
    GstVideoInfo info = {0};
    GstBuffer *buffer = create_i420_buffer(4U, 4U, 100U, 128U, 64U, &info);
    GstMapInfo map = {0};
    guint8 value = 0U;

    g_assert_nonnull(buffer);
    g_assert_true(sf_tet_tile_set_load(tile_path, &tiles));
    g_assert_cmpuint(sf_tet_find_tile_index_for_qp(&tiles, 27U), ==, 0U);
    g_assert_true(sf_tet_apply_i420_in_place(buffer, &info, &tiles, 0U));

    g_assert_true(gst_buffer_map(buffer, &map, GST_MAP_READ));
    value = map.data[0];
    gst_buffer_unmap(buffer, &map);
    g_assert_cmpuint(value, ==, 126U); /* 100 + round(0.1*255)=26 */

    sf_tet_tile_set_clear(&tiles);
    gst_buffer_unref(buffer);
    g_remove(tile_path);
    g_free(tile_path);
}

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    g_test_init(&argc, &argv, nullptr);

    g_test_add_func("/sync_store/roundtrip_consume", test_sync_store_roundtrip_and_consume);
    g_test_add_func("/sync_store/size_eviction", test_sync_store_size_eviction);
    g_test_add_func("/sync_store/dts_offset_keys", test_sync_store_supports_dts_and_offset_keys);
    g_test_add_func("/meta/attach_writable", test_sei_meta_attach_succeeds_on_writable_buffer);
    g_test_add_func("/meta/attach_after_make_writable", test_sei_meta_attach_succeeds_after_make_writable);
    g_test_add_func("/meta/attach_empty_payload",
                    test_sei_meta_attach_allows_empty_payload_without_false_positive_bytes);
    g_test_add_func("/pri/parse_paths", test_pri_parse_paths_absent_cancel_and_malformed);
    g_test_add_func("/pri/parse_multi_ratio_target_coordinates",
                    test_pri_parse_multi_ratio_keeps_target_coordinates_for_roi_reconstruction);
    g_test_add_func("/pri/reconstruct_i420", test_pri_reconstruct_i420);
    g_test_add_func("/eoi/parse_paths", test_eoi_parse_paths_absent_cancel_and_malformed);
    g_test_add_func("/eoi/luma_multiplier_apply", test_eoi_luma_multiplier_apply);
    g_test_add_func("/eoi/luma_multiplier_rejects_invalid_states",
                    test_eoi_luma_multiplier_rejects_cancel_and_unsupported_idc);
    g_test_add_func("/tet/load_and_apply", test_tet_load_and_apply);
    register_vcai_decoder_integration_tests();

    return g_test_run();
}
