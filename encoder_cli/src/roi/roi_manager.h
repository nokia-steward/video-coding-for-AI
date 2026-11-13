// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <vector>

#include "../../libvcai/include/rectangle.h"
#include "../../libvcai/include/yuv_color.h"
#include "../encoder_enums.h"
#include "roi_manager/stabilization.h"

/**
 * @brief Forward declaration for detection results produced by ROI extraction.
 */
struct detection_result;

/**
 * @file roi_manager.height
 * @brief Defines data structures and classes for ROI management and object tracking.
 */

/**
 * @enum object_state
 * @brief Represents the current state of a detected object.
 */
enum class object_state {
    detected, ///< Object detected with sufficient confidence
    detected_no_confidence, ///< Object detected but with low confidence
    missing, ///< Object not detected but still being tracked
    expired ///< Object has been missing for too long
};

/**
 * @struct object_data
 * @brief Contains data for a single detection/prediction of an object.
 */
struct object_data {
    sf::rectangle bounds; ///< Bounding box of the object
    float confidence{}; ///< Confidence level (-1.0 for predictions)
    uint32_t frame_number{}; ///< Frame number when this data was created
    sf::yuv_color color{}; ///< Color to use for drawing borders

    /**
     * @brief Default constructor.
     */
    object_data() = default;

    /**
     * @brief Constructor with parameters.
     * @param rect_bounds The bounding box of the object
     * @param conf The confidence level
     * @param frame_num The frame number
     * @param rect_color The border color
     */
    object_data(const sf::rectangle &rect_bounds, const float conf, const uint32_t frame_num,
                const sf::yuv_color rect_color) :
        bounds(rect_bounds),
        confidence(conf), frame_number(frame_num), color(rect_color) {}
};

/**
 * @struct detected_object
 * @brief Represents a tracked object with its history and current state.
 */
struct detected_object {
    uint32_t object_id{}; ///< Unique identifier for the object
    object_state state{}; ///< Current state of the object
    uint32_t missing_frames_count{}; ///< Number of consecutive frames the object has been missing
    std::vector<object_data> history; ///< History of detections and predictions
    stabilized_state stable_state; ///< Stabilization state for this object

    /**
     * @brief Default constructor.
     */
    detected_object() = default;

    /**
     * @brief Constructor with object ID.
     * @param id The unique identifier for the object
     */
    explicit detected_object(const uint32_t id) :
        object_id(id), state(object_state::detected), missing_frames_count(0), history(), stable_state() {}
};

/**
 * @struct roi_manager
 * @brief Manages ROI detection, tracking, and prediction for video frames.
 */
struct roi_manager {
    /**
     * @brief Constructor.
     * @param min_confidence_param Minimum confidence threshold for detections
     * @param max_missing_frames_param Maximum frames to track missing objects
     * @param enable_prediction_param Whether to enable ROI prediction when tracker stops providing data
     * @param accumulation_mode_param ROI accumulation mode (object_id or overlap)
     * @param overlap_threshold_param Overlap threshold for IOU-based matching (0.0 to 1.0)
     */
    roi_manager(float min_confidence_param, uint32_t max_missing_frames_param, bool enable_prediction_param,
                roi_accumulation_mode accumulation_mode_param, float overlap_threshold_param,
                stabilization_config stabilization_config_param);

    /**
     * @brief Processes a frame with new detections and updates object tracking.
     * @param detections Vector of new detections from the inference engine
     * @param frame_number Current frame number
     * @param frame_width Width of the frame for boundary checking
     * @param frame_height Height of the frame for boundary checking
     * @return Vector of all currently tracked objects
     */
    [[nodiscard]] std::vector<detected_object> process_frame(const std::vector<detection_result> &detections,
                                                             uint32_t frame_number, int frame_width, int frame_height);

    /**
     * @brief Gets all currently tracked objects.
     * @return Vector of all tracked objects
     */
    [[nodiscard]] const std::vector<detected_object> &get_tracked_objects() const { return tracked_objects; }

    /**
     * @brief Advances the frame counter without new detections.
     *
     * Called for bypass frames to maintain tracking state. Objects that
     * were being tracked will have their missing frame counter incremented,
     * and predictions will be generated for missing objects if enabled.
     *
     * @param frame_number The current frame number.
     * @param width Frame width for boundary checking.
     * @param height Frame height for boundary checking.
     */
    void advance_frame_without_detection(int frame_number, int width, int height);

private:
    /**
     * @brief Removes expired objects from tracking.
     */
    void remove_expired_objects();

    /**
     * @brief Finds an existing object by ID.
     * @param object_id The ID to search for
     * @return Iterator to the object, or end() if not found
     */
    [[nodiscard]] std::vector<detected_object>::iterator find_object_by_id(uint32_t object_id);
    [[nodiscard]] std::vector<detected_object>::const_iterator find_object_by_id(uint32_t object_id) const;

    // Private helper methods are now in separate files:
    // - roi_manager/tracking.height - Object tracking logic (update, create, handle missing)
    // - roi_manager/prediction.height - Position prediction logic

    float min_confidence; ///< Minimum confidence threshold
    uint32_t max_missing_frames; ///< Maximum frames to track missing objects
    bool enable_prediction; ///< Whether to enable ROI prediction when tracker stops providing data
    roi_accumulation_mode accumulation_mode; ///< ROI accumulation mode (object_id or overlap)
    float overlap_threshold; ///< Overlap threshold for IOU-based matching
    stabilization_config stabilization; ///< Stabilization configuration for detected ROIs
    std::vector<detected_object> tracked_objects; ///< Currently tracked objects
};

/**
 * @brief Initializes the debug category for ROI manager operations.
 */
void init_roi_manager_debug_category();
