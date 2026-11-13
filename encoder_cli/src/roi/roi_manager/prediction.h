// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include "../roi_manager.h"

/**
 * @file prediction.height
 * @brief Declares functions for predicting ROI positions when detections are missing.
 */

/**
 * @brief Predicts the position and size of a missing object.
 * @param obj The object to predict position for
 * @param frame_number Current frame number
 * @param frame_width Width of the frame for boundary checking
 * @param frame_height Height of the frame for boundary checking
 */
void predict_object_position(detected_object &obj, uint32_t frame_number, int frame_width, int frame_height);
