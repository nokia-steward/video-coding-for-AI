// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../libvcai/include/gst_raii.h"
#include "../../rtp_server/src/gst/gpu_elements.h"
#include "../common/gst_test_fixture_rtp_server.h"

/**
 * @file test_gpu_elements.cpp
 * @brief Tests for GPU element selection and creation helpers in rtp_server.
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

class gpu_elements_test_rtp_server : public GStreamerTestFixtureRtpServer {};

/**
 * @brief Verify GPU element names are selected for desktop platforms.
 */
TEST_F(gpu_elements_test_rtp_server, platform_element_names_match) {
    EXPECT_STREQ(get_encoder_element_name(), "nvh265enc");
    EXPECT_STREQ(get_default_decoder_element_name(), "nvh265dec");
}

/**
 * @brief Verify GPU element creation when factories are available.
 */
TEST_F(gpu_elements_test_rtp_server, create_gpu_elements_if_available) {
    init_gpu_elements_debug_category();

    const char *encoder_name = get_encoder_element_name();
    if (!is_element_factory_available(encoder_name)) {
        GTEST_SKIP() << "Encoder element unavailable: " << encoder_name;
    }

    const auto encoder = create_h265_encoder("test-encoder", 0);
    EXPECT_NE(encoder.get(), nullptr);

    const char *decoder_name = get_default_decoder_element_name();
    if (!is_element_factory_available(decoder_name)) {
        GTEST_SKIP() << "Decoder element unavailable: " << decoder_name;
    }

    const auto decoder = create_h265_decoder("test-decoder", decoder_name);
    EXPECT_NE(decoder.get(), nullptr);

    EXPECT_TRUE(is_registered_decoder_element("openhevcdec"));

    if (!is_element_factory_available("nvvideoconvert")) {
        GTEST_SKIP() << "Converter element unavailable: nvvideoconvert";
    }

    const auto converter = create_gpu_video_convert("test-converter");
    EXPECT_NE(converter.get(), nullptr);
}

/**
 * @brief When openhevcdec plugin is present, creation and postprocess convert must succeed.
 */
TEST_F(gpu_elements_test_rtp_server, openhevcdec_elements_if_available) {
    init_gpu_elements_debug_category();
    if (!is_element_factory_available("openhevcdec")) {
        GTEST_SKIP() << "openhevcdec plugin unavailable";
    }
    const auto decoder = create_h265_decoder("test-openhevc-decoder", "openhevcdec");
    EXPECT_NE(decoder.get(), nullptr);
    EXPECT_TRUE(decoder_outputs_system_memory("openhevcdec"));
    const auto postprocess = create_decoded_postprocess_convert("test-openhevc-postprocess", "openhevcdec");
    EXPECT_NE(postprocess.get(), nullptr);
}
