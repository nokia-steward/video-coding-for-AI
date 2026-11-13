// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "frame_buffer_utils.h"

#include <cstring>

#include "logging.h"

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

namespace {
    /**
     * @brief Checks if buffer has an H.265 start code.
     * @param data Buffer data.
     * @param size Buffer size.
     * @return true if start code is present, false otherwise.
     */
    [[nodiscard]] bool has_h265_start_code(const guint8 *data, gsize size) {
        if (size >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
            sf::debug(pipeline_category, "Buffer already has 3-byte start code (0x00 0x00 0x01)");
            return true;
        }
        if (size >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
            sf::debug(pipeline_category, "Buffer already has 4-byte start code (0x00 0x00 0x00 0x01)");
            return true;
        }
        sf::debug(pipeline_category, "Buffer missing start code. First bytes: 0x%02x 0x%02x 0x%02x 0x%02x",
                  size > 0 ? data[0] : 0, size > 1 ? data[1] : 0, size > 2 ? data[2] : 0, size > 3 ? data[3] : 0);
        return false;
    }

    /**
     * @brief Creates a new buffer with start code prepended.
     * @param original_data Original buffer data.
     * @param original_size Original buffer size.
     * @return New buffer with start code, or nullptr on failure.
     */
    [[nodiscard]] sf::unique_gst_buffer_ptr create_buffer_with_start_code(const guint8 *original_data,
                                                                          gsize original_size) {
        constexpr gsize start_code_size = 4; // 0x00 0x00 0x00 0x01
        const gsize new_size = original_size + start_code_size;

        auto new_buffer = sf::unique_gst_buffer_ptr(gst_buffer_new_allocate(nullptr, new_size, nullptr));
        if (!new_buffer) {
            return nullptr;
        }

        GstMapInfo map_info;
        if (!gst_buffer_map(new_buffer.get(), &map_info, GST_MAP_WRITE)) {
            return nullptr;
        }

        // Write start code: 0x00 0x00 0x00 0x01
        map_info.data[0] = 0x00;
        map_info.data[1] = 0x00;
        map_info.data[2] = 0x00;
        map_info.data[3] = 0x01;

        // Copy original buffer data
        std::memcpy(map_info.data + start_code_size, original_data, original_size);

        sf::debug(pipeline_category, "Added start code to buffer. Original size: %zu, New size: %zu", original_size,
                  new_size);
        gst_buffer_unmap(new_buffer.get(), &map_info);

        return new_buffer;
    }

    /**
     * @brief Copies timing information from source to destination buffer.
     * @param dest Destination buffer.
     * @param src Source buffer.
     */
    void copy_buffer_timing(GstBuffer *dest, GstBuffer *src) {
        GST_BUFFER_PTS(dest) = GST_BUFFER_PTS(src);
        GST_BUFFER_DTS(dest) = GST_BUFFER_DTS(src);
        GST_BUFFER_DURATION(dest) = GST_BUFFER_DURATION(src);
        GST_BUFFER_OFFSET(dest) = GST_BUFFER_OFFSET(src);
        GST_BUFFER_OFFSET_END(dest) = GST_BUFFER_OFFSET_END(src);
        GST_BUFFER_FLAGS(dest) = GST_BUFFER_FLAGS(src);
    }
} // namespace

/**
 * @brief Adds H.265 start code (0x00 0x00 0x00 0x01) to the beginning of a buffer if missing.
 * @param buffer The buffer to add start code to.
 * @return A new buffer with start code prepended, or the original buffer if it already has one.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr add_h265_start_code(const sf::unique_gst_buffer_ptr &buffer) {
    if (!buffer) {
        return nullptr;
    }

    GstMapInfo original_map_info;
    if (!gst_buffer_map(buffer.get(), &original_map_info, GST_MAP_READ)) {
        return nullptr;
    }

    const guint8 *data = original_map_info.data;
    const gsize original_size = original_map_info.size;

    // Check if buffer already has a start code
    if (has_h265_start_code(data, original_size)) {
        gst_buffer_unmap(buffer.get(), &original_map_info);
        // Ref the buffer to create a new unique_ptr since we can't copy
        return sf::unique_gst_buffer_ptr(gst_buffer_ref(buffer.get()));
    }

    // Create new buffer with start code
    auto new_buffer = create_buffer_with_start_code(data, original_size);
    gst_buffer_unmap(buffer.get(), &original_map_info);

    if (!new_buffer) {
        return nullptr;
    }

    // Copy timing information from original buffer
    copy_buffer_timing(new_buffer.get(), buffer.get());

    return new_buffer;
}
