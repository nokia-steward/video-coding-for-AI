// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "ctu_alignment.h"
#include "gst_raii.h"
#include "low_delay_packer/mapping.h"
#include "random_access_packer/history.h"
#include "rectangle.h"
#include "thread_safe_queue.h"

#include <gst/gst.h>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "../libvcai/include/logging.h"

/**
 * @file random_access_packer.height
 * @brief Declares the random access rectangle packer with intra-period buffering and CTU alignment.
 */

/**
 * @brief A random access rectangle packer with CTU-aligned packing and inter-period position memory.
 *
 * This packer buffers frames and tracked objects for a whole intra_period,
 * then processes them together using CTU-aligned two-phase packing:
 * - Buffering begins from the first successfully decoded and tracked frame
 * - After the intra_period completes, the buffered period is processed
 * - ROIs are accumulated for each object across the entire period, then merged if overlapping (30% threshold)
 * - Merged ROIs are placed on a CTU grid (64×64 by default) to avoid ROI boundaries mid-CTU
 * - First-occurrence ROIs are centered within their CTU rectangle (constraint 2)
 * - Recurring ROIs preserve pixel correspondence across periods (constraint 3)
 * - Shrinking ROIs free CTU cells without moving other ROIs (constraint 4)
 * - Growing ROIs that can't fit in place are relocated to unoccupied space (constraint 5)
 * - Position memory across periods enables inter-period stability for motion compensation
 */
struct random_access_packer {
    /**
     * @brief Structure to hold a buffered frame with its ROI data.
     */
    struct buffered_frame {
        int frame_number;
        sf::unique_gst_sample_ptr sample; // Own the sample (with caps) - keeps buffer valid
        std::vector<std::pair<sf::rectangle, std::string>> rectangles_with_ids;

        // Take ownership of sample - this keeps the buffer valid
        buffered_frame(const int fn, sf::unique_gst_sample_ptr sample_ptr) :
            frame_number(fn), sample(std::move(sample_ptr)), rectangles_with_ids() {
            if (!sample) {
                // Use sf:: logging directly since this is in a header file
                sf::error(nullptr, "Cannot store null sample for frame %d", fn);
            }
        }
    };

    /**
     * @brief Structure representing a complete intraperiod ready for processing.
     * Contains all frames and one set of processed ROIs (same for all frames in the period).
     */
    struct complete_period {
        int period_start_frame; // First frame number in this period
        int period_number; // Period number (0 = first period, 1+ = subsequent periods)
        std::vector<buffered_frame> frames; // All frames in this period
        std::vector<sf::rectangle> processed_rois; // One set of processed ROIs (same for all frames)
        std::vector<std::string>
                object_ids; // Object IDs in same order as sorted_source_rectangles (original accumulated ROIs)
        std::vector<sf::rectangle> original_rois; // Original accumulated ROIs (before merging and packing)
        std::vector<size_t> original_to_packed_mapping; // Mapping from object_ids index to processed_rois index

        complete_period() :
            period_start_frame(-1), period_number(-1), frames(), processed_rois(), object_ids(), original_rois(),
            original_to_packed_mapping() {}
    };

    /**
     * @brief Constructor for the random access packer.
     * @param max_width_param Maximum width of the packing area
     * @param max_height_param Maximum height of the packing area
     * @param ctu_size_param CTU cell size in pixels for CTU-aligned packing (default: 64)
     * @param intra_period_param Number of frames in an intra period (default: 30)
     * @param max_buffered_periods Maximum number of complete periods to buffer (default: 3)
     */
    random_access_packer(int max_width_param, int max_height_param, int ctu_size_param, int intra_period_param,
                         int max_buffered_periods);

    // Disable copy constructor and assignment operator due to reference member
    random_access_packer(const random_access_packer &) = delete;
    random_access_packer &operator=(const random_access_packer &) = delete;

    /**
     * @brief Add a frame and its ROI data to the packer.
     * This is called by the input thread for each frame. The packer will buffer frames
     * and process them when a complete intraperiod is available.
     * @param frame_number The frame number
     * @param sample The GstSample for this frame (packer takes ownership, will be buffered)
     * @param rectangles_with_ids Source ROI rectangles with their object IDs for this frame.
     * @param packing_rectangles_with_ids Optional ROI rectangles used for packing placement.
     *        When not provided, rectangles_with_ids are used for packing.
     */
    void add_frame(int frame_number, sf::unique_gst_sample_ptr sample,
                   const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
                   const std::optional<std::vector<std::pair<sf::rectangle, std::string>>>
                           &packing_rectangles_with_ids = std::nullopt);

    /**
     * @brief Get the next complete period from the packer.
     * This is a blocking call that waits for a complete intraperiod to be available.
     * Called by the Frame Processing thread.
     * @return Optional complete_period, nullopt if packer is closed
     */
    [[nodiscard]] std::optional<complete_period> get_complete_period();

    /**
     * @brief Close the packer, signaling that no more frames will be added.
     * This will cause get_complete_period() to return nullopt once all buffered periods are consumed.
     */
    void close();

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
    // object_roi_history is defined in random_access_packer/history.height

    int max_width;
    int max_height;
    int ctu_size;
    int intra_period;
    int max_buffered_periods;
    int pack_call_counter;

    // Thread-safe queue for complete periods
    sf::thread_safe_queue<complete_period> complete_periods_queue;

    // Buffering state (protected by mutex for thread safety)
    mutable std::mutex state_mutex;
    std::optional<complete_period> current_period; // Current period being built (nullopt if not started)
    std::optional<complete_period>
            completed_first_period; // First period after completion (kept until second period starts)
    std::unordered_map<std::string, object_roi_history>
            first_period_object_histories; // Object histories for first period
    int next_period_number; // Next period number to assign (0 = first period, 1+ = subsequent periods)
    bool closed; // Whether packer is closed (no more frames will be added)

    // ROI accumulation across the current period
    std::unordered_map<std::string, object_roi_history> object_histories;

    // Inter-period position memory: tracks CTU-aligned placements across periods
    // for pixel correspondence preservation (constraints 3, 4, 5).
    std::unordered_map<std::string, position_slot> position_memory;

    // Statistics
    size_t total_stable_count;
    size_t total_repacked_count;

    // Private helper methods are now in separate files:
    // - random_access_packer/utils.height - Utility functions
    // - random_access_packer/history.height - ROI history management
    // - random_access_packer/merging.height - ROI merging
    // - random_access_packer/positioning.height - Position finding
    // - random_access_packer/packing.height - Packing strategies
    // - random_access_packer/period.height - Period processing
};

/**
 * @brief Initializes the debug category for random access packer operations.
 */
void init_random_access_packer_debug_category();
