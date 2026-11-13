// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>

#include "../libvcai/include/logging.h"
#include "application/application_runner.h"
#include "application/lifecycle_facade.h"
#include "cli_params.h"
#include "configuration_manager.h"
#include "debug_category_init.h"
#include "encoder_cli_debug.h"
#include "encoder_config.h"
#include "error.h"
#include "global_data.h"
#include "gst/gpu_elements.h"
#include "pipeline/startup.h"
#include "utils/gpu_monitor.h"
#include "utils/memory_monitor.h"

/**
 * @file main.cpp
 * @brief The main entry point for the encoder application.
 */

namespace {
    /**
     * @brief Log optional monitor initialization result.
     * @param monitor_name Human-readable monitor name.
     * @param is_enabled true when monitor initialization succeeded.
     * @param unavailable_reason Reason logged when initialization fails.
     */
    void log_optional_monitor_status(const char *monitor_name, const bool is_enabled, const char *unavailable_reason) {
        if (is_enabled) {
            sf::info(encoder_cli_main, "%s enabled", monitor_name);
            return;
        }

        sf::info(encoder_cli_main, "%s unavailable: %s", monitor_name, unavailable_reason);
    }

    /**
     * @brief Check required GPU elements with error handling and logging.
     * @return true when required GPU elements are available.
     */
    [[nodiscard]] bool verify_gpu_elements_available() {
        try {
            if (!check_gpu_elements_available()) {
                sf::error(encoder_cli_main,
                          "Required GPU elements are unavailable. Verify NVIDIA plugins and drivers.");
                return false;
            }
        } catch (const std::runtime_error &exception) {
            sf::error(encoder_cli_main, "GPU element check failed: %s", exception.what());
            return false;
        }

        return true;
    }

    /**
     * @brief Initialize optional runtime monitors and report their status.
     */
    void initialize_optional_monitors() {
        const auto gpu_monitoring_enabled = init_gpu_monitoring();
        log_optional_monitor_status("GPU monitoring", gpu_monitoring_enabled,
                                    "NVML not found or initialization failed");

        const auto memory_monitoring_enabled = init_memory_monitoring();
        log_optional_monitor_status("Memory monitoring", memory_monitoring_enabled, "initialization failed");
    }

    /**
     * @brief Load optional ROI data configured for the current run.
     * @param data Runtime state and pipeline data.
     * @return true when all configured ROI data loads successfully.
     */
    [[nodiscard]] bool load_roi_data(global_data &data) {
        if (!data.load_static_roi_data()) {
            sf::error(encoder_cli_main, "Failed to load static ROI data");
            return false;
        }

        if (!data.load_groundtruth_roi_data()) {
            sf::error(encoder_cli_main, "Failed to load groundtruth ROI data");
            return false;
        }

        return true;
    }

    /**
     * @brief Create all pipelines with runtime-error handling.
     * @param data Pipeline-runtime facade.
     * @return true when pipeline creation succeeds.
     */
    [[nodiscard]] bool create_pipelines(lifecycle_facade &data) {
        try {
            return create_all_pipelines(data);
        } catch (const std::runtime_error &exception) {
            sf::error(encoder_cli_main, "Pipeline creation failed: %s", exception.what());
            return false;
        }
    }

    /**
     * @brief Start all pipelines with exception handling.
     * @param data Pipeline-runtime facade.
     * @throws std::exception Re-throws startup failures after logging.
     */
    void start_pipelines(lifecycle_facade &data) {
        try {
            start_all_pipelines(data);
        } catch (const std::exception &exception) {
            sf::error(encoder_cli_main, "Exception caught in main() when calling start_all_pipelines: %s",
                      exception.what());
            throw;
        }
    }

    /**
     * @brief Shutdown runtime diagnostics systems.
     */
    void shutdown_diagnostics() {
        shutdown_gpu_monitoring();
        shutdown_memory_monitoring();
    }

    /**
     * @brief Run the encoder CLI application workflow.
     * @param argc Number of command line arguments.
     * @param argv Command line argument vector.
     * @return 0 on success, -1 on failure.
     */
    [[nodiscard]] int run_encoder_cli_application(int argc, char **argv) {
        gst_init(&argc, &argv);
        initialize_debug_categories();
        sf::info(encoder_cli_main, "encoder_cli startup started");

        if (!verify_gpu_elements_available()) {
            return -1;
        }

        initialize_optional_monitors();

        const auto cli_params = parse_command_line(argc, argv);
        auto config = load_and_configure_encoder(cli_params);
        auto data = global_data(std::move(config));

        if (!load_roi_data(data)) {
            return -1;
        }

        lifecycle_facade pipeline_runtime(data);

        if (!create_pipelines(pipeline_runtime)) {
            return -1;
        }

        sf::info(encoder_cli_main, "Pipelines created; configuring DeepStream elements");
        configure_deepstream_elements(pipeline_runtime);
        sf::info(encoder_cli_main, "DeepStream configuration complete; starting pipelines");

        start_pipelines(pipeline_runtime);

        sf::info(encoder_cli_main, "Pipelines started; entering runtime loop");
        run_application(data);

        sf::info(encoder_cli_main, "Shutdown initiated...");
        shutdown_diagnostics();
        sf::info(encoder_cli_main, "Application finished gracefully.");

        return 0;
    }

} // namespace

/**
 * @brief The main function of the encoder CLI application.
 * @param argc The number of command line arguments.
 * @param argv The command line arguments.
 * @return 0 on success, -1 on failure.
 */
int main(int argc, char **argv) {
    try {
        return run_encoder_cli_application(argc, argv);
    } catch (const std::runtime_error &exception) {
        sf::error(encoder_cli_main, "Caught a critical runtime error: %s", exception.what());
        return -1;
    }
}
