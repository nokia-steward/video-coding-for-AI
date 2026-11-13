// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <gst/gst.h>
#include <optional>

#include "../../../../libvcai/include/gst_raii.h"
#include "frame_merger.h" // For frame_path_type

/**
 * @file frame_path_meta.height
 * @brief Declares project metadata for tracking frame paths in dual-path bypass mode.
 */

/**
 * @brief Custom GstMeta to indicate which pipeline path a frame took.
 */
struct frame_path_meta {
    GstMeta meta;
    frame_path_type path;
    uint64_t sequence_id;
};

/**
 * @brief Strongly-typed frame metadata contract payload.
 */
struct frame_contract_info {
    frame_path_type path{frame_path_type::inference};
    uint64_t sequence_id{0};
};

/**
 * @brief Gets the GType for frame_path_meta.
 */
[[nodiscard]] GType frame_path_meta_api_get_type();

/**
 * @brief Gets the GstMetaInfo for frame_path_meta.
 */
[[nodiscard]] const GstMetaInfo *frame_path_meta_get_info();

/**
 * @brief Attaches frame path metadata to a buffer.
 *
 * @param sample_ptr Reference to unique_ptr containing the sample.
 *                   May be modified if buffer needs to be made writable.
 * @param path The path type to attach.
 * @param sequence_id Strict per-frame sequence id.
 * @return True if metadata was attached successfully.
 */
[[nodiscard]] bool attach_frame_path_metadata(sf::unique_gst_sample_ptr &sample_ptr, frame_path_type path,
                                              uint64_t sequence_id);

/**
 * @brief Attaches frame contract metadata directly to a writable buffer.
 * @param buffer Target buffer.
 * @param path Frame path identity.
 * @param sequence_id Strict per-frame sequence id.
 * @return True if metadata attach succeeded.
 */
[[nodiscard]] bool attach_frame_contract_metadata_to_buffer(GstBuffer *buffer, frame_path_type path,
                                                            uint64_t sequence_id);

/**
 * @brief Gets strict frame contract metadata from buffer.
 *
 * @param buffer The buffer to check.
 * @return Contract info when present, std::nullopt otherwise.
 */
[[nodiscard]] std::optional<frame_contract_info> get_frame_contract_from_buffer(const GstBuffer *buffer);

/**
 * @brief Gets only frame path from strict contract metadata.
 * @param buffer The buffer to check.
 * @return Path identity; defaults to inference when metadata is absent.
 */
[[nodiscard]] frame_path_type get_frame_path_from_buffer(const GstBuffer *buffer);

/**
 * @brief Checks if a buffer is from the bypass path.
 *
 * @param buffer The buffer to check.
 * @return True if the buffer came from the bypass path.
 */
[[nodiscard]] bool is_bypass_frame(const GstBuffer *buffer);
