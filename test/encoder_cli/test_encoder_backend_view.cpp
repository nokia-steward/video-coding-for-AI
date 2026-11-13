// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>
#include <memory>
#include <optional>

#include "../../encoder_cli/src/application/lifecycle_facade.h"
#include "../../encoder_cli/src/encoder_backend/encoder_backend.h"
#include "../../encoder_cli/src/encoder_backend/encoder_backend_view.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/global_data.h"

/**
 * @file test_encoder_backend_view.cpp
 * @brief Unit tests for the encoder backend narrow view interface.
 *
 * Exercises the encoder_backend_view seam directly. Selection queries are tested
 * with a standalone backend (no aggregate); write no-ops are tested with a minimal
 * global_data, since the backend's write operations take it by design.
 */

TEST(EncoderBackendView, InactiveWhenNoBackend) {
    std::unique_ptr<encoder_backend> none;
    const encoder_backend_view view(none);
    EXPECT_FALSE(view.active());
    EXPECT_FALSE(view.requires_gst_encoding_pipeline());
}

TEST(EncoderBackendView, InactiveViewNoOpsWriteOperations) {
    encoder_config config;
    global_data data(std::move(config));

    std::unique_ptr<encoder_backend> none;
    encoder_backend_view view(none);
    EXPECT_TRUE(view.submit_frame(data, sf::unique_gst_buffer_ptr(nullptr), std::nullopt));
    view.signal_eos(data);
    SUCCEED();
}

TEST(EncoderBackendView, GstBackendExposesPipelineRequirement) {
    auto backend = create_gst_encoder_backend();
    ASSERT_NE(backend, nullptr);
    const encoder_backend_view view(backend);
    EXPECT_TRUE(view.active());
    EXPECT_TRUE(view.requires_gst_encoding_pipeline());
}

TEST(EncoderBackendView, ViewReflectsLiveBackendAfterConstruction) {
    // Mirrors the production ordering (main.cpp): the facade outlives the moment
    // the aggregate's backend is created, so the view must observe the backend
    // through the owner rather than caching a pointer snapshot at construction.
    encoder_config config;
    global_data data(std::move(config));
    lifecycle_facade runtime(data);
    EXPECT_FALSE(runtime.backend.active());

    data.active_encoder_backend = create_gst_encoder_backend();
    EXPECT_TRUE(runtime.backend.active());
    EXPECT_TRUE(runtime.backend.requires_gst_encoding_pipeline());
}
