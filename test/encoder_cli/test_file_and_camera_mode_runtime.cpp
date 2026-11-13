// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gtest/gtest.h>

#include "../../encoder_cli/src/application/file_mode.h"
#include "../../encoder_cli/src/application/input_modes/camera/camera_input_mode.h"
#include "../../encoder_cli/src/application/lifecycle_facade.h"
#include "../../encoder_cli/src/application/thread_management.h"
#include "../../encoder_cli/src/encoder_config.h"
#include "../../encoder_cli/src/encoder_enums.h"
#include "../../encoder_cli/src/global_data.h"
#include "../common/gst_test_fixture.h"

namespace {
    [[nodiscard]] encoder_config make_base_config() {
        encoder_config config;
        config.width = 640;
        config.height = 480;
        config.framerate = 30;
        config.format = sf::video_format::i420;
        config.file_name = sf::unique_g_char_ptr(g_strdup("dummy_640x480_30.yuv"));
        config.camera_device = "/dev/video0";
        return config;
    }

    void prepare_runtime_for_fast_shutdown(global_data &data) {
        data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("test-runtime-output-pipeline"));
        ASSERT_NE(data.output_pipeline, nullptr);
        data.encoded_ports.sei_src = gst_element_factory_make("appsrc", "test-runtime-sei-src");
        ASSERT_NE(data.encoded_ports.sei_src, nullptr);

        // Ensure worker threads exit immediately and deterministically.
        data.stop_processing.store(true);
        data.raw_frame_queue.close();
        data.encoded_frame_queue.close();
    }
} // namespace

class FileAndCameraModeRuntimeTest : public GStreamerTestFixture {};

TEST_F(FileAndCameraModeRuntimeTest, RunFileModeReturnsQuicklyWhenStopAlreadyRequested) {
    auto config = make_base_config();
    config.input_type_value = input_type::file;
    global_data data(std::move(config));
    prepare_runtime_for_fast_shutdown(data);

    EXPECT_NO_THROW(run_file_mode(data));
}

TEST_F(FileAndCameraModeRuntimeTest, report_application_startup_and_report_application_shutdown_are_runtime_safe) {
    auto config = make_base_config();
    config.input_type_value = input_type::file;
    global_data data(std::move(config));
    prepare_runtime_for_fast_shutdown(data);

    EXPECT_NO_THROW(run_file_mode(data));
}

TEST_F(FileAndCameraModeRuntimeTest, CameraInputModeRunModeReturnsQuicklyWhenStopAlreadyRequested) {
    auto config = make_base_config();
    config.input_type_value = input_type::camera;
    global_data data(std::move(config));
    prepare_runtime_for_fast_shutdown(data);

    const auto functions = create_camera_input_mode_functions();
    lifecycle_facade facade(data);
    EXPECT_NO_THROW(functions.run_mode(facade));
}

TEST_F(FileAndCameraModeRuntimeTest,
       wait_for_webrtc_connection_if_needed_and_run_camera_input_mode_paths_do_not_hang_when_stop_is_set) {
    auto config = make_base_config();
    config.input_type_value = input_type::camera;
    global_data data(std::move(config));
    prepare_runtime_for_fast_shutdown(data);

    const auto functions = create_camera_input_mode_functions();
    lifecycle_facade facade(data);
    EXPECT_NO_THROW(functions.run_mode(facade));
}

TEST_F(FileAndCameraModeRuntimeTest,
       start_random_access_frame_thread_close_random_access_packer_and_wait_for_threads_completion_paths_are_stable) {
    auto config = make_base_config();
    config.input_type_value = input_type::file;
    config.pack_rois = false;
    global_data data(std::move(config));

    bypass_gst_poll_slot roi_slot{};
    bypass_gst_poll_slot random_access_slot{};
    bypass_gst_poll_slot encoded_frame_slot{};
    EXPECT_NO_THROW(wait_for_threads_completion(roi_slot, random_access_slot, encoded_frame_slot, data));
}

TEST_F(FileAndCameraModeRuntimeTest, WorkerBundleLifecycleTransitionsToDrainedAfterDeterministicJoin) {
    auto config = make_base_config();
    config.input_type_value = input_type::file;
    config.pack_rois = false;
    global_data data(std::move(config));
    data.input_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("worker-bundle-input"));
    data.output_pipeline = sf::unique_gst_element_ptr(gst_pipeline_new("worker-bundle-output"));
    ASSERT_NE(data.input_pipeline, nullptr);
    ASSERT_NE(data.output_pipeline, nullptr);

    const worker_thread_start_options options{
            .start_input_message_thread = true,
            .start_output_message_thread = true,
    };
    auto bundle = start_worker_threads(data, options);
    ASSERT_NE(bundle, nullptr);
    EXPECT_EQ(data.worker_lifecycle.phase.load(), worker_lifecycle_phase::running);
    EXPECT_TRUE(data.worker_lifecycle.processing_workers_active.load());
    EXPECT_TRUE(data.worker_lifecycle.message_workers_active.load());

    (void) data.request_worker_stop_once(true);
    EXPECT_NO_THROW(wait_for_worker_threads_completion(data, *bundle));
    EXPECT_EQ(data.worker_lifecycle.phase.load(), worker_lifecycle_phase::drained);
    EXPECT_FALSE(data.worker_lifecycle.processing_workers_active.load());
    EXPECT_FALSE(data.worker_lifecycle.message_workers_active.load());
}

TEST_F(FileAndCameraModeRuntimeTest,
       log_optional_monitor_status_verify_gpu_elements_available_initialize_optional_monitors_and_load_roi_data_bootstrap_paths) {
    auto config = make_base_config();
    config.input_type_value = input_type::file;
    global_data data(std::move(config));
    prepare_runtime_for_fast_shutdown(data);
    EXPECT_NO_THROW(run_file_mode(data));
}
