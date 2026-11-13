// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "ops/sf_sei_payload_parse.h"

#include <bitstream.h>
#include <packed_regions_info.h>
#include <vector>

namespace {

    /** @brief Reads one bit via @ref sf::bitstream_reader; @return FALSE on underrun. */
    gboolean sf_br_read_bit(sf::bitstream_reader &reader, guint *out_bit) {
        if (reader.has_overflow()) {
            return FALSE;
        }
        *out_bit = static_cast<guint>(reader.read_bit());
        return !reader.has_overflow();
    }

    /** @brief Reads \a bits bits (max 32) in MSB order into \a out_value. @return TRUE on success. */
    gboolean sf_br_read_bits(sf::bitstream_reader &reader, guint bits, guint64 *out_value) {
        if (reader.has_overflow() || bits > 32U) {
            return FALSE;
        }
        *out_value = reader.read_bits(static_cast<int>(bits));
        return !reader.has_overflow();
    }

    /** @brief Reads one unsigned exponential-Golomb-coded value. @return TRUE on success. */
    gboolean sf_br_read_ue(sf::bitstream_reader &reader, guint *out_value) {
        if (reader.has_overflow()) {
            return FALSE;
        }
        *out_value = static_cast<guint>(reader.read_ue());
        return !reader.has_overflow();
    }

} // namespace

/** @brief Validates units→pixels shift for PRI summary / consistency checks. */
static gboolean sf_pri_shift_units_to_pixels(const guint value_units, const guint log2_unit_size, guint *out_pixels) {
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

gboolean sf_parse_pri_payload(const guint8 *data, const gsize size, sf_pri_payload_info *out_info) {
    if (out_info == nullptr || (data == nullptr && size > 0U)) {
        return FALSE;
    }

    out_info->present = (data != nullptr && size > 0U);
    out_info->parse_ok = FALSE;
    out_info->cancel_flag = FALSE;
    out_info->region_count = 0U;

    if (!out_info->present) {
        return TRUE;
    }

    const std::vector<uint8_t> rbsp_storage(data, data + size);
    bool lib_ok = false;
    const sf::packed_regions_info pri = sf::read_packed_regions_info(rbsp_storage, &lib_ok);
    if (!lib_ok) {
        return FALSE;
    }

    out_info->cancel_flag = pri.cancel_flag ? TRUE : FALSE;
    if (pri.cancel_flag) {
        out_info->parse_ok = TRUE;
        return TRUE;
    }

    const guint log2_unit_size = static_cast<guint>(pri.log2_unit_size.value_or(0U));
    const bool target_present = pri.target_pic_params_present_flag.value_or(false);
    if (target_present) {
        if (!pri.target_pic_width_minus1.has_value() || !pri.target_pic_height_minus1.has_value()) {
            return FALSE;
        }
    }

    guint drawable_count = 0U;
    for (const sf::region &reg: pri.regions) {
        if (reg.is_a_layer_flag.value_or(false)) {
            continue;
        }
        if (!reg.top_left_in_units_x.has_value() || !reg.top_left_in_units_y.has_value() ||
            !reg.width_in_units_minus1.has_value() || !reg.height_in_units_minus1.has_value()) {
            return FALSE;
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
                return FALSE;
            }
            target_x_units = static_cast<guint>(*reg.target_region_top_left_in_units_x);
            target_y_units = static_cast<guint>(*reg.target_region_top_left_in_units_y);
        }

        guint ignore = 0U;
        if (!sf_pri_shift_units_to_pixels(packed_x_units, log2_unit_size, &ignore) ||
            !sf_pri_shift_units_to_pixels(packed_y_units, log2_unit_size, &ignore) ||
            !sf_pri_shift_units_to_pixels(width_units, log2_unit_size, &ignore) ||
            !sf_pri_shift_units_to_pixels(height_units, log2_unit_size, &ignore) ||
            !sf_pri_shift_units_to_pixels(target_x_units, log2_unit_size, &ignore) ||
            !sf_pri_shift_units_to_pixels(target_y_units, log2_unit_size, &ignore)) {
            return FALSE;
        }
        ++drawable_count;
    }

    out_info->parse_ok = TRUE;
    out_info->region_count = drawable_count;
    return TRUE;
}

