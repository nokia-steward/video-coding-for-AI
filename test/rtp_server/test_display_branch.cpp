// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "../../libvcai/include/gst_raii.h"
#include "../../rtp_server/src/pipeline/output/display/deepstream_config.h"
#include "../../rtp_server/src/pipeline/output/display/display_branch.h"
#include "../../rtp_server/src/pipeline/output/display/label_utils.h"

namespace {
    class display_helpers_test : public ::testing::Test {
    protected:
        std::filesystem::path temp_dir;

        void SetUp() override {
            const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            temp_dir = std::filesystem::temp_directory_path() / ("rtp_server_display_helpers_" + suffix);
            std::filesystem::create_directories(temp_dir);
        }

        void TearDown() override { std::filesystem::remove_all(temp_dir); }
    };
} // namespace

TEST(RtpServerDisplayBranch, FunctionTableHandlesNullPipelineInputs) {
    const auto context = create_display_branch_context();
    const auto &functions = context.functions;

    EXPECT_TRUE(functions.is_enabled(nullptr));
    EXPECT_FALSE(functions.create_elements(nullptr));
    EXPECT_FALSE(functions.configure_elements(nullptr));
    EXPECT_FALSE(functions.link_branch(nullptr));
    EXPECT_FALSE(functions.link_to_tee(nullptr, nullptr));
    EXPECT_FALSE(functions.add_to_pipeline(nullptr, nullptr));

    EXPECT_NO_THROW(functions.cleanup(nullptr));
    EXPECT_STREQ(functions.get_branch_name(), "display");
}

TEST_F(display_helpers_test, label_utils_handle_trim_normalize_split_and_file_reading) {
    EXPECT_EQ(trim("  hello \n"), "hello");
    EXPECT_EQ(normalize_label_name("  CaR "), "car");
    EXPECT_TRUE(trim("   \t\r\n").empty());
    EXPECT_TRUE(split_labels(" , , ").empty());

    const auto split = split_labels(" person,car,   bus ,,");
    ASSERT_EQ(split.size(), 3U);
    EXPECT_EQ(split[0], "person");
    EXPECT_EQ(split[1], "car");
    EXPECT_EQ(split[2], "bus");

    const auto label_file = temp_dir / "labels.txt";
    std::ofstream stream(label_file);
    ASSERT_TRUE(stream.is_open());
    stream << "person\n";
    stream << "car\n";
    stream << " bus \n";
    stream.close();

    const auto labels = read_label_file(label_file);
    ASSERT_EQ(labels.size(), 3U);
    EXPECT_EQ(labels[0], "person");
    EXPECT_EQ(labels[1], "car");
    EXPECT_EQ(labels[2], "bus");

    const auto missing_labels = read_label_file(temp_dir / "does_not_exist.txt");
    EXPECT_TRUE(missing_labels.empty());
}

TEST_F(display_helpers_test, get_label_file_path_and_class_ids_read_from_nvinfer_configuration) {
    sf::unique_gst_element_ptr nvinfer(gst_element_factory_make("nvinfer", "display-test-nvinfer"));
    if (!nvinfer) {
        GTEST_SKIP() << "nvinfer plugin not available in this environment";
    }

    const auto label_file = temp_dir / "labels.txt";
    {
        std::ofstream labels_stream(label_file);
        ASSERT_TRUE(labels_stream.is_open());
        labels_stream << "person\ncar\nbicycle\n";
    }

    const auto config_file = temp_dir / "nvinfer_config.txt";
    {
        std::ofstream config_stream(config_file);
        ASSERT_TRUE(config_stream.is_open());
        config_stream << "labelfile-path = labels.txt\n";
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(nvinfer.get()), "config-file-path") == nullptr) {
        GTEST_SKIP() << "nvinfer element does not expose config-file-path property";
    }
    g_object_set(G_OBJECT(nvinfer.get()), "config-file-path", config_file.c_str(), nullptr);

    const std::string resolved_label_path = get_label_file_path(nvinfer.get());
    EXPECT_EQ(std::filesystem::path(resolved_label_path), label_file);

    const auto filtered_ids = get_class_ids(nvinfer.get(), {"CAR", "person"});
    ASSERT_EQ(filtered_ids.size(), 2U);
    EXPECT_EQ(filtered_ids[0], 0U);
    EXPECT_EQ(filtered_ids[1], 1U);

    const auto all_ids = get_class_ids(nvinfer.get(), {});
    ASSERT_EQ(all_ids.size(), 3U);
    EXPECT_EQ(all_ids[2], 2U);
}

