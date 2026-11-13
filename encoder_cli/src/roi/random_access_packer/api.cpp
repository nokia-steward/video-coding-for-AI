// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../../libvcai/include/logging.h"
#include "../random_access_packer.h"
#include "history.h"
#include "period.h"
#include "utils.h"

#include <gst/gst.h>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @file api.cpp
 * @brief Implementation of random access packer API functions.
 */

/// GStreamer debug category for random access packer operations.
GST_DEBUG_CATEGORY_STATIC(random_access_packer_debug);

random_access_packer::random_access_packer(const int max_width_param, const int max_height_param,
                                           const int ctu_size_param, const int intra_period_param,
                                           const int max_buffered_periods_param) :
    max_width(max_width_param),
    max_height(max_height_param), ctu_size(ctu_size_param), intra_period(intra_period_param),
    max_buffered_periods(max_buffered_periods_param), pack_call_counter(0), complete_periods_queue(), state_mutex(),
    current_period(std::nullopt), completed_first_period(std::nullopt), first_period_object_histories(),
    next_period_number(0), closed(false), object_histories(), position_memory(), total_stable_count(0),
    total_repacked_count(0) {

    sf::info(random_access_packer_debug,
             "Random Access Packer initialized: %dx%d area, ctu_size=%d, intra_period=%d, max_buffered_periods=%d",
             max_width, max_height, ctu_size, intra_period, max_buffered_periods);
}

void random_access_packer::add_frame(
        int frame_number, sf::unique_gst_sample_ptr sample,
        const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
        const std::optional<std::vector<std::pair<sf::rectangle, std::string>>> &packing_rectangles_with_ids) {
    if (closed) {
        sf::warning(random_access_packer_debug, "Frame %d: Packer is closed, ignoring frame", frame_number);
        return;
    }

    std::lock_guard lock(state_mutex);

    // Start a new period if we don't have one
    if (!current_period.has_value()) {
        // If we're starting period 1, first process the completed first period
        if (next_period_number == 1 && completed_first_period.has_value()) {
            // Process the first period using its saved object_histories
            std::swap(object_histories, first_period_object_histories);

            const auto result = process_intra_period(object_histories, max_width, max_height, ctu_size,
                                                     pack_call_counter, position_memory);

            std::swap(object_histories, first_period_object_histories);

            if (result) {
                // Update the first period with processed ROIs
                complete_period period_to_push = std::move(*completed_first_period);

                // Extract object_ids in the SAME ORDER as accumulated_rois
                period_to_push.object_ids.clear();
                period_to_push.object_ids.reserve(first_period_object_histories.size());
                for (const auto &[object_id, history]: first_period_object_histories) {
                    if (!history.rois.empty()) {
                        period_to_push.object_ids.push_back(object_id);
                    }
                }
                std::ranges::sort(period_to_push.object_ids);

                // Extract processed ROIs from pack result
                period_to_push.processed_rois = result->packed_rectangles;
                period_to_push.original_rois = result->sorted_source_rectangles;
                period_to_push.original_to_packed_mapping = result->original_to_packed_mapping;

                // Check if queue is full, drop oldest if necessary
                int periods_to_drop = 0;
                while (periods_to_drop < max_buffered_periods - 1) {
                    auto old_period = complete_periods_queue.try_pop();
                    if (!old_period) {
                        break;
                    }
                    periods_to_drop++;
                    sf::warning(random_access_packer_debug,
                                "Dropping oldest period %d (max_buffered_periods=%d reached)",
                                old_period->period_number, max_buffered_periods);
                }

                // Push the completed first period
                complete_periods_queue.push(std::move(period_to_push));
            } else {
                sf::error(random_access_packer_debug, "Failed to process first intra period");
            }

            // Clear the saved first period data
            completed_first_period = std::nullopt;
            first_period_object_histories.clear();
        }

        complete_period new_period;
        new_period.period_start_frame = frame_number;
        new_period.period_number = next_period_number;
        next_period_number++;

        current_period = std::move(new_period);
    }

    // Add frame to current period (take ownership of sample - keeps buffer valid)
    current_period->frames.emplace_back(frame_number, std::move(sample));
    auto &frame = current_period->frames.back();
    frame.rectangles_with_ids = rectangles_with_ids;

    const auto &rectangles_for_packing = packing_rectangles_with_ids.value_or(rectangles_with_ids);

    // Accumulate ROIs for each object in this frame
    for (const auto &[rect, id]: rectangles_for_packing) {
        auto &history = object_histories[id];
        if (history.object_id.empty()) {
            history.object_id = id;
        }
        history.rois.push_back(rect);
    }

    // Check if current period is now complete (after adding this frame)
    const int frames_in_period = static_cast<int>(current_period->frames.size());
    if (frames_in_period >= intra_period) {
        // Process the completed period (only if it's not the first period)
        if (current_period->period_number > 0) {
            // Save current period's object_histories before processing
            std::unordered_map<std::string, object_roi_history> previous_period_object_histories =
                    std::move(object_histories);

            // Process the completed period using its saved object_histories
            std::swap(object_histories, previous_period_object_histories);
            const auto result = process_intra_period(object_histories, max_width, max_height, ctu_size,
                                                     pack_call_counter, position_memory);
            std::swap(object_histories, previous_period_object_histories);

            if (result) {
                // Update the period with processed ROIs
                complete_period period_to_push = std::move(*current_period);

                // Extract object_ids in the SAME ORDER as accumulated_rois
                period_to_push.object_ids.clear();
                period_to_push.object_ids.reserve(previous_period_object_histories.size());
                for (const auto &[object_id, history]: previous_period_object_histories) {
                    if (!history.rois.empty()) {
                        period_to_push.object_ids.push_back(object_id);
                    }
                }
                std::ranges::sort(period_to_push.object_ids);

                // Extract processed ROIs from pack result
                period_to_push.processed_rois = result->packed_rectangles;
                period_to_push.original_rois = result->sorted_source_rectangles;
                period_to_push.original_to_packed_mapping = result->original_to_packed_mapping;

                // Check if queue is full, drop oldest if necessary
                int periods_to_drop = 0;
                while (periods_to_drop < max_buffered_periods - 1) {
                    auto old_period = complete_periods_queue.try_pop();
                    if (!old_period) {
                        break;
                    }
                    periods_to_drop++;
                    sf::warning(random_access_packer_debug,
                                "Dropping oldest period %d (max_buffered_periods=%d reached)",
                                old_period->period_number, max_buffered_periods);
                }

                // Push the completed period
                complete_periods_queue.push(std::move(period_to_push));
            } else {
                sf::error(random_access_packer_debug, "Failed to process intra period %d",
                          current_period->period_number);
            }
        } else {
            // First period: save it for processing when second period starts
            // Save the first period and its object histories
            completed_first_period = std::move(*current_period);
            first_period_object_histories = std::move(object_histories);
        }

        // Clear the current period to start a new one on next frame
        current_period = std::nullopt;
        object_histories.clear();
    }
}

