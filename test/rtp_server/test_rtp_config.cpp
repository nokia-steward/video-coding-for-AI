// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../rtp_server/src/core/rtp_config.h"

namespace {
    struct rtp_config_test : ::testing::Test {};

    // Test transport_type_to_string
    TEST_F(rtp_config_test, transport_type_to_string_rtp) {
        EXPECT_STREQ(transport_type_to_string(transport_type::rtp), "rtp");
    }

    TEST_F(rtp_config_test, transport_type_to_string_webrtc) {
        EXPECT_STREQ(transport_type_to_string(transport_type::webrtc), "webrtc");
    }

    // Test string_to_transport_type
    TEST_F(rtp_config_test, string_to_transport_type_rtp) {
        EXPECT_EQ(string_to_transport_type("rtp"), transport_type::rtp);
    }

    TEST_F(rtp_config_test, string_to_transport_type_webrtc) {
        EXPECT_EQ(string_to_transport_type("webrtc"), transport_type::webrtc);
    }

    TEST_F(rtp_config_test, string_to_transport_type_case_sensitive) {
        // Should be case-sensitive, uppercase should default to rtp
        EXPECT_EQ(string_to_transport_type("RTP"), transport_type::rtp);
        EXPECT_EQ(string_to_transport_type("Rtp"), transport_type::rtp);
        EXPECT_EQ(string_to_transport_type("WEBRTC"), transport_type::rtp);
    }

    TEST_F(rtp_config_test, string_to_transport_type_invalid) {
        // Invalid strings should default to rtp
        EXPECT_EQ(string_to_transport_type("invalid"), transport_type::rtp);
        EXPECT_EQ(string_to_transport_type(""), transport_type::rtp);
        EXPECT_EQ(string_to_transport_type("tcp"), transport_type::rtp);
    }

    // Test webrtc_signaling_mode_to_string
    TEST_F(rtp_config_test, webrtc_signaling_mode_to_string_embedded) {
        EXPECT_STREQ(webrtc_signaling_mode_to_string(webrtc_signaling_mode::embedded), "embedded");
    }

    TEST_F(rtp_config_test, webrtc_signaling_mode_to_string_external) {
        EXPECT_STREQ(webrtc_signaling_mode_to_string(webrtc_signaling_mode::external), "external");
    }

    // Test string_to_webrtc_signaling_mode
    TEST_F(rtp_config_test, string_to_webrtc_signaling_mode_embedded) {
        EXPECT_EQ(string_to_webrtc_signaling_mode("embedded"), webrtc_signaling_mode::embedded);
    }

    TEST_F(rtp_config_test, string_to_webrtc_signaling_mode_external) {
        EXPECT_EQ(string_to_webrtc_signaling_mode("external"), webrtc_signaling_mode::external);
    }

    TEST_F(rtp_config_test, string_to_webrtc_signaling_mode_case_sensitive) {
        // Should be case-sensitive, uppercase should default to embedded
        EXPECT_EQ(string_to_webrtc_signaling_mode("EMBEDDED"), webrtc_signaling_mode::embedded);
        EXPECT_EQ(string_to_webrtc_signaling_mode("Embedded"), webrtc_signaling_mode::embedded);
        EXPECT_EQ(string_to_webrtc_signaling_mode("EXTERNAL"), webrtc_signaling_mode::embedded);
    }

    TEST_F(rtp_config_test, string_to_webrtc_signaling_mode_invalid) {
        // Invalid strings should default to embedded
        EXPECT_EQ(string_to_webrtc_signaling_mode("invalid"), webrtc_signaling_mode::embedded);
        EXPECT_EQ(string_to_webrtc_signaling_mode(""), webrtc_signaling_mode::embedded);
        EXPECT_EQ(string_to_webrtc_signaling_mode("mixed"), webrtc_signaling_mode::embedded);
    }

    // Test round-trip conversions
    TEST_F(rtp_config_test, transport_type_round_trip_rtp) {
        const auto type = transport_type::rtp;
        const auto str = transport_type_to_string(type);
        const auto converted = string_to_transport_type(str);
        EXPECT_EQ(converted, type);
    }

    TEST_F(rtp_config_test, transport_type_round_trip_webrtc) {
        const auto type = transport_type::webrtc;
        const auto str = transport_type_to_string(type);
        const auto converted = string_to_transport_type(str);
        EXPECT_EQ(converted, type);
    }

    TEST_F(rtp_config_test, webrtc_signaling_mode_round_trip_embedded) {
        const auto mode = webrtc_signaling_mode::embedded;
        const auto str = webrtc_signaling_mode_to_string(mode);
        const auto converted = string_to_webrtc_signaling_mode(str);
        EXPECT_EQ(converted, mode);
    }

    TEST_F(rtp_config_test, webrtc_signaling_mode_round_trip_external) {
        const auto mode = webrtc_signaling_mode::external;
        const auto str = webrtc_signaling_mode_to_string(mode);
        const auto converted = string_to_webrtc_signaling_mode(str);
        EXPECT_EQ(converted, mode);
    }
} // namespace
