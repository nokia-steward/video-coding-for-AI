// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "../../encoder_cli/src/roi/roi_parser.h"

namespace {
    /**
     * @brief Creates a temporary file with the specified content.
     * @param path The path where the file will be created.
     * @param content The string content to write to the file.
     */
    void create_temp_file(const std::filesystem::path &path, const std::string &content) {
        std::ofstream file(path);
        file << content;
    }

    // A test fixture for tests that need to create temporary files and directories.
    // This ensures that the temporary directory is cleaned up after each test.
    struct roi_parser_file_test : ::testing::Test {
    protected:
        const std::filesystem::path test_dir = "temp_roi_parser_test_files";

        void SetUp() override { std::filesystem::create_directory(test_dir); }

        void TearDown() override { std::filesystem::remove_all(test_dir); }
    };

} // namespace

// Tests successful loading and parsing of a valid, real-world ROI file.
TEST(roi_parser_test, successful_load) {
    // Try multiple possible paths for the test file
    // Path relative to test binary directory (test_build/test_bin/)
    std::filesystem::path test_file = "../../test/BasketballDrill_832x480_50.txt";

    // If not found, try alternative path (project root)
    if (!std::filesystem::exists(test_file)) {
        test_file = "../../BasketballDrill_832x480_50.txt";
    }

    // If still not found, try test_data directory
    if (!std::filesystem::exists(test_file)) {
        test_file = "../../test/test_data/roi_data/BasketballDrill_832x480_50.txt";
    }

    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "FATAL: Test file not found. Tried: "
                     << std::filesystem::absolute("../../test/BasketballDrill_832x480_50.txt") << ", "
                     << std::filesystem::absolute("../../BasketballDrill_832x480_50.txt") << ", "
                     << std::filesystem::absolute("../../test/test_data/roi_data/BasketballDrill_832x480_50.txt");
    }

    const auto info_opt = load_roi_file(test_file, 1);
    ASSERT_TRUE(info_opt.has_value());
    const auto &info = *info_opt;

    // 1. Check filename parsing
    EXPECT_EQ(info.sequence_name, "BasketballDrill");
    EXPECT_EQ(info.width, 832);
    EXPECT_EQ(info.height, 480);
    EXPECT_EQ(info.frame_rate_nominal, 50);

    // 2. Check sfu_config lookup
    ASSERT_TRUE(info.frame_rate_details.has_value());
    EXPECT_EQ(info.frame_rate_details->frame_rate, 50);
    EXPECT_EQ(info.frame_rate_details->intra_period, 64);
    EXPECT_TRUE(info.qp_config_ld.has_value());
    EXPECT_TRUE(info.qp_config_ai.has_value());

    // 3. Check ROI content parsing
    ASSERT_FALSE(info.rois.empty());
    ASSERT_EQ(info.rois.size(), 25); // The file contains 25 frames of ROIs.

    // Check frame 0
    ASSERT_EQ(info.rois.count(0), 1);
    const auto &frame0_rois = info.rois.at(0);
    ASSERT_EQ(frame0_rois.size(), 7);
    // Check first ROI of frame 0: [12, 11, 253, 216] -> x=12, y=11, w=241, h=205
    const auto &roi0_0 = frame0_rois[0];
    EXPECT_EQ(roi0_0.left, 12);
    EXPECT_EQ(roi0_0.top, 11);
    EXPECT_EQ(roi0_0.width, 241);
    EXPECT_EQ(roi0_0.height, 205);

    // Check last ROI of frame 0: [574, 0, 706, 131] -> x=574, y=0, w=132, h=131
    const auto &roi0_6 = frame0_rois[6];
    EXPECT_EQ(roi0_6.left, 574);
    EXPECT_EQ(roi0_6.top, 0);
    EXPECT_EQ(roi0_6.width, 132);
    EXPECT_EQ(roi0_6.height, 131);

    // Check last frame (24)
    ASSERT_EQ(info.rois.count(24), 1);
    const auto &frame24_rois = info.rois.at(24);
    ASSERT_EQ(frame24_rois.size(), 9);
    // Check last ROI of last frame: [681, 0, 788, 159] -> x=681, y=0, w=107, h=159
    const auto &roi24_8 = frame24_rois[8];
    EXPECT_EQ(roi24_8.left, 681);
    EXPECT_EQ(roi24_8.top, 0);
    EXPECT_EQ(roi24_8.width, 107);
    EXPECT_EQ(roi24_8.height, 159);
}

