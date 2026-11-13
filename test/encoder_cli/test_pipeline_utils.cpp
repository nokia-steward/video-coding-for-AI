// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/gst/pipeline_utils.h"
#include "../common/gst_test_fixture.h"
#include "../libvcai/include/gst_raii.h"

/**
 * @file test_pipeline_utils.cpp
 * @brief Tests for pipeline utility functions.
 */

class PipelineUtilsTest : public GStreamerTestFixture {
protected:
    void SetUp() override {
        GStreamerTestFixture::SetUp();
        // Debug categories are initialized by GStreamerTestFixture
    }

    void TearDown() override { GStreamerTestFixture::TearDown(); }
};

/**
 * @brief Test link_tee_to_branch with valid elements.
 */
TEST_F(PipelineUtilsTest, LinkTeeToBranchValid) {
    // Ensure GStreamer is initialized
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    // Update registry to ensure plugins are available
    GstRegistry *registry = gst_registry_get();
    if (registry) {
        gst_update_registry();
    }

    // Create a pipeline
    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-pipeline"));
    if (!pipeline) {
        GTEST_SKIP()
                << "Failed to create pipeline - GStreamer may not be properly initialized or plugins not available";
    }

    // Create tee element
    auto tee = sf::unique_gst_element_ptr(gst_element_factory_make("tee", "test-tee"));
    if (!tee) {
        GTEST_SKIP() << "Failed to create tee element - GStreamer plugins may not be loaded";
    }

    // Create queue element (branch)
    auto queue = sf::unique_gst_element_ptr(gst_element_factory_make("queue", "test-queue"));
    if (!queue) {
        GTEST_SKIP() << "Failed to create queue element - GStreamer plugins may not be loaded";
    }

    // Add elements to pipeline (pipeline takes ownership)
    gst_bin_add_many(GST_BIN(pipeline.get()), tee.get(), queue.get(), nullptr);

    // Release ownership - pipeline now owns these elements
    tee.release();
    queue.release();

    // Link tee to branch (get elements from pipeline)
    GstElement *tee_in_pipeline = gst_bin_get_by_name(GST_BIN(pipeline.get()), "test-tee");
    GstElement *queue_in_pipeline = gst_bin_get_by_name(GST_BIN(pipeline.get()), "test-queue");
    ASSERT_NE(tee_in_pipeline, nullptr);
    ASSERT_NE(queue_in_pipeline, nullptr);

    const bool result = link_tee_to_branch(tee_in_pipeline, queue_in_pipeline);
    EXPECT_TRUE(result);

    // Unref the retrieved references (pipeline still owns them)
    gst_object_unref(tee_in_pipeline);
    gst_object_unref(queue_in_pipeline);
}

/**
 * @brief Test link_tee_to_branch with null tee element.
 */
TEST_F(PipelineUtilsTest, LinkTeeToBranchNullTee) {
    // Ensure GStreamer is initialized
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    const auto queue = sf::unique_gst_element_ptr(gst_element_factory_make("queue", "test-queue"));
    if (!queue) {
        GTEST_SKIP() << "Failed to create queue element - GStreamer plugins may not be loaded";
    }

    const bool result = link_tee_to_branch(nullptr, queue.get());
    EXPECT_FALSE(result);
}

/**
 * @brief Test link_tee_to_branch with null branch queue.
 */
TEST_F(PipelineUtilsTest, LinkTeeToBranchNullQueue) {
    // Ensure GStreamer is initialized
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    const auto tee = sf::unique_gst_element_ptr(gst_element_factory_make("tee", "test-tee"));
    if (!tee) {
        GTEST_SKIP() << "Failed to create tee element - GStreamer plugins may not be loaded";
    }

    const bool result = link_tee_to_branch(tee.get(), nullptr);
    EXPECT_FALSE(result);
}

/**
 * @brief Test link_tee_to_branch with elements not in pipeline.
 */
TEST_F(PipelineUtilsTest, LinkTeeToBranchElementsNotInPipeline) {
    // Ensure GStreamer is initialized
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    // Create elements but don't add to pipeline
    const auto tee = sf::unique_gst_element_ptr(gst_element_factory_make("tee", "test-tee"));
    if (!tee) {
        GTEST_SKIP() << "Failed to create tee element - GStreamer plugins may not be loaded";
    }

    const auto queue = sf::unique_gst_element_ptr(gst_element_factory_make("queue", "test-queue"));
    if (!queue) {
        GTEST_SKIP() << "Failed to create queue element - GStreamer plugins may not be loaded";
    }

    // Note: GStreamer allows linking elements even if they're not in a pipeline
    // The function will succeed, but the link won't be functional until elements are in a pipeline
    const bool result = link_tee_to_branch(tee.get(), queue.get());
    // The function can succeed even without a pipeline, but the link is only useful when in a pipeline
    EXPECT_TRUE(result);
}

/**
 * @brief Test link_tee_to_branch with multiple branches.
 */
TEST_F(PipelineUtilsTest, LinkTeeToBranchMultipleBranches) {
    // Ensure GStreamer is initialized
    ASSERT_TRUE(gst_is_initialized()) << "GStreamer must be initialized";

    const auto pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-pipeline"));
    if (!pipeline) {
        GTEST_SKIP()
                << "Failed to create pipeline - GStreamer may not be properly initialized or plugins not available";
    }

    auto tee = sf::unique_gst_element_ptr(gst_element_factory_make("tee", "test-tee"));
    if (!tee) {
        GTEST_SKIP() << "Failed to create tee element - GStreamer plugins may not be loaded";
    }

    auto queue1 = sf::unique_gst_element_ptr(gst_element_factory_make("queue", "test-queue-1"));
    if (!queue1) {
        GTEST_SKIP() << "Failed to create queue1 element - GStreamer plugins may not be loaded";
    }

    auto queue2 = sf::unique_gst_element_ptr(gst_element_factory_make("queue", "test-queue-2"));
    if (!queue2) {
        GTEST_SKIP() << "Failed to create queue2 element - GStreamer plugins may not be loaded";
    }

    // Add elements to pipeline (pipeline takes ownership)
    gst_bin_add_many(GST_BIN(pipeline.get()), tee.get(), queue1.get(), queue2.get(), nullptr);

    // Release ownership - pipeline now owns these elements
    tee.release();
    queue1.release();
    queue2.release();

    // Get elements from pipeline for linking
    GstElement *tee_in_pipeline = gst_bin_get_by_name(GST_BIN(pipeline.get()), "test-tee");
    GstElement *queue1_in_pipeline = gst_bin_get_by_name(GST_BIN(pipeline.get()), "test-queue-1");
    GstElement *queue2_in_pipeline = gst_bin_get_by_name(GST_BIN(pipeline.get()), "test-queue-2");
    ASSERT_NE(tee_in_pipeline, nullptr);
    ASSERT_NE(queue1_in_pipeline, nullptr);
    ASSERT_NE(queue2_in_pipeline, nullptr);

    // Link first branch
    const bool result1 = link_tee_to_branch(tee_in_pipeline, queue1_in_pipeline);
    EXPECT_TRUE(result1);

    // Link second branch
    const bool result2 = link_tee_to_branch(tee_in_pipeline, queue2_in_pipeline);
    EXPECT_TRUE(result2);

    // Unref the retrieved references (pipeline still owns them)
    gst_object_unref(tee_in_pipeline);
    gst_object_unref(queue1_in_pipeline);
    gst_object_unref(queue2_in_pipeline);
}