gboolean sf_parse_eoi_payload(const guint8 *data, const gsize size, sf_eoi_payload_info *out_info) {
    guint bit = 0;
    guint64 type_bits = 0;
    guint ignored_ue = 0;
    guint64 ignored_bits = 0;

    if (out_info == nullptr || (data == nullptr && size > 0U)) {
        return FALSE;
    }

    out_info->present = (data != nullptr && size > 0U);
    out_info->parse_ok = FALSE;
    out_info->cancel_flag = FALSE;
    out_info->type_bits = 0U;
    out_info->has_luma_adaptation = FALSE;
    out_info->luma_adaptation_idc = 0U;
    out_info->ratio_luma_value = 0U;
    out_info->ratio_luma_sign_flag = FALSE;
    out_info->backscale_ratio_value = 0U;

    if (!out_info->present) {
        return TRUE;
    }

    std::vector<uint8_t> rbsp_storage(data, data + size);
    sf::bitstream_reader br(rbsp_storage);

    if (!sf_br_read_bit(br, &bit)) {
        return FALSE;
    }
    out_info->cancel_flag = (bit != 0U);
    if (out_info->cancel_flag) {
        out_info->parse_ok = TRUE;
        return TRUE;
    }

    if (!sf_br_read_bit(br, &bit)) {
        return FALSE;
    }
    if (!sf_br_read_bits(br, 2U, &ignored_bits)) {
        return FALSE;
    }
    if (!sf_br_read_bits(br, 2U, &ignored_bits)) {
        return FALSE;
    }
    if (!sf_br_read_bits(br, 2U, &ignored_bits)) {
        return FALSE;
    }
    if (!sf_br_read_bits(br, 16U, &type_bits)) {
        return FALSE;
    }
    out_info->type_bits = static_cast<guint16>(type_bits);

    if ((out_info->type_bits & 0x01U) != 0U) {
        guint64 object_based_idc = 0;
        if (!sf_br_read_bits(br, 16U, &object_based_idc)) {
            return FALSE;
        }
        if ((object_based_idc & 0x02U) != 0U) {
            if (!sf_br_read_ue(br, &ignored_ue)) {
                return FALSE;
            }
            if (ignored_ue > 0U) {
                if (!sf_br_read_bit(br, &bit)) {
                    return FALSE;
                }
            }
        }
    }

    if ((out_info->type_bits & 0x02U) != 0U) {
        guint temporal_resampling_type_flag = 0;
        if (!sf_br_read_bit(br, &temporal_resampling_type_flag)) {
            return FALSE;
        }
        if (!sf_br_read_ue(br, &ignored_ue)) {
            return FALSE;
        }
        if (temporal_resampling_type_flag != 0U && ignored_ue > 0U) {
            if (!sf_br_read_bit(br, &bit)) {
                return FALSE;
            }
        }
    }

    if ((out_info->type_bits & 0x04U) != 0U) {
        guint orig_pic_dimensions_flag = 0;
        if (!sf_br_read_bit(br, &orig_pic_dimensions_flag)) {
            return FALSE;
        }
        if (orig_pic_dimensions_flag != 0U) {
            if (!sf_br_read_bits(br, 16U, &ignored_bits)) {
                return FALSE;
            }
            if (!sf_br_read_bits(br, 16U, &ignored_bits)) {
                return FALSE;
            }
        } else {
            if (!sf_br_read_bits(br, 2U, &ignored_bits)) {
                return FALSE;
            }
            if (!sf_br_read_bits(br, 2U, &ignored_bits)) {
                return FALSE;
            }
        }
    }

    if ((out_info->type_bits & 0x20U) != 0U) {
        if (!sf_br_read_bits(br, 16U, &ignored_bits)) {
            return FALSE;
        }
        if (!sf_br_read_bits(br, 8U, &ignored_bits)) {
            return FALSE;
        }
    }

    if ((out_info->type_bits & 0x40U) != 0U) {
        guint64 value = 0;
        out_info->has_luma_adaptation = TRUE;
        if (!sf_br_read_ue(br, &out_info->luma_adaptation_idc)) {
            return FALSE;
        }
        if (!sf_br_read_bits(br, 7U, &value)) {
            return FALSE;
        }
        out_info->ratio_luma_value = static_cast<guint>(value);
        if (!sf_br_read_bit(br, &bit)) {
            return FALSE;
        }
        out_info->ratio_luma_sign_flag = (bit != 0U);
        if (out_info->luma_adaptation_idc == 0U) {
            if (!sf_br_read_bits(br, 4U, &ignored_bits)) {
                return FALSE;
            }
        }
        if (!sf_br_read_bits(br, 7U, &value)) {
            return FALSE;
        }
        out_info->backscale_ratio_value = static_cast<guint>(value);
    }

    out_info->parse_ok = TRUE;
    return TRUE;
}