/**
 * @brief Pops the next complete period from the packer.
 * @return Complete period if available, otherwise nullopt.
 */
std::optional<random_access_packer::complete_period> random_access_packer::get_complete_period() {
    return complete_periods_queue.pop();
}

void random_access_packer::close() {
    std::lock_guard lock(state_mutex);

    if (closed) {
        return;
    }

    closed = true;

    // Process final period if there's one in progress
    if (current_period.has_value() && !current_period->frames.empty() && current_period->period_number > 0) {
        const auto result = process_intra_period(object_histories, max_width, max_height, ctu_size, pack_call_counter,
                                                 position_memory);

        if (result) {
            complete_period period = std::move(*current_period);

            // Extract object_ids in the SAME ORDER as accumulated_rois
            period.object_ids.clear();
            period.object_ids.reserve(object_histories.size());
            for (const auto &[object_id, history]: object_histories) {
                if (!history.rois.empty()) {
                    period.object_ids.push_back(object_id);
                }
            }
            std::ranges::sort(period.object_ids);

            period.processed_rois = result->packed_rectangles;
            period.original_rois = result->sorted_source_rectangles;
            period.original_to_packed_mapping = result->original_to_packed_mapping;

            complete_periods_queue.push(std::move(period));
        }
    }

    // Close the queue to signal no more periods will be added
    complete_periods_queue.close();
}

void random_access_packer::clear_memory() {
    std::lock_guard lock(state_mutex);

    current_period = std::nullopt;
    completed_first_period = std::nullopt;
    object_histories.clear();
    first_period_object_histories.clear();
    position_memory.clear();
    next_period_number = 0;
    closed = false;
    pack_call_counter = 0;
    total_stable_count = 0;
    total_repacked_count = 0;

    // Drain the queue
    complete_periods_queue.close();
    complete_periods_queue.drain();
    // Recreate the queue (thread_safe_queue doesn't have reset, so we need to recreate)
    // Actually, we can't easily recreate it, so just drain it
    // The queue will be closed, so get_complete_period() will return nullopt
}

/**
 * @brief Returns a snapshot of packer statistics.
 */
random_access_packer::stats random_access_packer::get_stats() const {
    stats stats_snapshot{};
    stats_snapshot.total_rectangles_remembered = object_histories.size();
    stats_snapshot.total_packing_calls = static_cast<size_t>(pack_call_counter);
    stats_snapshot.average_stability_ratio =
            total_stable_count + total_repacked_count > 0
                    ? static_cast<double>(total_stable_count) /
                              static_cast<double>(total_stable_count + total_repacked_count)
                    : 0.0;
    return stats_snapshot;
}

int random_access_packer::get_weight(const sf::rectangle &rect) { return get_rectangle_weight(rect); }
