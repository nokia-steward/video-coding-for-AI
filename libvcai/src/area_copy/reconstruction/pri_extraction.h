// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <utility>
#include <vector>
#include "../../../include/area_copy.h"
#include "../../../include/packed_regions_info.h"
#include "../../../include/rectangle.h"

namespace sf {
    /**
     * @brief Extracts rectangle data from PRI information.
     * @param pri_info PRI information.
     * @return Pair of (packed_rects, target_rects) vectors.
     */
    [[nodiscard]] std::pair<std::vector<rectangle>, std::vector<rectangle>>
    extract_rectangles_from_pri(const packed_regions_info &pri_info);
} // namespace sf
