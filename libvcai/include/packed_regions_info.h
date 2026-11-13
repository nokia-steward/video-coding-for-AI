// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <cstdint>
#include <iomanip>
#include <optional>
#include <string>
#include <vector>

namespace sf {
    /**
     * @file packed_regions_info.h
     * @brief Public API for serializing and deserializing Packed Regions Info SEI messages.
     *
     * This file contains the data structures that model the SEI message and the
     * public function declarations for writing to and reading from a byte buffer.
     */

    /**
     * @struct resampling_ratio
     * @brief Represents the resampling ratio parameters.
     */
    struct resampling_ratio {
        unsigned int width_num_minus1{};
        unsigned int width_denom_minus1{};
        bool fixed_aspect_ratio_flag{};
        std::optional<unsigned int> height_num_minus1{};
        std::optional<unsigned int> height_denom_minus1{};
    };

    /**
     * @struct region
     * @brief Represents the information for a single packed region.
     */
    struct region {
        std::optional<unsigned int> id{};
        std::optional<unsigned int> layer_id{};
        std::optional<bool> is_a_layer_flag{};
        std::optional<unsigned int> top_left_in_units_x{};
        std::optional<unsigned int> top_left_in_units_y{};
        std::optional<unsigned int> width_in_units_minus1{};
        std::optional<unsigned int> height_in_units_minus1{};
        std::optional<unsigned int> resampling_ratio_idx{};
        std::optional<unsigned int> target_region_top_left_in_units_x{};
        std::optional<unsigned int> target_region_top_left_in_units_y{};
    };

    /**
     * @struct packed_regions_info
     * @brief Represents the entire Packed Regions Information SEI message.
     */
    struct packed_regions_info {
        bool cancel_flag{false};
        std::optional<bool> persistence_flag{};
        std::optional<bool> multilayer_flag{};
        std::optional<bool> use_max_dimensions_flag{};
        std::optional<uint8_t> log2_unit_size{};
        std::optional<uint8_t> region_size_len_minus1{};
        std::optional<bool> region_id_present_flag{};
        std::optional<bool> target_pic_params_present_flag{};
        std::optional<uint16_t> target_pic_width_minus1{};
        std::optional<uint16_t> target_pic_height_minus1{};
        /**
         * @brief Inferred default resampling ratio (spec index 0).
         *
         * Per H.274 8.39 semantics, index 0 is inferred as 1:1 and is not
         * signaled in the PRI bitstream.
         */
        resampling_ratio inferred_resampling_ratio{0U, 0U, true, std::nullopt, std::nullopt};

        /**
         * @brief Signaled resampling ratios (spec indices 1..N).
         *
         * The i-th element corresponds to spec index (i + 1).
         */
        std::vector<resampling_ratio> resampling_ratios{};
        std::vector<region> regions{};

        /**
         * @brief Default constructor.
         */
        packed_regions_info() = default;

        /**
         * @brief Move constructor.
         * @param other Source instance to move from.
         */
        packed_regions_info(packed_regions_info &&other) noexcept = default;

        /**
         * @brief Move assignment operator.
         * @param other Source instance to move from.
         * @return Reference to this instance.
         */
        packed_regions_info &operator=(packed_regions_info &&other) noexcept = default;

        /**
         * @brief Copy constructor.
         * @param other Source instance to copy from.
         */
        packed_regions_info(const packed_regions_info &other) = default;

        /**
         * @brief Copy assignment operator.
         * @param other Source instance to copy from.
         * @return Reference to this instance.
         */
        packed_regions_info &operator=(const packed_regions_info &other) = default;

        /**
         * @brief Creates a sentinel packed_regions_info object indicating no ROIs for a frame.
         * @return A packed_regions_info with cancel_flag set to true.
         */
        static packed_regions_info create_sentinel() {
            packed_regions_info sentinel{};
            sentinel.cancel_flag = true;
            return sentinel;
        }
    };

    /**
     * @brief Creates a string representation of a resampling_ratio object.
     * @param r The resampling_ratio object.
     * @return A string representation of the object.
     */
    [[nodiscard]] std::string to_string(const resampling_ratio &r);

    /**
     * @brief Creates a string representation of a region object.
     * @param r The region object.
     * @return A string representation of the object.
     */
    [[nodiscard]] std::string to_string(const region &r);

    /**
     * @brief Creates a string representation of a packed_regions_info object.
     * @param pri The packed_regions_info object.
     * @return A string representation of the object.
     */
    [[nodiscard]] std::string to_string(const packed_regions_info &pri);

    /**
     * @brief Serializes a packed_regions_info struct into a byte buffer.
     * @param data The struct to serialize.
     * @return A vector of bytes representing the serialized data.
     */
    [[nodiscard]] std::vector<uint8_t> write_packed_regions_info(const packed_regions_info &data);

    /**
     * @brief Deserializes a byte buffer into a packed_regions_info struct.
     * @param buffer The byte buffer to read from.
     * @param parse_ok_out When non-null, set to false if the reader underran while parsing (invalid/truncated RBSP).
     * @return The deserialized packed_regions_info struct (best-effort when @a parse_ok_out is false).
     */
    [[nodiscard]] packed_regions_info read_packed_regions_info(const std::vector<uint8_t> &buffer,
                                                               bool *parse_ok_out = nullptr);
} // namespace sf
