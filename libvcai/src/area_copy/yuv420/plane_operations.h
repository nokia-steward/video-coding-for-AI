// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include "../../../include/area_copy.h"

namespace sf {
    /**
     * @brief Copies a rectangular region from a source plane to a destination plane.
     * @param src Pointer to the start of the source plane data.
     * @param dst Pointer to the start of the destination plane data.
     * @param src_stride The stride (width) of the source plane.
     * @param dst_stride The stride (width) of the destination plane.
     * @param src_plane_height The total height of the source plane.
     * @param dst_plane_height The total height of the destination plane.
     * @param src_x The starting X coordinate in the source.
     * @param src_y The starting Y coordinate in the source.
     * @param dst_x The starting X coordinate in the destination.
     * @param dst_y The starting Y coordinate in the destination.
     * @param width The width of the region to copy.
     * @param height The height of the region to copy.
     */
    void copy_plane_region(const uint8_t *const src, uint8_t *const dst, const int src_stride, const int dst_stride,
                           const int src_plane_height, const int dst_plane_height, const int src_x, const int src_y,
                           const int dst_x, const int dst_y, const int width, const int height);

    /**
     * @brief Calculates the required pointers for Y, U, and V planes from mapped buffers.
     * @param in_map_info Mapped input buffer.
     * @param out_map_info Mapped output buffer.
     * @param width The width of the Y plane.
     * @param height The height of the Y plane.
     * @return A struct containing all six plane pointers.
     */
    yuv_420_plane_pointers get_yuv420_plane_pointers(const GstMapInfo &in_map_info, const GstMapInfo &out_map_info,
                                                     const int width, const int height);
} // namespace sf
