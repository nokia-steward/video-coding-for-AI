// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <vector>
#include "roi_detection_processing.h"

/**
 * @brief Forward declaration for shared application state.
 */
struct global_data;

/**
 * @file roi_detection_extraction.height
 * @brief Functions for extracting detection data from DeepStream metadata.
 */

/**
 * @brief Extracts Regions of Interest (ROIs) from DeepStream metadata in a GstBuffer.
 *
 * This function iterates through the object metadata attached by the `nvinfer` element,
 * filters for configured class IDs, and extracts basic detection information.
 *
 * @param buffer The GstBuffer containing the frame and its metadata.
 * @param data The global data store, containing the list of class IDs to filter for.
 * @return A vector of detection results from the inference engine.
 */
[[nodiscard]] std::vector<detection_result> get_detections_from_metadata(GstBuffer *buffer, global_data &data);
