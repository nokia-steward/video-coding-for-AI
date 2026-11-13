// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <vector>

/**
 * @file qp_map_frame_payload.height
 * @brief Per-frame QP-map payload passed to encoder backends.
 */
struct qp_map_frame_payload {
    int frame_number = -1;
    int width = 0;
    int height = 0;
    int ctb_size = 64;
    int background_delta = 0;
    std::vector<int8_t> delta_map{};
};