TEST_F(display_helpers_test, get_label_file_path_returns_empty_when_config_has_no_label_key) {
    sf::unique_gst_element_ptr nvinfer(gst_element_factory_make("nvinfer", "display-test-nvinfer-no-label-key"));
    if (!nvinfer) {
        GTEST_SKIP() << "nvinfer plugin not available in this environment";
    }

    const auto config_file = temp_dir / "nvinfer_no_label_key.txt";
    {
        std::ofstream config_stream(config_file);
        ASSERT_TRUE(config_stream.is_open());
        config_stream << "network-mode=2\n";
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(nvinfer.get()), "config-file-path") == nullptr) {
        GTEST_SKIP() << "nvinfer element does not expose config-file-path property";
    }
    g_object_set(G_OBJECT(nvinfer.get()), "config-file-path", config_file.c_str(), nullptr);

    EXPECT_TRUE(get_label_file_path(nvinfer.get()).empty());
}

TEST_F(display_helpers_test, get_class_ids_returns_empty_when_label_file_path_is_missing) {
    sf::unique_gst_element_ptr nvinfer(gst_element_factory_make("nvinfer", "display-test-nvinfer-missing-label"));
    if (!nvinfer) {
        GTEST_SKIP() << "nvinfer plugin not available in this environment";
    }

    const auto config_file = temp_dir / "nvinfer_missing_label.txt";
    {
        std::ofstream config_stream(config_file);
        ASSERT_TRUE(config_stream.is_open());
        config_stream << "labelfile-path=does_not_exist.txt\n";
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(nvinfer.get()), "config-file-path") == nullptr) {
        GTEST_SKIP() << "nvinfer element does not expose config-file-path property";
    }
    g_object_set(G_OBJECT(nvinfer.get()), "config-file-path", config_file.c_str(), nullptr);

    const auto class_ids = get_class_ids(nvinfer.get(), {"person"});
    EXPECT_TRUE(class_ids.empty());
}

TEST_F(display_helpers_test, get_label_file_path_supports_case_insensitive_key_and_colon_separator) {
    sf::unique_gst_element_ptr nvinfer(gst_element_factory_make("nvinfer", "display-test-nvinfer-colon"));
    if (!nvinfer) {
        GTEST_SKIP() << "nvinfer plugin not available in this environment";
    }

    const auto label_file = temp_dir / "labels_case_colon.txt";
    {
        std::ofstream labels_stream(label_file);
        ASSERT_TRUE(labels_stream.is_open());
        labels_stream << "alpha\nbeta\n";
    }

    const auto config_file = temp_dir / "nvinfer_colon_config.txt";
    {
        std::ofstream config_stream(config_file);
        ASSERT_TRUE(config_stream.is_open());
        config_stream << "LABELFILE-PATH : labels_case_colon.txt\n";
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(nvinfer.get()), "config-file-path") == nullptr) {
        GTEST_SKIP() << "nvinfer element does not expose config-file-path property";
    }
    g_object_set(G_OBJECT(nvinfer.get()), "config-file-path", config_file.c_str(), nullptr);

    const std::string resolved_label_path = get_label_file_path(nvinfer.get());
    EXPECT_EQ(std::filesystem::path(resolved_label_path), label_file);
}

