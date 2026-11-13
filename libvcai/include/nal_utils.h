// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sf {
    /**
     * @file nal_utils.h
     * @brief Provides utility functions for creating and injecting H.265 NAL units.
     *
     * This module contains the core logic for wrapping serialized SEI message payloads
     * into complete NAL units and for injecting those NAL units into a raw H.265
     * byte stream at the correct, standards-compliant position.
     *
     * This module follows functional programming principles:
     * - Pure data transformations: functions operate on input data to produce output
     * - Immutable parameters: all input parameters are const references
     * - Clear separation of concerns: each function has a single responsibility
     * - Error handling through return values: functions return success/failure status
     * - No global state: all functions are stateless and predictable
     */

    // Forward declaration
    struct packed_regions_info;

    /**
     * @brief Creates a complete, standards-compliant prefix SEI NAL unit.
     *
     * This function wraps a raw SEI payload with payloadType/payloadSize fields,
     * rbsp_stop_one_bit, and the HEVC PREFIX_SEI_NUT header.
     *
     * @param payload_type SEI payload type value.
     * @param payload_bytes Serialized SEI payload bytes.
     * @return A vector of bytes representing the complete SEI NAL unit (without a start code).
     */
    [[nodiscard]] std::vector<uint8_t> create_sei_nal_unit(uint32_t payload_type,
                                                           const std::vector<uint8_t> &payload_bytes);

    /**
     * @brief Creates a complete, standards-compliant SEI NAL unit.
     *
     * This function takes the high-level packed_regions_info struct, serializes
     * its payload, and wraps it with the necessary SEI headers (payload_type,
     * payload_size) and a NAL unit header for a Prefix SEI NAL unit.
     *
     * @param pri_data The populated packed_regions_info struct to be encoded.
     * @return A vector of bytes representing the complete SEI NAL unit (without a
     * start code), ready for injection.
     */
    [[nodiscard]] std::vector<uint8_t> create_sei_nal_unit(const packed_regions_info &pri_data);

    /**
     * @brief Checks whether a frame contains a layer-0 HEVC IRAP VCL NAL unit.
     *
     * HEVC IRAP NAL unit types are in the range [16, 21], covering BLA, IDR,
     * and CRA families.
     *
     * @param frame_data A pointer to the raw access-unit bytes.
     * @param frame_length The total length of the frame_data buffer.
     * @return true when at least one layer-0 IRAP VCL NAL is present, false otherwise.
     */
    [[nodiscard]] bool contains_hevc_irap_vcl_nal(const uint8_t *frame_data, size_t frame_length);

    /**
     * @brief Injects a pre-formed SEI NAL unit into an H.265 access unit.
     *
     * This function scans the provided frame data to find the correct injection
     * point, which is immediately before the first VCL NAL unit of the base layer
     * (nuh_layer_id == 0). It then constructs a new buffer with the SEI NAL unit
     * inserted at this location.
     *
     * @param frame_data A pointer to the raw byte buffer of the access unit.
     * @param frame_length The total length of the frame_data buffer.
     * @param sei_nal_unit The complete SEI NAL unit (without a start code) to inject.
     * @return A new vector containing the correctly structured access unit with the
     * injected SEI message.
     *
     * Note: frame_data parameter is const as the function only reads from it.
     * The function creates a new buffer and returns it, not modifying the input.
     */
    [[nodiscard]] std::vector<uint8_t> inject_sei_message(const uint8_t *frame_data, size_t frame_length,
                                                          const std::vector<uint8_t> &sei_nal_unit);
} // namespace sf

/**
 * @brief Initialize the nal_utils debug category.
 * This function should be called after gst_init() to ensure the category is properly initialized.
 */
void init_nal_utils_debug_category();
