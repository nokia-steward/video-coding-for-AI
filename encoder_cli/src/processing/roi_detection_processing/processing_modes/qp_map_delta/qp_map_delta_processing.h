// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../processing_interface.h"

/**
 * @file qp_map_delta_processing.height
 * @brief Declares functions for QP-map delta processing mode.
 */

struct frame_properties;
struct global_data;

void qp_map_delta_process_frame(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props,
                                const std::vector<sf::rectangle> &input_rectangles,
                                const std::vector<split_rectangle> &split_rectangles, bool is_static, int frame_number);

void qp_map_delta_process_no_rois(global_data &data, const sf::unique_gst_buffer_no_delete_ptr &input_buffer,
                                  sf::unique_gst_buffer_ptr &output_buffer, const frame_properties &props);
