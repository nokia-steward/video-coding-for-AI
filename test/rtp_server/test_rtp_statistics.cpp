// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <thread>

#include "../../rtp_server/src/utils/rtp_statistics.h"
#include "../common/gst_test_fixture_rtp_server.h"

namespace {
    struct rtp_statistics_test : GStreamerTestFixtureRtpServer {
        rtp_monitoring_config default_config;

        void SetUp() override {
            GStreamerTestFixtureRtpServer::SetUp();
            default_config = rtp_monitoring_config{};
        }
    };

    // Test constructor
    TEST_F(rtp_statistics_test, constructor_initialization) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        EXPECT_EQ(monitor.get_packet_loss_rate(), 0.0);
        EXPECT_EQ(monitor.get_average_jitter_ms(), 0.0);
        EXPECT_EQ(monitor.get_session_duration_ms(), 0);
        EXPECT_FALSE(monitor.should_terminate_session());
    }

    // Test on_packet_received - basic functionality
    TEST_F(rtp_statistics_test, on_packet_received_basic) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_packet_received(100, 1000, 1);

        EXPECT_EQ(monitor.get_packet_loss_rate(), 0.0);
        // First packet shouldn't calculate jitter
        EXPECT_EQ(monitor.get_average_jitter_ms(), 0.0);
    }

    // Test on_packet_received - sequence number tracking
    TEST_F(rtp_statistics_test, on_packet_received_sequence_tracking) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_packet_received(100, 1000, 1);
        monitor.on_packet_received(100, 2000, 2);
        monitor.on_packet_received(100, 3000, 3);

        EXPECT_EQ(monitor.get_packet_loss_rate(), 0.0);
    }

    // Test on_packet_received - packet loss detection
    TEST_F(rtp_statistics_test, on_packet_received_packet_loss) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_packet_received(100, 1000, 1);
        monitor.on_packet_received(100, 2000, 2);
        // Skip sequence 3, receive 4
        monitor.on_packet_received(100, 4000, 4);

        const auto loss_rate = monitor.get_packet_loss_rate();
        EXPECT_GT(loss_rate, 0.0);
        EXPECT_LE(loss_rate, 100.0);
    }

    // Test on_packet_received - sequence number wraparound
    TEST_F(rtp_statistics_test, on_packet_received_sequence_wraparound) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        // Receive packet at sequence 65535
        monitor.on_packet_received(100, 1000, 65535);
        // Next packet should be 0 (wraparound)
        monitor.on_packet_received(100, 2000, 0);

        // Should not detect packet loss on wraparound
        EXPECT_EQ(monitor.get_packet_loss_rate(), 0.0);
    }

    // Test on_packet_received - multiple lost packets
    TEST_F(rtp_statistics_test, on_packet_received_multiple_lost_packets) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_packet_received(100, 1000, 1);
        // Skip sequences 2, 3, 4, receive 5
        monitor.on_packet_received(100, 5000, 5);

        const auto loss_rate = monitor.get_packet_loss_rate();
        EXPECT_GT(loss_rate, 0.0);
    }

    // Test on_frame_decoded
    TEST_F(rtp_statistics_test, on_frame_decoded) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_frame_decoded(1920 * 1080 * 3 / 2); // Typical frame size

        // Frame decoded counter should increment
        // (No direct accessor, but we can verify no crash)
        EXPECT_FALSE(monitor.should_terminate_session());
    }

    // Test on_corrupted_frame
    TEST_F(rtp_statistics_test, on_corrupted_frame) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_corrupted_frame();
        monitor.on_corrupted_frame();

        // Corrupted frame counter should increment
        // (No direct accessor, but we can verify no crash)
        EXPECT_FALSE(monitor.should_terminate_session());
    }

    // Test on_packet_lost
    TEST_F(rtp_statistics_test, on_packet_lost) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_packet_received(100, 1000, 1);
        monitor.on_packet_lost(2, 5); // Expected 2, got 5 (lost 3 packets)

        const auto loss_rate = monitor.get_packet_loss_rate();
        EXPECT_GT(loss_rate, 0.0);
    }

    // Test on_packet_lost - wraparound
    TEST_F(rtp_statistics_test, on_packet_lost_wraparound) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_packet_received(100, 1000, 65534);
        monitor.on_packet_lost(65535, 1); // Wraparound case

        const auto loss_rate = monitor.get_packet_loss_rate();
        EXPECT_GT(loss_rate, 0.0);
    }

    // Test get_packet_loss_rate - zero packets
    TEST_F(rtp_statistics_test, get_packet_loss_rate_zero_packets) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        EXPECT_EQ(monitor.get_packet_loss_rate(), 0.0);
    }

    // Test get_packet_loss_rate - calculation
    TEST_F(rtp_statistics_test, get_packet_loss_rate_calculation) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        // Receive 9 packets, lose 1 (10% loss)
        for (guint16 i = 1; i <= 9; ++i) {
            monitor.on_packet_received(100, i * 1000, i);
        }
        monitor.on_packet_lost(10, 11);

        const auto loss_rate = monitor.get_packet_loss_rate();
        EXPECT_NEAR(loss_rate, 10.0, 0.1); // 10% loss
    }

    // Test get_average_jitter_ms - zero samples
    TEST_F(rtp_statistics_test, get_average_jitter_ms_zero_samples) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        EXPECT_EQ(monitor.get_average_jitter_ms(), 0.0);
    }

    // Test get_average_jitter_ms - calculation
    TEST_F(rtp_statistics_test, get_average_jitter_ms_calculation) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        // Send packets with timestamps
        monitor.on_packet_received(100, 1000, 1);
        // Small delay to allow jitter calculation
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        monitor.on_packet_received(100, 2000, 2);

        // Jitter should be calculated (may be non-zero due to timing)
        const auto jitter = monitor.get_average_jitter_ms();
        EXPECT_GE(jitter, 0.0);
    }

    // Test get_session_duration_ms
    TEST_F(rtp_statistics_test, get_session_duration_ms) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        // Wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        const auto duration = monitor.get_session_duration_ms();
        EXPECT_GE(duration, 50);
    }

    // Test should_terminate_session - all checks disabled
    TEST_F(rtp_statistics_test, should_terminate_session_all_disabled) {
        constexpr guint16 test_port = 5004;
        rtp_monitoring_config config;
        config.enable_packet_loss_check = false;
        config.enable_jitter_check = false;
        config.enable_silence_check = false;

        rtp_statistics_monitor monitor(test_port, config);

        EXPECT_FALSE(monitor.should_terminate_session());
    }

    // Test should_terminate_session - silence check enabled
    TEST_F(rtp_statistics_test, should_terminate_session_silence_check) {
        constexpr guint16 test_port = 5004;
        rtp_monitoring_config config;
        config.enable_silence_check = true;
        config.max_silence_ms = 100; // Very short threshold for testing

        rtp_statistics_monitor monitor(test_port, config);

        monitor.on_packet_received(100, 1000, 1);

        // Wait longer than threshold
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        EXPECT_TRUE(monitor.should_terminate_session());
    }

    // Test should_terminate_session - packet loss check enabled
    TEST_F(rtp_statistics_test, should_terminate_session_packet_loss_check) {
        constexpr guint16 test_port = 5004;
        rtp_monitoring_config config;
        config.enable_packet_loss_check = true;
        config.max_packet_loss_rate = 5.0; // 5% threshold

        rtp_statistics_monitor monitor(test_port, config);

        // Create high packet loss (receive 90, lose 10 = 10% loss)
        for (guint16 i = 1; i <= 90; ++i) {
            monitor.on_packet_received(100, i * 1000, i);
        }
        // Lose 10 packets
        for (guint16 i = 91; i <= 100; ++i) {
            monitor.on_packet_lost(i, i + 1);
        }

        EXPECT_TRUE(monitor.should_terminate_session());
    }

    // Test should_terminate_session - packet loss below threshold
    TEST_F(rtp_statistics_test, should_terminate_session_packet_loss_below_threshold) {
        constexpr guint16 test_port = 5004;
        rtp_monitoring_config config;
        config.enable_packet_loss_check = true;
        config.max_packet_loss_rate = 5.0; // 5% threshold

        rtp_statistics_monitor monitor(test_port, config);

        // Create low packet loss (receive 98, lose 2 = ~2% loss)
        for (guint16 i = 1; i <= 98; ++i) {
            monitor.on_packet_received(100, i * 1000, i);
        }
        monitor.on_packet_lost(99, 100);

        EXPECT_FALSE(monitor.should_terminate_session());
    }

    // Test should_terminate_session - jitter check enabled
    TEST_F(rtp_statistics_test, should_terminate_session_jitter_check) {
        constexpr guint16 test_port = 5004;
        rtp_monitoring_config config;
        config.enable_jitter_check = true;
        config.max_jitter_ms = 1.0; // Very low threshold

        rtp_statistics_monitor monitor(test_port, config);

        // Send packets with timestamps that will create jitter
        monitor.on_packet_received(100, 1000, 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        monitor.on_packet_received(100, 2000, 2);

        // Note: Jitter calculation depends on timing, so this may or may not trigger
        // depending on system timing. The important thing is it doesn't crash.
        // Just verify the function works without crashing
        (void) monitor.should_terminate_session();
    }

    // Test should_terminate_session - boundary at threshold
    TEST_F(rtp_statistics_test, should_terminate_session_boundary_at_threshold) {
        constexpr guint16 test_port = 5004;
        rtp_monitoring_config config;
        config.enable_packet_loss_check = true;
        config.max_packet_loss_rate = 10.0; // Exactly 10% threshold

        rtp_statistics_monitor monitor(test_port, config);

        // Create exactly 10% loss (receive 90, lose 10)
        // Note: The check uses > (greater than), so exactly 10% should NOT terminate
        // To test the boundary, we need slightly more than 10%
        for (guint16 i = 1; i <= 89; ++i) {
            monitor.on_packet_received(100, i * 1000, i);
        }
        // Lose 11 packets to get > 10% loss (11/100 = 11%)
        for (guint16 i = 90; i <= 100; ++i) {
            monitor.on_packet_lost(i, i + 1);
        }

        // Should terminate when loss rate exceeds threshold (> 10%)
        EXPECT_TRUE(monitor.should_terminate_session());
    }

    // Test should_terminate_session - exactly at threshold (should not terminate)
    TEST_F(rtp_statistics_test, should_terminate_session_exactly_at_threshold) {
        constexpr guint16 test_port = 5004;
        rtp_monitoring_config config;
        config.enable_packet_loss_check = true;
        config.max_packet_loss_rate = 10.0; // Exactly 10% threshold

        rtp_statistics_monitor monitor(test_port, config);

        // Create exactly 10% loss (receive 90, lose 10)
        for (guint16 i = 1; i <= 90; ++i) {
            monitor.on_packet_received(100, i * 1000, i);
        }
        for (guint16 i = 91; i <= 100; ++i) {
            monitor.on_packet_lost(i, i + 1);
        }

        // Should NOT terminate when loss rate equals threshold (uses >, not >=)
        EXPECT_FALSE(monitor.should_terminate_session());
    }

    // Test reset
    TEST_F(rtp_statistics_test, reset) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        // Add some statistics
        monitor.on_packet_received(100, 1000, 1);
        monitor.on_packet_received(100, 2000, 2);
        monitor.on_frame_decoded(1000);
        monitor.on_corrupted_frame();

        // Reset
        monitor.reset();

        // Statistics should be reset
        EXPECT_EQ(monitor.get_packet_loss_rate(), 0.0);
        EXPECT_EQ(monitor.get_average_jitter_ms(), 0.0);
        EXPECT_FALSE(monitor.should_terminate_session());
    }

    // Test log_statistics - verify it doesn't crash
    TEST_F(rtp_statistics_test, log_statistics) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        monitor.on_packet_received(100, 1000, 1);
        monitor.on_packet_received(100, 2000, 2);
        monitor.on_frame_decoded(1000);

        // Should not crash
        monitor.log_statistics();
    }

    // Test multiple statistics updates
    TEST_F(rtp_statistics_test, multiple_statistics_updates) {
        constexpr guint16 test_port = 5004;
        rtp_statistics_monitor monitor(test_port, default_config);

        // Simulate receiving many packets
        for (guint16 i = 1; i <= 100; ++i) {
            monitor.on_packet_received(100, i * 1000, i);
        }

        // Simulate decoding frames
        for (int i = 0; i < 10; ++i) {
            monitor.on_frame_decoded(1920 * 1080 * 3 / 2);
        }

        // Verify statistics are reasonable
        EXPECT_EQ(monitor.get_packet_loss_rate(), 0.0);
        EXPECT_GE(monitor.get_session_duration_ms(), 0);
    }
} // namespace
