// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gst/gstregistry.h>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>

#include "gst_test_fixture.h"

/**
 * @file test_gstreamer_environment.cpp
 * @brief Diagnostic test to check GStreamer environment health
 *
 * This test verifies that the GStreamer environment is properly configured
 * and can create basic pipelines. Run this test to diagnose environment issues.
 */

class GStreamerEnvironmentTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();

        // Ensure GStreamer is initialized
        if (!gst_is_initialized()) {
            gst_init(nullptr, nullptr);
        }
        // Verify initialization succeeded
        if (!gst_is_initialized()) {
            FAIL() << "GStreamer initialization failed";
        }
    }

    void TearDown() override {
        // Don't deinitialize GStreamer - let GStreamerTestFixture handle it
        // This prevents interference with other tests
        GStreamerTestFixture::TearDown();
    }
};

/**
 * @brief Test that GStreamer is initialized
 */
TEST_F(GStreamerEnvironmentTest, GStreamerInitialized) {
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";
}

/**
 * @brief Test that we can create a simple pipeline
 */
TEST_F(GStreamerEnvironmentTest, CanCreateSimplePipeline) {
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    // Try to update registry if pipeline creation fails
    GstRegistry *registry = gst_registry_get();
    if (registry) {
        gst_update_registry();
    }

    GstElement *pipeline = gst_pipeline_new("test-pipeline");
    ASSERT_NE(pipeline, nullptr) << "Cannot create simple pipeline - GStreamer environment issue";

    gst_object_unref(pipeline);
}

/**
 * @brief Test that core plugins are available
 */
TEST_F(GStreamerEnvironmentTest, CorePluginsAvailable) {
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    GstRegistry *registry = gst_registry_get();
    ASSERT_NE(registry, nullptr) << "Registry must exist";

    // Check for essential plugins (these are actual plugin names, not element names)
    const std::vector<std::string> essential_plugins = {
            "coreelements", // Contains pipeline, filesrc, fakesink, etc.
            "videotestsrc" // Standalone plugin
    };

    // Also check if we can create essential elements (more reliable test)
    const std::vector<std::string> essential_elements = {"pipeline", "filesrc", "videotestsrc", "fakesink"};

    bool all_plugins_available = true;
    for (const auto &plugin_name: essential_plugins) {
        GstPlugin *plugin = gst_registry_find_plugin(registry, plugin_name.c_str());
        if (plugin) {
            gst_object_unref(plugin);
        } else {
            all_plugins_available = false;
        }
    }

    bool all_elements_available = true;
    for (const auto &element_name: essential_elements) {
        GstElementFactory *factory = gst_element_factory_find(element_name.c_str());
        if (factory) {
            gst_object_unref(factory);
        } else {
            all_elements_available = false;
        }
    }

    const bool all_available = all_plugins_available && all_elements_available;

    if (!all_available) {
        FAIL() << "Some essential plugins are missing";
    }
}

/**
 * @brief Test that we can create pipeline elements
 */
TEST_F(GStreamerEnvironmentTest, CanCreateElements) {
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    // Test creating various elements
    const std::vector<std::string> element_names = {"pipeline", "tee", "queue", "fakesink"};

    for (const auto &element_name: element_names) {
        GstElement *element = gst_element_factory_make(element_name.c_str(), nullptr);
        ASSERT_NE(element, nullptr) << "Cannot create element: " << element_name;
        gst_object_unref(element);
    }
}

/**
 * @brief Test registry status
 */
TEST_F(GStreamerEnvironmentTest, RegistryStatus) {
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    GstRegistry *registry = gst_registry_get();
    ASSERT_NE(registry, nullptr) << "Registry must exist";

    GList *plugins = gst_registry_get_plugin_list(registry);
    ASSERT_NE(plugins, nullptr) << "Plugin list must exist";

    const guint plugin_count = g_list_length(plugins);

    ASSERT_GT(plugin_count, 0U) << "Registry has no plugins - this is a serious problem";

    g_list_free(plugins);
}

/**
 * @brief Test pipeline creation with elements
 */
TEST_F(GStreamerEnvironmentTest, CanCreatePipelineWithElements) {
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    GstElement *pipeline = gst_pipeline_new("test-pipeline");
    ASSERT_NE(pipeline, nullptr) << "Must be able to create pipeline";

    GstElement *tee = gst_element_factory_make("tee", "test-tee");
    ASSERT_NE(tee, nullptr) << "Must be able to create tee element";

    GstElement *queue = gst_element_factory_make("queue", "test-queue");
    ASSERT_NE(queue, nullptr) << "Must be able to create queue element";

    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(pipeline), tee, queue, nullptr);

    // Cleanup
    gst_object_unref(pipeline);
}

/**
 * @brief Test registry update
 */
TEST_F(GStreamerEnvironmentTest, CanUpdateRegistry) {
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    const bool updated = gst_update_registry();

    // This test always passes - registry update may return false if already up-to-date
    (void) updated;
    SUCCEED();
}

/**
 * @brief Test environment variables
 */
TEST_F(GStreamerEnvironmentTest, EnvironmentVariables) {
    // Check that environment variables can be read (test doesn't validate values)
    const char *plugin_path = g_getenv("GST_PLUGIN_PATH");
    const char *registry = g_getenv("GST_REGISTRY");
    const char *debug = g_getenv("GST_DEBUG");

    // Suppress unused variable warnings
    (void) plugin_path;
    (void) registry;
    (void) debug;

    // This test always passes - it's just informational
    SUCCEED();
}
