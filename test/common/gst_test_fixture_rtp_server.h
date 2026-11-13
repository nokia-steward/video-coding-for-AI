// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <gst/gst.h>
#include <gtest/gtest.h>
#include "test_debug_categories_rtp_server.h"

/**
 * @file gst_test_fixture_rtp_server.height
 * @brief GStreamer test fixture for rtp_server tests
 *
 * This fixture ensures GStreamer is initialized only once for all tests and
 * initializes rtp_server-specific debug categories.
 */

class GStreamerTestFixtureRtpServer : public ::testing::Test {
protected:
    // Static guard to ensure GStreamer is only deinitialized once across all test suites
    static std::atomic<bool> gstreamer_deinitialized;
    static std::atomic<bool> debug_categories_initialized;

    static void SetUpTestSuite() {
        // Initialize GStreamer once for all tests
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }

        // Update registry to ensure plugins are available
        GstRegistry *registry = gst_registry_get();
        if (registry) {
            gst_update_registry();
        }

        // Initialize all debug categories once for all tests
        if (!debug_categories_initialized.load()) {
            init_all_rtp_server_test_debug_categories();
            debug_categories_initialized.store(true);
        }
    }

    static void TearDownTestSuite() {
        // DON'T deinitialize GStreamer - let it stay initialized for other test suites
        // Deinitializing GStreamer can cause issues when tests run in different orders
        // The OS will clean up GStreamer when the process exits
    }

    // Individual test setup/teardown - ensure GStreamer is initialized
    void SetUp() override {
        // Ensure GStreamer is initialized (defensive check)
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }

        // Update registry to ensure plugins are available
        GstRegistry *registry = gst_registry_get();
        if (registry) {
            gst_update_registry();
        }

        // Ensure debug categories are initialized (defensive check)
        if (!debug_categories_initialized.load()) {
            init_all_rtp_server_test_debug_categories();
            debug_categories_initialized.store(true);
        }
    }

    void TearDown() override {
        // Don't deinitialize GStreamer - keep it initialized for other tests
    }
};

// Initialize the static atomic flags
inline std::atomic<bool> GStreamerTestFixtureRtpServer::gstreamer_deinitialized{false};
inline std::atomic<bool> GStreamerTestFixtureRtpServer::debug_categories_initialized{false};
