// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <gst/gst.h>
#include <gtest/gtest.h>
#include "test_debug_categories.h"

/**
 * @file gst_test_fixture.height
 * @brief Global GStreamer test fixture to prevent double initialization/deinitialization
 *
 * This fixture ensures GStreamer is initialized only once for all tests and
 * deinitialized only once when all tests are complete.
 *
 * Uses a static guard to prevent multiple test suites from deinitializing GStreamer.
 */

class GStreamerTestFixture : public ::testing::Test {
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
            init_all_test_debug_categories();
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
            init_all_test_debug_categories();
            debug_categories_initialized.store(true);
        }
    }

    void TearDown() override {
        // Don't deinitialize GStreamer - keep it initialized for other tests
    }
};

// Initialize the static atomic flags
inline std::atomic<bool> GStreamerTestFixture::gstreamer_deinitialized{false};
inline std::atomic<bool> GStreamerTestFixture::debug_categories_initialized{false};
