// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <string>
#include "../../../../libvcai/include/rectangle.h"

/**
 * @file slot_reservation.height
 * @brief Declares slot reservation structure for low delay packer.
 */

/**
 * @brief Reservation for a recently missing ROI slot.
 */
struct slot_reservation {
    sf::rectangle slot;
    std::string owner_id;
    int frames_remaining;
};
