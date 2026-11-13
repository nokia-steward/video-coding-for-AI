// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../../libvcai/include/packed_regions_info.h"

#include <cstdint>

/**
 * @brief Associates packed-regions payload with strict frame sequence identity.
 */
struct metadata_frame_bundle {
    uint64_t sequence_id = 0;
    sf::packed_regions_info regions{};
};
