// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <filesystem>
#include <gst/gst.h>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

struct command_line_parameters;
struct encoder_config;
struct global_data;

/**
 * @file integration_test_helpers.height
 * @brief Helper utilities for end-to-end integration tests.
 */

/**
 * @brief Creates a test video file using GStreamer videotestsrc.
 * @param output_path Path where the video file will be created.
 * @param width Video width in pixels.
 * @param height Video height in pixels.
 * @param framerate Video framerate.
 * @param duration_seconds Duration of the video in seconds.
 * @param format Video format (e.g., "mp4", "h264", "h265").
 * @return true if video was created successfully, false otherwise.
 */
[[nodiscard]] bool create_test_video(const std::filesystem::path &output_path, int width, int height, int framerate,
                                     int duration_seconds, const std::string &format = "mp4");

/**
 * @brief Creates a minimal encoder config file for testing.
 * @param config_path Path where the config file will be created.
 * @param video_file_path Path to the input video file.
 * @param width Video width.
 * @param height Video height.
 * @param framerate Video framerate.
 * @param output_file Optional path for output file.
 * @return true if config was created successfully, false otherwise.
 */
[[nodiscard]] bool create_test_config(const std::filesystem::path &config_path,
                                      const std::filesystem::path &video_file_path, int width, int height,
                                      int framerate,
                                      const std::optional<std::filesystem::path> &output_file = std::nullopt);

/**
 * @brief Verifies that an output file exists and has non-zero size.
 * @param file_path Path to the file to verify.
 * @return true if file exists and has content, false otherwise.
 */
[[nodiscard]] bool verify_output_file(const std::filesystem::path &file_path);

/**
 * @brief Gets the test data directory path.
 * @return Path to the test data directory.
 */
[[nodiscard]] std::filesystem::path get_test_data_dir();

/**
 * @brief Gets the test output directory path.
 * @return Path to the test output directory.
 */
[[nodiscard]] std::filesystem::path get_test_output_dir();

/**
 * @brief Ensures test directories exist, creating them if necessary.
 */
void ensure_test_directories();

/**
 * @brief Cleans up test output files.
 * @param keep_dirs If true, keeps directories but removes files.
 */
void cleanup_test_output(bool keep_dirs = true);

/**
 * @brief Creates a static ROI JSON file for testing.
 * @param output_path Path where the JSON file will be created.
 * @param rois Vector of ROI rectangles (x, y, width, height).
 * @return true if JSON file was created successfully, false otherwise.
 */
[[nodiscard]] bool create_static_roi_json(const std::filesystem::path &output_path,
                                          const std::vector<std::tuple<int, int, int, int>> &rois);

/**
 * @brief Creates an ROI data file in the format expected by the parser.
 * Format: [[[x1,y1,x2,y2], ...], [[x1,y1,x2,y2], ...], ...]
 * Filename format: SequenceName_widthxheight_framerate.txt
 * @param output_path Path where the ROI data file will be created.
 * @param sequence_name Sequence name (e.g., "TestSequence").
 * @param width Video width.
 * @param height Video height.
 * @param framerate Video framerate.
 * @param frame_rois Vector of frame ROIs, where each frame is a vector of (x1, y1, x2, y2) tuples.
 * @return true if ROI data file was created successfully, false otherwise.
 */
[[nodiscard]] bool create_roi_data_file(const std::filesystem::path &output_path, const std::string &sequence_name,
                                        int width, int height, int framerate,
                                        const std::vector<std::vector<std::tuple<int, int, int, int>>> &frame_rois);

/**
 * @brief Writes text content to a file and verifies it is non-empty.
 * @param output_path File path to write.
 * @param file_contents Text contents to write.
 * @return true if file was written and is non-empty, false otherwise.
 */
[[nodiscard]] bool write_text_file(const std::filesystem::path &output_path, const std::string &file_contents);

/**
 * @brief Verifies a generated config file has expected minimum validity.
 * @param config_path Config file path.
 * @param required_token Optional token that must exist in file content.
 * @param error_message Populated when validation fails.
 * @return true if config exists, is non-empty, and contains required token (if provided).
 */
[[nodiscard]] bool validate_config_file(const std::filesystem::path &config_path, const std::string &required_token,
                                        std::string &error_message);

/**
 * @brief Parses command line parameters for a config file.
 * @param config_path Path to configuration file.
 * @param params Output parsed parameters.
 * @param error_message Populated when parsing fails.
 * @return true if parsing succeeded, false otherwise.
 */
[[nodiscard]] bool parse_test_command_line(const std::filesystem::path &config_path, command_line_parameters &params,
                                           std::string &error_message);

/**
 * @brief Loads encoder configuration from a config file path.
 * @param config_path Path to configuration file.
 * @param config Output loaded encoder configuration.
 * @param error_message Populated when loading fails.
 * @return true if loading succeeded, false otherwise.
 */
[[nodiscard]] bool load_encoder_config_from_file(const std::filesystem::path &config_path, encoder_config &config,
                                                 std::string &error_message);

/**
 * @brief Result status for creating the full input/encoding/output pipelines.
 */
enum class pipeline_setup_status {
    success,
    encoding_unavailable,
    failure,
};

/**
 * @brief Result container for end-to-end pipeline creation.
 */
struct pipeline_setup_result {
    pipeline_setup_status status = pipeline_setup_status::failure;
    std::string error_message;
};

/**
 * @brief Creates input, encoding, and output pipelines in order.
 * @param data Global application state used by pipelines.
 * @return Structured pipeline setup result.
 */
[[nodiscard]] pipeline_setup_result create_full_test_pipelines(global_data &data);

/**
 * @brief Performs common integration-test cleanup for global_data.
 * @param data Shared test global_data pointer to reset.
 * @param reset_udp_state Whether to reset UDP connection tracking state.
 */
void cleanup_test_global_data(std::unique_ptr<global_data> &data, bool reset_udp_state = false);
