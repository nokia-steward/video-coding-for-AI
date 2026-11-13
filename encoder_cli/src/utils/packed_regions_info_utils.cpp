// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "packed_regions_info_utils.h"

#include <algorithm>
#include <bit>
#include <map>
#include <stdexcept>

/**
 * @file packed_regions_info_utils.cpp
 * @brief Implementation of the high-level packed_regions_info creation utility.
 */

namespace {
    /**
     * @struct ratio_key
     * @brief A key for mapping unique resampling ratios to an index.
     */
    struct ratio_key {
        int width_num, w_den, h_num, h_den;
        bool operator<(const ratio_key &other) const {
            if (width_num != other.width_num)
                return width_num < other.width_num;
            if (w_den != other.w_den)
                return w_den < other.w_den;
            if (h_num != other.h_num)
                return h_num < other.h_num;
            return h_den < other.h_den;
        }
    };

    constexpr unsigned int inferred_ratio_index = 0U;
    constexpr unsigned int first_signaled_ratio_index = 1U;
} // namespace

sf::packed_regions_info create_packed_regions_info(const std::vector<sf::rectangle> &roi_data,
                                                   const std::vector<sf::rectangle> &packed_rois,
                                                   const creation_params &params) {
    if (roi_data.size() != packed_rois.size() || roi_data.empty()) {
        throw std::invalid_argument("ROI vectors must be non-empty and of the same size.");
    }

    auto pri = sf::packed_regions_info();
    pri.cancel_flag = false;
    pri.persistence_flag = params.persistence_flag;
    pri.multilayer_flag = false;
    pri.use_max_dimensions_flag = false;
    pri.log2_unit_size = static_cast<uint8_t>(params.log2_unit_size);
    pri.region_id_present_flag = true;
    pri.target_pic_params_present_flag = true;
    pri.target_pic_width_minus1 = static_cast<uint16_t>(params.reconstructed_width - 1);
    pri.target_pic_height_minus1 = static_cast<uint16_t>(params.reconstructed_height - 1);
    pri.inferred_resampling_ratio = sf::resampling_ratio{0U, 0U, true, std::nullopt, std::nullopt};

    const auto unit_size = static_cast<unsigned int>(1 << params.log2_unit_size);
    unsigned int max_coord_val = 0;
    std::map<ratio_key, unsigned int> ratio_map;

    // First pass: Determine unique resampling ratios and find max coordinate value
    for (size_t i = 0; i < packed_rois.size(); ++i) {
        const auto &packed = packed_rois[i];
        const auto &recon = roi_data[i];
        if (packed.width != recon.width || packed.height != recon.height) {
            if (ratio_key key = {recon.width, packed.width, recon.height, packed.height}; !ratio_map.contains(key)) {
                ratio_map[key] = static_cast<unsigned int>(ratio_map.size()) + first_signaled_ratio_index;
            }
        }

        max_coord_val = std::max({max_coord_val, (static_cast<unsigned int>(packed.left) / unit_size),
                                  (static_cast<unsigned int>(packed.top) / unit_size),
                                  (static_cast<unsigned int>(packed.width - 1) / unit_size),
                                  (static_cast<unsigned int>(packed.height - 1) / unit_size),
                                  (static_cast<unsigned int>(recon.left) / unit_size),
                                  (static_cast<unsigned int>(recon.top) / unit_size)});
    }

    // Populate signaled resampling ratios (spec indices 1..N).
    if (!ratio_map.empty()) {
        pri.resampling_ratios.resize(ratio_map.size());
        for (const auto &[key, spec_index]: ratio_map) {
            auto &region_entry = pri.resampling_ratios[spec_index - first_signaled_ratio_index];
            region_entry.width_num_minus1 = static_cast<unsigned int>(key.width_num - 1);
            region_entry.width_denom_minus1 = static_cast<unsigned int>(key.w_den - 1);
            region_entry.fixed_aspect_ratio_flag = false;
            region_entry.height_num_minus1 = static_cast<unsigned int>(key.h_num - 1);
            region_entry.height_denom_minus1 = static_cast<unsigned int>(key.h_den - 1);
        }
    }
    pri.region_size_len_minus1 = max_coord_val > 0 ? static_cast<uint8_t>(std::bit_width(max_coord_val) - 1) : 0;

    // Second pass: Populate the regions vector
    for (size_t i = 0; i < packed_rois.size(); ++i) {
        const auto &packed = packed_rois[i];
        const auto &recon = roi_data[i];
        auto region_entry = sf::region();
        region_entry.id = static_cast<unsigned int>(i);
        region_entry.is_a_layer_flag = false;
        region_entry.top_left_in_units_x = static_cast<unsigned int>(packed.left) / unit_size;
        region_entry.top_left_in_units_y = static_cast<unsigned int>(packed.top) / unit_size;
        region_entry.width_in_units_minus1 = static_cast<unsigned int>(packed.width - 1) / unit_size;
        region_entry.height_in_units_minus1 = static_cast<unsigned int>(packed.height - 1) / unit_size;
        region_entry.target_region_top_left_in_units_x = static_cast<unsigned int>(recon.left) / unit_size;
        region_entry.target_region_top_left_in_units_y = static_cast<unsigned int>(recon.top) / unit_size;

        // The index field is present when at least one signaled ratio exists.
        if (!pri.resampling_ratios.empty()) {
            if (packed.width != recon.width || packed.height != recon.height) {
                ratio_key key = {recon.width, packed.width, recon.height, packed.height};
                region_entry.resampling_ratio_idx = ratio_map.at(key);
            } else {
                region_entry.resampling_ratio_idx = inferred_ratio_index;
            }
        }
        pri.regions.push_back(region_entry);
    }

    return pri;
}
