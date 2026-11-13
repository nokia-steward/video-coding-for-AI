// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>
#include <gtest/gtest.h>

#include "../../encoder_cli/src/processing/memory_contract_helper.h"
#include "../common/gst_test_fixture.h"
#include "../libvcai/include/gst_raii.h"

class MemoryContractHelperTest : public GStreamerTestFixture {};

TEST_F(MemoryContractHelperTest, ClassifyCapsMemoryDomainHandlesSystemNvmmAndCuda) {
    const sf::unique_gst_caps_ptr system_caps(
            gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", nullptr));
    ASSERT_NE(system_caps, nullptr);
    EXPECT_EQ(classify_caps_memory_domain(system_caps.get()), memory_domain_contract::system);

    const sf::unique_gst_caps_ptr nvmm_caps(gst_caps_from_string("video/x-raw(memory:NVMM),format=NV12"));
    ASSERT_NE(nvmm_caps, nullptr);
    EXPECT_EQ(classify_caps_memory_domain(nvmm_caps.get()), memory_domain_contract::nvmm);

    const sf::unique_gst_caps_ptr cuda_caps(gst_caps_from_string("video/x-raw(memory:CUDAMemory),format=NV12"));
    ASSERT_NE(cuda_caps, nullptr);
    EXPECT_EQ(classify_caps_memory_domain(cuda_caps.get()), memory_domain_contract::cuda);
}

TEST_F(MemoryContractHelperTest, CreateSystemMemoryFallbackCopyIncrementsConversionCounter) {
    memory_policy_runtime runtime;
    const sf::unique_gst_buffer_ptr input_buffer(gst_buffer_new_allocate(nullptr, 128, nullptr));
    ASSERT_NE(input_buffer, nullptr);

    auto fallback_copy = create_system_memory_fallback_copy(input_buffer.get(), runtime, nullptr, "test-stage");
    ASSERT_NE(fallback_copy, nullptr);
    EXPECT_EQ(runtime.snapshot_counters().memory_domain_fallback_conversions, 1U);
    EXPECT_EQ(gst_buffer_get_size(fallback_copy.get()), gst_buffer_get_size(input_buffer.get()));
}

TEST_F(MemoryContractHelperTest, EnsureWritableProbeBufferAppliesFallbackAndTracksCounter) {
    memory_policy_runtime runtime;
    GstBuffer *original_buffer = gst_buffer_new_allocate(nullptr, 64, nullptr);
    ASSERT_NE(original_buffer, nullptr);
    GstBuffer *shared_reference = gst_buffer_ref(original_buffer);
    ASSERT_NE(shared_reference, nullptr);

    const auto result =
            ensure_writable_probe_buffer_with_fallback(nullptr, original_buffer, runtime, nullptr, "test-stage");
    EXPECT_EQ(result.status, memory_contract_status::fallback_applied);
    ASSERT_NE(result.buffer, nullptr);
    EXPECT_NE(result.buffer, shared_reference);
    EXPECT_EQ(runtime.snapshot_counters().writable_buffer_fallback_conversions, 1U);

    gst_buffer_unref(result.buffer);
    gst_buffer_unref(shared_reference);
}
