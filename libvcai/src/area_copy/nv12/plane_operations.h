// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>
#include "../../../include/area_copy.h"

namespace sf {
    /**
     * @brief Copies an interleaved UV region from NV12 format.
     * @param src_uv Pointer to the start of the source UV plane (interleaved U,V,U,V...).
     * @param dst_uv Pointer to the start of the destination UV plane (interleaved U,V,U,V...).
     * @param src_stride_bytes The stride of the source UV plane in bytes.
     * @param dst_stride_bytes The stride of the destination UV plane in bytes.
     * @param src_plane_height The total height of the source UV plane.
     * @param dst_plane_height The total height of the destination UV plane.
     * @param src_x_bytes The starting X coordinate in the source in BYTES (not pixels).
     * @param src_y The starting Y coordinate in the source (in UV plane rows).
     * @param dst_x_bytes The starting X coordinate in the destination in BYTES (not pixels).
     * @param dst_y The starting Y coordinate in the destination (in UV plane rows).
     * @param width_bytes The width of the region to copy in BYTES (not pixels).
     * @param height The height of the region to copy (in UV plane rows).
     */
    void copy_interleaved_uv_region(const uint8_t *const src_uv, uint8_t *const dst_uv, const int src_stride_bytes,
                                    const int dst_stride_bytes, const int src_plane_height, const int dst_plane_height,
                                    const int src_x_bytes, const int src_y, const int dst_x_bytes, const int dst_y,
                                    const int width_bytes, const int height);
} // namespace sf
