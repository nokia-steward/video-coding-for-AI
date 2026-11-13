// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

/**
 * @file test_input_analyzer_api.cpp
 * @brief Minimal unit tests for input_analyzer C API (create, destroy, init, is_valid).
 */

#include <gtest/gtest.h>

#include "../../encoder_cli/src/input_analyzer/input_analyzer.h"
#include "../common/gst_test_fixture.h"

class InputAnalyzerApiTest : public GStreamerTestFixture {};

TEST_F(InputAnalyzerApiTest, CreateReturnsNonNull) {
    auto analyzer = input_analyzer_create();
    ASSERT_NE(analyzer, nullptr);
}

TEST_F(InputAnalyzerApiTest, IsValidFalseBeforeInit) {
    auto analyzer = input_analyzer_create();
    ASSERT_NE(analyzer, nullptr);
    EXPECT_FALSE(input_analyzer_is_valid(analyzer.get()));
}

TEST_F(InputAnalyzerApiTest, DestroyNullDoesNotCrash) { input_analyzer_destroy(nullptr); }

TEST_F(InputAnalyzerApiTest, InitNullAnalyzerReturnsFalse) {
    EXPECT_FALSE(input_analyzer_init(nullptr, "/some/path.onnx"));
}

TEST_F(InputAnalyzerApiTest, InitEmptyPathReturnsFalse) {
    auto analyzer = input_analyzer_create();
    ASSERT_NE(analyzer, nullptr);
    EXPECT_FALSE(input_analyzer_init(analyzer.get(), ""));
    EXPECT_FALSE(input_analyzer_is_valid(analyzer.get()));
}

TEST_F(InputAnalyzerApiTest, InitNonexistentFileReturnsFalseAndInvalid) {
    auto analyzer = input_analyzer_create();
    ASSERT_NE(analyzer, nullptr);
    EXPECT_FALSE(input_analyzer_init(analyzer.get(), "/nonexistent/model.onnx"));
    EXPECT_FALSE(input_analyzer_is_valid(analyzer.get()));
}
