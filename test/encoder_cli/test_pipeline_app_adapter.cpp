// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/bridge/app/pipeline_app_adapter.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.input_type_value = input_type::file;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
        config.format = sf::video_format::i420;
        return config;
    }
} // namespace

class PipelineAppAdapterTest : public GStreamerTestFixture {};

TEST_F(PipelineAppAdapterTest, RecordsRoiAndEncodingCountersViaStatusReporter) {
    auto config = make_base_config();
    config.enable_status_reporter = true;
    global_data data(std::move(config));
    ASSERT_NE(data.monitoring.status_reporter, nullptr);

    const pipeline_app_adapter adapter(data);
    adapter.record_roi_detection_update(3U, 2U, "inference");
    adapter.record_roi_filtering_update(1);
    adapter.record_encoded_frame_update();

    const std::string report = data.monitoring.status_reporter->build_status_report_string();
    EXPECT_NE(report.find("Processed: 1"), std::string::npos);
    EXPECT_NE(report.find("Encoded: 1"), std::string::npos);
    EXPECT_NE(report.find("Detected: 3"), std::string::npos);
    EXPECT_NE(report.find("Tracked: 2"), std::string::npos);
    EXPECT_NE(report.find("Filtered: 1"), std::string::npos);
}

TEST_F(PipelineAppAdapterTest, FailFastStopRequestsCloseQueuesAndStopProcessing) {
    auto config = make_base_config();
    global_data data(std::move(config));
    const pipeline_app_adapter adapter(data);

    adapter.request_fail_fast_stop("unit-test fail-fast");

    EXPECT_TRUE(data.stop_processing.load());
    EXPECT_TRUE(data.ingress_runtime.raw_queue_closed.load());
    EXPECT_TRUE(data.ingress_runtime.encoded_queue_closed.load());
    EXPECT_TRUE(data.egress_bridge_runtime.metadata_queues_closed.load());
}

TEST_F(PipelineAppAdapterTest, MetadataFailStopIncrementsContractCounter) {
    auto config = make_base_config();
    global_data data(std::move(config));
    const pipeline_app_adapter adapter(data);

    const auto before = data.metadata_runtime.snapshot_counters();
    adapter.request_metadata_contract_fail_stop("unit-test metadata fail-stop");
    const auto after = data.metadata_runtime.snapshot_counters();

    EXPECT_EQ(after.contract_fail_stops, before.contract_fail_stops + 1U);
    EXPECT_TRUE(data.stop_processing.load());
}

TEST_F(PipelineAppAdapterTest, FileInputForceCloseDecisionAndOneShotStateAreDeterministic) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.expected_file_input_frames = 5U;
    const pipeline_app_adapter adapter(data);

    EXPECT_FALSE(adapter.should_force_close_file_raw_queue(4U));
    EXPECT_TRUE(adapter.should_force_close_file_raw_queue(5U));
    EXPECT_TRUE(adapter.mark_file_input_forced_raw_eos_once());
    EXPECT_FALSE(adapter.mark_file_input_forced_raw_eos_once());
}