// Tests successful loading and parsing of a valid, real-world ROI file with temporal resampling.
TEST(roi_parser_test, successful_load_with_temporal_resampling) {
    // Try multiple possible paths for the test file
    std::filesystem::path test_file = "../../test/BasketballDrill_832x480_50.txt";

    if (!std::filesystem::exists(test_file)) {
        test_file = "../../BasketballDrill_832x480_50.txt";
    }

    if (!std::filesystem::exists(test_file)) {
        test_file = "../../test/test_data/roi_data/BasketballDrill_832x480_50.txt";
    }

    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "FATAL: Test file not found. Tried multiple paths.";
    }

    const int temporal_resampling = 2;
    const auto info_opt = load_roi_file(test_file, temporal_resampling);
    ASSERT_TRUE(info_opt.has_value());
    const auto &info = *info_opt;

    // The file contains 25 frames of ROIs. All should be loaded.
    ASSERT_EQ(info.rois.size(), 25);

    // Check that the frame indices are correct (0, 2, 4, ..., 48).
    std::vector<int> frame_indices;
    for (const auto &[frame_idx, rois]: info.rois) {
        frame_indices.push_back(frame_idx);
    }
    std::sort(frame_indices.begin(), frame_indices.end());

    ASSERT_EQ(frame_indices.size(), 25);
    for (size_t i = 0; i < frame_indices.size(); ++i) {
        EXPECT_EQ(frame_indices[i], i * temporal_resampling);
    }

    // Check the content of the first frame from the file, which should be at index 0.
    ASSERT_EQ(info.rois.count(0), 1);
    const auto &frame0_rois = info.rois.at(0);
    ASSERT_EQ(frame0_rois.size(), 7);

    // Check the content of the last frame from the file (25th frame), which should be at index 48.
    const int last_frame_index = (25 - 1) * temporal_resampling;
    ASSERT_EQ(info.rois.count(last_frame_index), 1);
    const auto &last_frame_rois = info.rois.at(last_frame_index);
    ASSERT_EQ(last_frame_rois.size(), 9);
}

// Tests that temporal resampling with a factor of 0 is handled gracefully.
TEST(roi_parser_test, zero_temporal_resampling) {
    // Try multiple possible paths for the test file
    std::filesystem::path test_file = "../../test/BasketballDrill_832x480_50.txt";

    if (!std::filesystem::exists(test_file)) {
        test_file = "../../BasketballDrill_832x480_50.txt";
    }

    if (!std::filesystem::exists(test_file)) {
        test_file = "../../test/test_data/roi_data/BasketballDrill_832x480_50.txt";
    }

    if (!std::filesystem::exists(test_file)) {
        GTEST_SKIP() << "FATAL: Test file not found. Tried multiple paths.";
    }
    const auto info_opt = load_roi_file(test_file, 0);
    EXPECT_FALSE(info_opt.has_value());
}

// Tests that the parser correctly handles a non-existent file path.
TEST(roi_parser_test, file_not_found) {
    const auto info_opt = load_roi_file("non_existent_file_123.txt", 1);
    EXPECT_FALSE(info_opt.has_value());
}

// Tests that the parser rejects filenames that do not conform to the expected format.
TEST_F(roi_parser_file_test, invalid_filename) {
    // Create one valid file to ensure that content parsing isn't the issue.
    // The function should fail on the filename parsing step.
    const std::string dummy_content = "[[[0,0,1,1]]]";
    create_temp_file(test_dir / "InvalidName.txt", dummy_content);
    create_temp_file(test_dir / "Name_bad-resolution_50.txt", dummy_content);
    create_temp_file(test_dir / "Name_100x100_bad-fr.txt", dummy_content);

    EXPECT_FALSE(load_roi_file(test_dir / "InvalidName.txt", 1).has_value());
    EXPECT_FALSE(load_roi_file(test_dir / "Name_bad-resolution_50.txt", 1).has_value());
    EXPECT_FALSE(load_roi_file(test_dir / "Name_100x100_bad-fr.txt", 1).has_value());
}

// Tests that the parser correctly handles various types of malformed file content.
TEST_F(roi_parser_file_test, malformed_content) {
    const auto filename = test_dir / "Malformed_10x10_10.txt";

    // Mismatched brackets
    create_temp_file(filename, "[[[0,0,1,1]");
    EXPECT_FALSE(load_roi_file(filename, 1).has_value());

    // Non-numeric value in coordinates
    create_temp_file(filename, "[[[0,0,1,abc]]]");
    EXPECT_FALSE(load_roi_file(filename, 1).has_value());

    // Wrong number of coordinates for an ROI
    create_temp_file(filename, "[[[0,0,1]]]");
    EXPECT_FALSE(load_roi_file(filename, 1).has_value());

    // Content does not match the expected list-of-lists structure
    create_temp_file(filename, "hello world");
    EXPECT_FALSE(load_roi_file(filename, 1).has_value());
}
