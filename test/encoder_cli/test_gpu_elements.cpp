// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/gst/gpu_elements.h"
#include "../../libvcai/include/gst_raii.h"
#include "../common/gst_test_fixture.h"

/**
 * @file test_gpu_elements.cpp
 * @brief Tests for GPU element selection and creation helpers.
 */

namespace {
    [[nodiscard]] bool is_element_factory_available(const char *element_name) {
        if (!element_name || element_name[0] == '\0') {
            return false;
        }

        GstElementFactory *factory = gst_element_factory_find(element_name);
        if (!factory) {
            return false;
        }

        gst_object_unref(factory);
        return true;
    }
} // namespace

class gpu_elements_test : public GStreamerTestFixture {};

/**
 * @brief Verify GPU element names are selected per platform.
 */
TEST_F(gpu_elements_test, platform_element_names_match) {
#if defined(USE_ARM64_ENCODER) && USE_ARM64_ENCODER == 1
    EXPECT_STREQ(get_encoder_element_name(), "nvv4l2h265enc");
#else
    EXPECT_STREQ(get_encoder_element_name(), "nvh265enc");
#endif

#if defined(USE_ARM64_DECODER) && USE_ARM64_DECODER == 1
    EXPECT_STREQ(get_decoder_element_name(), "nvv4l2decoder");
#else
    EXPECT_STREQ(get_decoder_element_name(), "nvh265dec");
#endif
}

/**
 * @brief Verify GPU element creation when factories are available.
 */
TEST_F(gpu_elements_test, create_gpu_elements_if_available) {
    const char *encoder_name = get_encoder_element_name();
    if (!encoder_supported_on_current_build(encoder_name)) {
        GTEST_SKIP() << "Encoder not supported on this build: " << encoder_name;
    }
    if (!is_element_factory_available(encoder_name)) {
        GTEST_SKIP() << "Encoder element unavailable: " << encoder_name;
    }

    const auto encoder = create_h265_encoder("test-encoder", encoder_name, 0, std::nullopt);
    EXPECT_NE(encoder.get(), nullptr);

    const char *decoder_name = get_decoder_element_name();
    if (!is_element_factory_available(decoder_name)) {
        GTEST_SKIP() << "Decoder element unavailable: " << decoder_name;
    }

    const auto decoder = create_h265_decoder("test-decoder");
    EXPECT_NE(decoder.get(), nullptr);

    if (!is_element_factory_available("nvvideoconvert")) {
        GTEST_SKIP() << "Converter element unavailable: nvvideoconvert";
    }

    const auto converter = create_gpu_video_convert("test-converter");
    EXPECT_NE(converter.get(), nullptr);
}

TEST_F(gpu_elements_test, nvv4l2_qp_range_locks_i_p_and_b) {
    EXPECT_EQ(format_nvv4l2_qp_range(42), "42,42:42,42:42,42");
    EXPECT_EQ(format_nvv4l2_qp_range(0), "0,0:0,0:0,0");
}

TEST_F(gpu_elements_test, jetson_encoder_applies_config_qp_range_when_available) {
    constexpr int configured_qp = 42;
    if (!encoder_supported_on_current_build("nvv4l2h265enc")) {
        GTEST_SKIP() << "nvv4l2h265enc not supported on this build (Jetson encoder path only)";
    }
    if (!is_element_factory_available("nvv4l2h265enc")) {
        GTEST_SKIP() << "nvv4l2h265enc unavailable";
    }

    const auto encoder = create_h265_encoder("test-qp-encoder", "nvv4l2h265enc", 0, configured_qp);
    ASSERT_NE(encoder.get(), nullptr);

    gchar *qp_range = nullptr;
    gint control_rate = -1;
    g_object_get(G_OBJECT(encoder.get()), "qp-range", &qp_range, "control-rate", &control_rate, nullptr);
    ASSERT_NE(qp_range, nullptr);
    EXPECT_STREQ(qp_range, format_nvv4l2_qp_range(configured_qp).c_str());
    EXPECT_EQ(control_rate, 0);
    g_free(qp_range);
}