TEST_F(display_helpers_test, get_label_file_path_keeps_absolute_label_path) {
    sf::unique_gst_element_ptr nvinfer(gst_element_factory_make("nvinfer", "display-test-nvinfer-absolute"));
    if (!nvinfer) {
        GTEST_SKIP() << "nvinfer plugin not available in this environment";
    }

    const auto label_file = temp_dir / "labels_absolute.txt";
    {
        std::ofstream labels_stream(label_file);
        ASSERT_TRUE(labels_stream.is_open());
        labels_stream << "person\ncar\n";
    }

    const auto config_file = temp_dir / "nvinfer_absolute_config.txt";
    {
        std::ofstream config_stream(config_file);
        ASSERT_TRUE(config_stream.is_open());
        config_stream << "labelfile-path=" << label_file.string() << "\n";
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(nvinfer.get()), "config-file-path") == nullptr) {
        GTEST_SKIP() << "nvinfer element does not expose config-file-path property";
    }
    g_object_set(G_OBJECT(nvinfer.get()), "config-file-path", config_file.c_str(), nullptr);

    EXPECT_EQ(std::filesystem::path(get_label_file_path(nvinfer.get())), label_file);
}

TEST_F(display_helpers_test, get_class_ids_returns_all_for_empty_filter_and_empty_for_unknown_labels) {
    sf::unique_gst_element_ptr nvinfer(gst_element_factory_make("nvinfer", "display-test-nvinfer-class-ids"));
    if (!nvinfer) {
        GTEST_SKIP() << "nvinfer plugin not available in this environment";
    }

    const auto label_file = temp_dir / "labels_for_class_ids.txt";
    {
        std::ofstream labels_stream(label_file);
        ASSERT_TRUE(labels_stream.is_open());
        labels_stream << "person\ncar\nbicycle\n";
    }

    const auto config_file = temp_dir / "nvinfer_class_ids_config.txt";
    {
        std::ofstream config_stream(config_file);
        ASSERT_TRUE(config_stream.is_open());
        config_stream << "labelfile-path = labels_for_class_ids.txt\n";
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(nvinfer.get()), "config-file-path") == nullptr) {
        GTEST_SKIP() << "nvinfer element does not expose config-file-path property";
    }
    g_object_set(G_OBJECT(nvinfer.get()), "config-file-path", config_file.c_str(), nullptr);

    const auto all_class_ids = get_class_ids(nvinfer.get(), {});
    ASSERT_EQ(all_class_ids.size(), 3U);
    EXPECT_EQ(all_class_ids[0], 0U);
    EXPECT_EQ(all_class_ids[1], 1U);
    EXPECT_EQ(all_class_ids[2], 2U);

    const auto unknown_class_ids = get_class_ids(nvinfer.get(), {"truck"});
    EXPECT_TRUE(unknown_class_ids.empty());
}

TEST_F(display_helpers_test, deepstream_config_helpers_reject_missing_inputs) {
    EXPECT_FALSE(configure_nvinfer(nullptr, "missing.yml"));
    sf::unique_gst_element_ptr identity_nvinfer(gst_element_factory_make("identity", "identity-nvinfer"));
    ASSERT_NE(identity_nvinfer, nullptr);
    EXPECT_FALSE(configure_nvinfer(identity_nvinfer.get(), "missing.yml"));

    const std::string explicit_tracker_config = "/tmp/explicit_tracker_config.yml";
    EXPECT_EQ(determine_tracker_config_path(explicit_tracker_config), explicit_tracker_config);
}

TEST_F(display_helpers_test, configure_nvtracker_validates_required_paths) {
    sf::unique_gst_element_ptr tracker(gst_element_factory_make("identity", "identity-nvtracker"));
    ASSERT_NE(tracker, nullptr);

    const auto existing_config = temp_dir / "tracker.yml";
    const auto existing_library = temp_dir / "libnvtracker.so";

    {
        std::ofstream config_stream(existing_config);
        ASSERT_TRUE(config_stream.is_open());
        config_stream << "tracker: {}\n";
    }
    {
        std::ofstream library_stream(existing_library);
        ASSERT_TRUE(library_stream.is_open());
        library_stream << "placeholder";
    }

    EXPECT_FALSE(configure_nvtracker(nullptr, existing_config.string(), existing_library.string(), 1280, 720));
    EXPECT_FALSE(configure_nvtracker(tracker.get(), (temp_dir / "missing.yml").string(), existing_library.string(),
                                     1280, 720));
    EXPECT_FALSE(configure_nvtracker(tracker.get(), existing_config.string(), (temp_dir / "missing.so").string(), 1280,
                                     720));
}
