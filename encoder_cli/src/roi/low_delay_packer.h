// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "low_delay_packer/history.h"
#include "low_delay_packer/mapping.h"
#include "low_delay_packer/slot_reservation.h"
#include "packer_common.h"

/**
 * @file low_delay_packer.height
 * @brief Declares the low delay rectangle packer with ROI accumulation and merging.
 */

/**
 * @brief A low-delay rectangle packer with ROI accumulation and merging.
 *
 * This packer maintains a history of the last num_frames ROIs for each object,
 * accumulates them (bounding box union), merges overlapping accumulated ROIs
 * (30% threshold, recursive), and packs them close to the top-left corner.
 */
struct low_delay_packer {
    /**
     * @brief Constructor for the low delay packer.
     * @param max_width_param Maximum width of the packing area
     * @param max_height_param Maximum height of the packing area
     * @param ctu_size_param CTU cell size for CTU-aligned packing (default: 64)
     * @param num_frames_param Number of frames to accumulate for each object (default: 3)
     */
    low_delay_packer(int max_width_param, int max_height_param, int ctu_size_param, int num_frames_param);

    // Disable copy constructor and assignment operator due to reference member
    low_delay_packer(const low_delay_packer &) = delete;
    low_delay_packer &operator=(const low_delay_packer &) = delete;

    /**
     * @brief Pack rectangles with position stability (drop-in replacement for get_packed_rectangles).
     * @param input_rectangles The rectangles to pack (will be assigned IDs based on position/size)
     * @param frame_number The frame number for logging purposes
     * @return An optional pack_result. On success, it contains the packed rectangles and the
     *         axis-aligned bounding box of the packed area. On failure, it's std::nullopt.
     */
    [[nodiscard]] std::optional<pack_result> get_packed_rectangles(const std::vector<sf::rectangle> &input_rectangles,
                                                                   int frame_number = -1);
    /**
     * @brief Pack rectangles with explicit IDs for better position stability.
     * @param rectangles_with_ids Vector of rectangles with their IDs
     * @param frame_number The frame number for logging purposes
     * @return An optional pack_result. On success, it contains the packed rectangles and the
     *         axis-aligned bounding box of the packed area. On failure, it's std::nullopt.
     */
    [[nodiscard]] std::optional<pack_result>
    get_packed_rectangles_with_ids(const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
                                   int frame_number = -1);

    /**
     * @brief Pack rectangles with position stability (internal method).
     * @param rectangles Vector of rectangles with IDs to pack
     * @param frame_number The frame number for logging purposes
     * @return Optional pack result, nullopt if packing fails
     */
    [[nodiscard]] std::optional<stable_pack_result> pack_rectangles(const std::vector<stable_rectangle> &rectangles,
                                                                    int frame_number = -1);

    /**
     * @brief Clear the position memory (useful for resetting between different packing sessions).
     */
    void clear_memory();

    /**
     * @brief Get statistics about the current packing state.
     */
    struct stats {
        size_t total_rectangles_remembered;
        size_t total_packing_calls;
        double average_stability_ratio;
    };
    [[nodiscard]] stats get_stats() const;

    /**
     * @brief Calculate the weight of a rectangle (weight = area).
     * @param rect The rectangle to calculate weight for
     * @return The weight (area) of the rectangle
     */
    static int get_weight(const sf::rectangle &rect);

private:
    int max_width;
    int max_height;
    int ctu_size;
    int num_frames;
    int pack_call_counter;

    // Memory of previous positions
    std::unordered_map<std::string, position_slot> position_memory;

    // Slot reservations for recently missing ROIs
    std::unordered_map<std::string, slot_reservation> slot_reservations;

    // ROI history for accumulation
    std::unordered_map<std::string, roi_history> roi_histories;

    // Statistics
    size_t total_stable_count;
    size_t total_repacked_count;

    int compaction_wait_counter;

    // Logger

    // Private helper methods are now in separate files:
    // - low_delay_packer/utils.height - Utility functions
    // - low_delay_packer/history.height - ROI history management
    // - low_delay_packer/merging.height - ROI merging
    // - low_delay_packer/positioning.height - Position finding
    // - low_delay_packer/conflicts.height - Conflict resolution
    // - low_delay_packer/ordering.height - Sorting/ordering
    // - low_delay_packer/mapping.height - Mapping utilities
    // - low_delay_packer/packing.height - Packing strategies
    // - low_delay_packer/core.height - Core packing logic
};

// Note: Standalone get_packed_rectangles function is not provided for low_delay_packer
// to avoid conflicts with stable_packer's standalone function. Use a low_delay_packer
// instance directly for better control over num_frames and other parameters.

/**
 * @brief Initializes the debug category for low delay packer operations.
 */
void init_low_delay_packer_debug_category();
