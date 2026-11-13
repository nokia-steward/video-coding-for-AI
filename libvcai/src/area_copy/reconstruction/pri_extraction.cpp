// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../include/area_copy.h"
#include "../../../include/logging.h"
#include "../../../include/packed_regions_info.h"
#include "../../../include/rectangle.h"

#include <gst/gst.h>
#include <vector>

// Debug category for area copy operations.
GST_DEBUG_CATEGORY_STATIC(area_copy);
#define GST_CAT_DEFAULT area_copy

namespace sf {
    namespace {
        /**
         * @brief Extracts a single rectangle from PRI region data.
         * @param region PRI region.
         * @param unit_size Unit size in pixels.
         * @return Optional packed rectangle if region is complete.
         */
        [[nodiscard]] std::optional<rectangle> extract_packed_rect_from_region(const region &region,
                                                                               const unsigned int unit_size) {
            if (!region.top_left_in_units_x || !region.top_left_in_units_y || !region.width_in_units_minus1 ||
                !region.height_in_units_minus1) {
                return std::nullopt;
            }

            return rectangle(static_cast<int>(*region.top_left_in_units_x * unit_size),
                             static_cast<int>(*region.top_left_in_units_y * unit_size),
                             static_cast<int>((*region.width_in_units_minus1 + 1) * unit_size),
                             static_cast<int>((*region.height_in_units_minus1 + 1) * unit_size));
        }

        /**
         * @brief Extracts target rectangle from PRI region data.
         * @param region PRI region.
         * @param unit_size Unit size in pixels.
         * @param packed_w Packed rectangle width.
         * @param packed_h Packed rectangle height.
         * @return Optional target rectangle if region is complete.
         */
        [[nodiscard]] std::optional<rectangle> extract_target_rect_from_region(const region &region,
                                                                               const unsigned int unit_size,
                                                                               const int packed_w, const int packed_h) {
            if (!region.target_region_top_left_in_units_x || !region.target_region_top_left_in_units_y) {
                return std::nullopt;
            }

            return rectangle(static_cast<int>(*region.target_region_top_left_in_units_x * unit_size),
                             static_cast<int>(*region.target_region_top_left_in_units_y * unit_size), packed_w,
                             packed_h);
        }
    } // namespace

    /**
     * @brief Extracts rectangle data from PRI information.
     * @param pri_info PRI information.
     * @return Pair of (packed_rects, target_rects) vectors.
     */
    [[nodiscard]] std::pair<std::vector<rectangle>, std::vector<rectangle>>
    extract_rectangles_from_pri(const packed_regions_info &pri_info) {
        std::vector<rectangle> packed_rects;
        std::vector<rectangle> target_rects;

        const auto log2_unit_size = pri_info.log2_unit_size.value_or(0);
        const auto unit_size = 1U << log2_unit_size;

        for (const auto &region: pri_info.regions) {
            const auto packed_rect_opt = extract_packed_rect_from_region(region, unit_size);
            if (!packed_rect_opt) {
                warning(area_copy, "Skipping incomplete region in PRI message.");
                continue;
            }

            const rectangle packed_rect = *packed_rect_opt;
            packed_rects.push_back(packed_rect);

            const auto target_rect_opt =
                    extract_target_rect_from_region(region, unit_size, packed_rect.width, packed_rect.height);
            if (!target_rect_opt) {
                warning(area_copy, "Skipping incomplete target region in PRI message.");
                continue;
            }

            target_rects.push_back(*target_rect_opt);
        }

        return {packed_rects, target_rects};
    }
} // namespace sf
