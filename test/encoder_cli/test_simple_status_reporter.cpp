// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "../../encoder_cli/src/application/app_periodic_gst_tasks.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../../encoder_cli/src/gst/input_types/input_ingress_facade.h"
#include "../../encoder_cli/src/gst/input_types/udp_input.h"
#include "../../encoder_cli/src/reporting/simple_status_reporter.h"
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
        config.enable_status_reporter = false;
        return config;
    }
} // namespace

class SimpleStatusReporterTest : public GStreamerTestFixture {};

TEST_F(SimpleStatusReporterTest, UpdateMethodsAndReportGenerationAreStable) {
    auto config = make_base_config();
    global_data data(std::move(config));
    simple_status_reporter reporter(data.params, data.bypass, data.memory_runtime, data.metadata_runtime,
                                    data.monitoring.fps_monitor, data.stop_processing, data.udp_input_runtime,
                                    data.udp_frame_drop_manager, data.last_applied_luma_downscale,
                                    data.last_applied_luma_upscale);

    reporter.update_frame_processing();
    reporter.update_encoding();
    reporter.update_pushed_to_encoder();
    reporter.update_output();
    reporter.update_output_processed();
    reporter.update_output_display();
    reporter.update_output_h265();
    reporter.update_output_mp4();
    reporter.update_output_debug();
    reporter.update_output_rtp();
    reporter.update_udp_decoded();
    reporter.update_roi_detection(3, 2);
    reporter.update_roi_filtering(1);
    reporter.update_sei_injection(2);
    reporter.report_error();

    EXPECT_NO_THROW(reporter.report_event("unit-test-event"));
    EXPECT_NO_THROW(reporter.report_pipeline_status("output", "PLAYING"));
    EXPECT_NO_THROW(reporter.generate_status_report());
}

TEST_F(SimpleStatusReporterTest, StartAndStopPeriodicReportingLifecycleIsSafe) {
    auto config = make_base_config();
    global_data data(std::move(config));
    simple_status_reporter reporter(data.params, data.bypass, data.memory_runtime, data.metadata_runtime,
                                    data.monitoring.fps_monitor, data.stop_processing, data.udp_input_runtime,
                                    data.udp_frame_drop_manager, data.last_applied_luma_downscale,
                                    data.last_applied_luma_upscale);

    reporter.start_periodic_reporting(1);
    ASSERT_TRUE(start_app_periodic_reporting_gst_task(data));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_NO_THROW(reporter.stop_periodic_reporting());
    join_app_periodic_reporting_gst_task(data);
    EXPECT_NO_THROW(reporter.stop_periodic_reporting());
}

TEST_F(SimpleStatusReporterTest, ReportContainsAdaptiveLumaOnWhenEnabled) {
    auto config = make_base_config();
    config.enable_luma_adaptation = true;
    config.enable_adaptive_luma_adaptation = true;
    global_data data(std::move(config));
    data.last_applied_luma_downscale.store(0.5);
    data.last_applied_luma_upscale.store(1.2);
    simple_status_reporter reporter(data.params, data.bypass, data.memory_runtime, data.metadata_runtime,
                                    data.monitoring.fps_monitor, data.stop_processing, data.udp_input_runtime,
                                    data.udp_frame_drop_manager, data.last_applied_luma_downscale,
                                    data.last_applied_luma_upscale);

    const std::string report = reporter.build_status_report_string();

    EXPECT_TRUE(report.find("Adaptive luma: on") != std::string::npos);
}

TEST_F(SimpleStatusReporterTest, ReportContainsLumaAppliedWhenLumaAdaptationEnabled) {
    auto config = make_base_config();
    config.enable_luma_adaptation = true;
    global_data data(std::move(config));
    data.last_applied_luma_downscale.store(0.30);
    data.last_applied_luma_upscale.store(2.40);
    simple_status_reporter reporter(data.params, data.bypass, data.memory_runtime, data.metadata_runtime,
                                    data.monitoring.fps_monitor, data.stop_processing, data.udp_input_runtime,
                                    data.udp_frame_drop_manager, data.last_applied_luma_downscale,
                                    data.last_applied_luma_upscale);

    const std::string report = reporter.build_status_report_string();

    EXPECT_TRUE(report.find("Luma (d,u) applied: 0.30, 2.40") != std::string::npos);
}

TEST_F(SimpleStatusReporterTest, ReportContainsMemoryFallbackCounters) {
    auto config = make_base_config();
    global_data data(std::move(config));
    data.memory_runtime.record_memory_domain_fallback_conversion();
    data.memory_runtime.record_writable_buffer_fallback_conversion();
    data.memory_runtime.record_writable_buffer_fallback_conversion();
    data.memory_runtime.record_fallback_failure();
    data.metadata_runtime.record_bundle_enqueued();
    data.metadata_runtime.record_bundle_enqueued();
    data.metadata_runtime.record_bundle_dequeued();
    data.metadata_runtime.record_validation_failure();
    simple_status_reporter reporter(data.params, data.bypass, data.memory_runtime, data.metadata_runtime,
                                    data.monitoring.fps_monitor, data.stop_processing, data.udp_input_runtime,
                                    data.udp_frame_drop_manager, data.last_applied_luma_downscale,
                                    data.last_applied_luma_upscale);

    const std::string report = reporter.build_status_report_string();
    EXPECT_TRUE(report.find("Memory fallback (domain,writable,fail): 1, 2, 1") != std::string::npos);
    EXPECT_TRUE(report.find("Metadata contract (enq,deq,mismatch,invalid,fail-stop): 2, 1, 0, 1, 0") !=
                std::string::npos);
}

TEST_F(SimpleStatusReporterTest, UdpModeStartReturnsAfterStopRequestedWithoutClient) {
    auto config = make_base_config();
    config.input_type_value = input_type::udp;
    global_data data(std::move(config));
    simple_status_reporter reporter(data.params, data.bypass, data.memory_runtime, data.metadata_runtime,
                                    data.monitoring.fps_monitor, data.stop_processing, data.udp_input_runtime,
                                    data.udp_frame_drop_manager, data.last_applied_luma_downscale,
                                    data.last_applied_luma_upscale);
    input_ingress_facade ingress(data);
    reset_udp_connection_state(ingress);

    std::atomic_bool start_returned{false};
    std::jthread start_thread([&]() {
        reporter.start_periodic_reporting(1);
        start_returned.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    data.stop_processing = true;

    const auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!start_returned.load() && std::chrono::steady_clock::now() < wait_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_TRUE(start_returned.load());
    reporter.stop_periodic_reporting();
}
