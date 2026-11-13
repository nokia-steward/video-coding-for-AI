// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include <string>

/**
 * @file frame_saver.height
 * @brief Declares utility struct for saving YUV420 frames as PNG files.
 */

/**
 * @brief Utility struct for saving YUV420 frames as PNG files
 *
 * This utility converts YUV420 video frames to PNG format and saves them
 * to a debug_frames/ directory with proper frame number ordering.
 */
struct frame_saver {
    /**
     * @brief Constructor
     * @param output_dir_param Directory to save frames (will be created if it doesn't exist)
     */
    explicit frame_saver(std::string output_dir_param = "debug_frames");

    /**
     * @brief Destructor
     */
    ~frame_saver();

    /**
     * @brief Save a YUV420 frame as PNG
     * @param buffer GStreamer buffer containing the frame data
     * @param width Frame width in pixels
     * @param height Frame height in pixels
     * @param frame_number Frame number for filename
     * @return true if successful, false otherwise
     */
    [[nodiscard]] bool save_frame(GstBuffer *buffer, guint width, guint height, guint frame_number) const;

    /**
     * @brief Check if frame saving is enabled
     * @return true if enabled, false otherwise
     */
    [[nodiscard]] bool is_enabled() const { return enabled; }

private:
    std::string output_dir;
    bool enabled;

    /**
     * @brief Create output directory if it doesn't exist
     * @return true if successful, false otherwise
     */
    void create_output_dir() const;

    /**
     * @brief Generate filename for frame
     * @param frame_number Frame number
     * @return Filename string
     */
    [[nodiscard]] std::string generate_filename(guint frame_number) const;
};

/**
 * @brief Initialize the frame saver debug category.
 */
void init_frame_saver_debug_category();
