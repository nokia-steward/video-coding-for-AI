// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "gst_raii.h"

/**
 * @brief Adds H.265 start code (0x00 0x00 0x00 0x01) to the beginning of a buffer.
 * @param buffer The buffer to add start code to.
 * @return A new buffer with start code prepended, or nullptr on failure.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr add_h265_start_code(const sf::unique_gst_buffer_ptr &buffer);
