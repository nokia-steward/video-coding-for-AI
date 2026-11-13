// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../../encoder_enums.h"
#include "../../../processing/memory_contract_helper.h"
#include "../../gpu_elements.h"
#include "../bypass_path.h"
#include "../input_ingress_facade.h"
#include "../roi_input.h"
#include "nvds_yml_parser.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <gst/gst.h>
#include <iomanip>
#include <map>
#include <unistd.h>

#include "../../../../libvcai/include/gst_raii.h"
#include "../../../../libvcai/include/logging.h"

GST_DEBUG_CATEGORY_EXTERN(roi_input);

#define RETURN_ON_PARSER_ERROR(parse_expr)                                                                             \
    do {                                                                                                               \
        if (NVDS_YAML_PARSER_SUCCESS != (parse_expr)) {                                                                \
            sf::error(roi_input, "Error in parsing configuration file.");                                              \
            sf::error(roi_input, "This error may be caused by:");                                                      \
            sf::error(roi_input, "  1. CUDA driver not available (check if container is run with --nv flag)");         \
            sf::error(roi_input, "  2. WSL environment: GPU access requires Windows NVIDIA drivers and CUDA on WSL");  \
            sf::error(roi_input, "  3. Invalid configuration file format or missing required parameters");             \
            sf::error(roi_input, "  4. GPU not accessible in the container environment");                              \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

/**
 * @brief Creates DeepStream elements for ROI detection.
 * @param elements Output element container.
 * @param data Global data with configuration.
 * @return True if all required elements were created.
 */
bool create_deepstream_elements(deepstream_elements &elements, const encoder_config &config) {
    elements.streammux = gst_element_factory_make("nvstreammux", "stream-muxer");
    elements.nvvidconv = gst_element_factory_make("nvvideoconvert", "nvvideo-converter");
    elements.nvvidconv2 = gst_element_factory_make("nvvideoconvert", "nvvideo-converter2");
    elements.nvvidconv_pre_inference = gst_element_factory_make("nvvideoconvert", "nvvideo-converter-pre-inference");
    elements.capsfilter_inference = gst_element_factory_make("capsfilter", "capsfilter-inference");
    // RAII_EXEMPT: deepstream_elements stores non-owning raw handles used by gst_bin_add paths.
    elements.videoconvert_inference = create_gpu_video_convert("video-converter-inference").release();
    elements.appsink = gst_element_factory_make("appsink", "roi-detection-sink");

    // Only create nvinfer element if inference engine is enabled
    if (config.inference_engine) {
        elements.pgie = gst_element_factory_make("nvinfer", "primary-nvinference-engine");
        if (!elements.pgie) {
            sf::error(roi_input, "Failed to create nvinfer element for inference engine");
            return false;
        }

        // Create tracker only if enabled
        if (config.detection.enable_tracker) {
            elements.tracker = gst_element_factory_make("nvtracker", "nvtracker");
            if (!elements.tracker) {
                sf::error(roi_input, "Failed to create nvtracker element");
                return false;
            }
            sf::info(roi_input, "Tracker enabled - nvtracker element created");
        } else {
            sf::info(roi_input, "Tracker disabled - skipping nvtracker element creation (use "
                                "roi_accumulation_mode=overlap for ROI matching)");
        }
    } else {
        sf::info(roi_input, "Inference engine disabled, skipping nvinfer and nvtracker element creation");
    }

    // Create bypass path elements if bypass feature is enabled
    if (config.performance.enable_deepstream_bypass) {
        // Create valve for inference path
        elements.valve_inference = gst_element_factory_make("valve", "inference-valve");
        if (!elements.valve_inference) {
            sf::error(roi_input, "Failed to create inference valve element");
            return false;
        }

        // Create bypass path elements
        if (!create_bypass_path_elements(elements.bypass)) {
            sf::error(roi_input, "Failed to create bypass path elements");
            return false;
        }
    }

    return elements.streammux && elements.nvvidconv && elements.nvvidconv2 && elements.nvvidconv_pre_inference &&
           elements.capsfilter_inference && elements.videoconvert_inference && elements.appsink;
}

bool configure_deepstream_elements(input_ingress_facade &data, deepstream_elements &elements) {
    const std::string config_file = data.params.deepstream_config_file;
    if (config_file.empty()) {
        sf::info(roi_input, "DeepStream config file path is empty. Configuring nvstreammux with basic properties.");

        // Configure nvstreammux with basic required properties
        const int mux_width = data.params.width;
        const int mux_height = data.params.height;
        g_object_set(elements.streammux, "batch-size", 1, "batched-push-timeout", 40000, "width", mux_width, "height",
                     mux_height, nullptr);

        sf::info(roi_input, "Configured nvstreammux: batch-size=1, timeout=40000, resolution=%dx%d", mux_width,
                 mux_height);

        return true;
    }

    // Only configure streammux if we have a valid config file
    RETURN_ON_PARSER_ERROR(
            nvds_parse_streammux(elements.streammux, const_cast<char *>(config_file.c_str()), "streammux"));

    // Only configure nvinfer if inference engine is enabled
    if (elements.pgie) {
        sf::info(roi_input, "Configuring nvinfer with config file: %s", config_file.c_str());
        sf::info(roi_input, "Note: nvinfer requires CUDA driver access. Ensure container is run with --nv flag.");
        const auto parse_result = nvds_parse_gie(elements.pgie, const_cast<char *>(config_file.c_str()), "primary-gie");
        if (NVDS_YAML_PARSER_SUCCESS != parse_result) {
            sf::error(roi_input, "Failed to configure nvinfer element.");
            sf::error(roi_input, "Common causes:");
            sf::error(roi_input, "  - CUDA driver not available: Run container with 'apptainer exec --nv ...'");
            sf::error(roi_input, "  - WSL environment: GPU access in WSL requires:");
            sf::error(roi_input, "    * NVIDIA drivers installed on Windows (not Linux)");
            sf::error(roi_input, "    * CUDA on WSL properly configured");
            sf::error(roi_input, "    * Verify with 'nvidia-smi' in WSL (outside container)");
            sf::error(roi_input, "    * Apptainer may need additional flags: '--nv --writable-tmpfs'");
            sf::error(roi_input, "  - Invalid config file: Check %s for syntax errors", config_file.c_str());
            sf::error(roi_input, "  - GPU not accessible: Verify GPU access with 'nvidia-smi'");
            sf::error(roi_input, "  - Missing DeepStream libraries: Check LD_LIBRARY_PATH includes DeepStream libs");
            return false;
        }
        sf::info(roi_input, "Successfully configured nvinfer element");
    } else {
        sf::info(roi_input, "Skipping nvinfer configuration - inference engine disabled");
    }

    // Configure bypass path elements if bypass feature is enabled
    if (data.params.performance.enable_deepstream_bypass) {
        // Configure inference valve to pass frames by default.
        g_object_set(elements.valve_inference, "drop", FALSE, nullptr);
        sf::debug(roi_input, "Configured inference valve: drop=FALSE to pass frames");

        // Configure bypass path elements with the bypass valve closed by default.
        if (!configure_bypass_path_elements(elements.bypass, data.params)) {
            sf::error(roi_input, "Failed to configure bypass path elements");
            return false;
        }

        // Configure inference appsink for polling (disable signals)
        g_object_set(elements.appsink, "emit-signals", FALSE, "wait-on-eos", TRUE, nullptr);
        sf::debug(roi_input, "Configured inference appsink for polling: emit-signals=FALSE");

        // Store element references in global_data
        data.bypass.appsink_inference = elements.appsink;
        data.bypass.appsink_bypass = elements.bypass.appsink_bypass;
        data.bypass.valve_bypass = elements.bypass.valve_bypass;
        data.bypass.valve_inference = elements.valve_inference;
    }

    return true;
}

[[nodiscard]] bool generate_tracker_config_file(const std::string &base_config_path, const std::string &output_path,
                                                int max_shadow_tracking_age, bool enable_reassoc,
                                                int trajectory_projection_length, int max_tracklet_matching_time_range,
                                                int max_targets_per_stream, int probation_age,
                                                int early_termination_age, double min_confidence,
                                                bool use_prediction_for_assoc) {
    std::ifstream input_file(base_config_path);
    if (!input_file.is_open()) {
        sf::error(roi_input, "Failed to open base tracker config file: %s", base_config_path.c_str());
        return false;
    }

    std::ofstream output_file(output_path);
    if (!output_file.is_open()) {
        sf::error(roi_input, "Failed to create output tracker config file: %s", output_path.c_str());
        return false;
    }

    std::string line;
    bool in_base_config = false;
    bool in_target_management = false;
    bool in_trajectory_management = false;
    bool in_data_associator = false;
    bool modified_max_shadow = false;
    bool modified_reassoc = false;
    bool modified_projection_length = false;
    bool modified_tracklet_range = false;
    bool modified_max_targets = false;
    bool modified_probation_age = false;
    bool modified_early_termination = false;
    bool modified_min_confidence = false;
    bool modified_use_prediction = false;

    while (std::getline(input_file, line)) {
        // Check if we're entering BaseConfig section
        if (line.find("BaseConfig:") != std::string::npos) {
            in_base_config = true;
            in_target_management = false;
            in_trajectory_management = false;
            in_data_associator = false;
            output_file << line << "\n";
            continue;
        }

        // Check if we're entering TargetManagement section
        if (line.find("TargetManagement:") != std::string::npos) {
            in_base_config = false;
            in_target_management = true;
            in_trajectory_management = false;
            in_data_associator = false;
            output_file << line << "\n";
            continue;
        }

        // Check if we're entering TrajectoryManagement section
        if (line.find("TrajectoryManagement:") != std::string::npos) {
            in_base_config = false;
            in_target_management = false;
            in_trajectory_management = true;
            in_data_associator = false;
            output_file << line << "\n";
            continue;
        }

        // Check if we're entering DataAssociator section
        if (line.find("DataAssociator:") != std::string::npos) {
            in_base_config = false;
            in_target_management = false;
            in_trajectory_management = false;
            in_data_associator = true;
            output_file << line << "\n";
            continue;
        }

        // Check if we're leaving these sections
        if (!line.empty() && line[0] != ' ' && line[0] != '\t' && line.find(':') != std::string::npos &&
            line.find("BaseConfig:") == std::string::npos && line.find("TargetManagement:") == std::string::npos &&
            line.find("TrajectoryManagement:") == std::string::npos &&
            line.find("DataAssociator:") == std::string::npos) {
            in_base_config = false;
            in_target_management = false;
            in_trajectory_management = false;
            in_data_associator = false;
        }

        // Modify maxShadowTrackingAge in TargetManagement
        if (in_target_management && line.find("maxShadowTrackingAge:") != std::string::npos && !modified_max_shadow) {
            output_file << "  maxShadowTrackingAge: " << max_shadow_tracking_age << "\n";
            modified_max_shadow = true;
            continue;
        }

        // Modify enableReAssoc in TrajectoryManagement
        if (in_trajectory_management && line.find("enableReAssoc:") != std::string::npos && !modified_reassoc) {
            output_file << "  enableReAssoc: " << (enable_reassoc ? "1" : "0") << "\n";
            modified_reassoc = true;
            continue;
        }

        // Modify trajectoryProjectionLength in TrajectoryManagement
        if (in_trajectory_management && line.find("trajectoryProjectionLength:") != std::string::npos &&
            !modified_projection_length) {
            output_file << "  trajectoryProjectionLength: " << trajectory_projection_length << "\n";
            modified_projection_length = true;
            continue;
        }

        // Modify maxTrackletMatchingTimeSearchRange in TrajectoryManagement
        if (in_trajectory_management && line.find("maxTrackletMatchingTimeSearchRange:") != std::string::npos &&
            !modified_tracklet_range) {
            output_file << "  maxTrackletMatchingTimeSearchRange: " << max_tracklet_matching_time_range << "\n";
            modified_tracklet_range = true;
            continue;
        }

        // Modify maxTargetsPerStream in TargetManagement
        if (in_target_management && line.find("maxTargetsPerStream:") != std::string::npos && !modified_max_targets) {
            output_file << "  maxTargetsPerStream: " << max_targets_per_stream << "\n";
            modified_max_targets = true;
            continue;
        }

        // Modify probationAge in TargetManagement
        if (in_target_management && line.find("probationAge:") != std::string::npos && !modified_probation_age) {
            output_file << "  probationAge: " << probation_age << "\n";
            modified_probation_age = true;
            continue;
        }

        // Modify earlyTerminationAge in TargetManagement
        if (in_target_management && line.find("earlyTerminationAge:") != std::string::npos &&
            !modified_early_termination) {
            output_file << "  earlyTerminationAge: " << early_termination_age << "\n";
            modified_early_termination = true;
            continue;
        }

        // Modify minDetectorConfidence in BaseConfig
        if (in_base_config && line.find("minDetectorConfidence:") != std::string::npos && !modified_min_confidence) {
            output_file << " minDetectorConfidence: " << std::fixed << std::setprecision(4) << min_confidence << "\n";
            modified_min_confidence = true;
            continue;
        }

        // Modify usePrediction4Assoc in DataAssociator
        if (in_data_associator && line.find("usePrediction4Assoc:") != std::string::npos && !modified_use_prediction) {
            output_file << " usePrediction4Assoc: " << (use_prediction_for_assoc ? "1" : "0") << "\n";
            modified_use_prediction = true;
            continue;
        }

        // Write the line as-is
        output_file << line << "\n";
    }

    // If parameters weren't found, add them
    if (!modified_max_shadow || !modified_reassoc || !modified_projection_length || !modified_tracklet_range ||
        !modified_max_targets || !modified_probation_age || !modified_early_termination || !modified_min_confidence ||
        !modified_use_prediction) {
        sf::warning(roi_input, "Some tracker parameters were not found in base config, they may not be applied");
    }

    input_file.close();
    output_file.close();
    return true;
}

namespace {
    /**
     * @brief Determines tracker config filename based on tracker type.
     * @param tracker_type Tracker type string.
     * @return Config filename.
     */
    [[nodiscard]] std::string get_tracker_config_filename(const std::string &tracker_type) {
        if (tracker_type == "IOU") {
            return "config_tracker_IOU.yml";
        } else if (tracker_type == "NvSORT") {
            return "config_tracker_NvSORT.yml";
        } else if (tracker_type == "NvDCF") {
            return "config_tracker_NvDCF_perf.yml";
        } else if (tracker_type == "NvDeepSORT") {
            return "config_tracker_NvDeepSORT.yml";
        } else {
            // Default to NvSORT
            sf::warning(roi_input, "Unknown tracker type '%s', defaulting to NvSORT", tracker_type.c_str());
            return "config_tracker_NvSORT.yml";
        }
    }

    /**
     * @brief Determines tracker config path, using default if not provided.
     * @param config Encoder configuration.
     * @return Config path or empty string on error.
     */
    [[nodiscard]] std::string determine_tracker_config_path(const encoder_config &config) {
        if (!config.tracker.config_file.empty()) {
            return config.tracker.config_file;
        }

        const std::string deepstream_path = "/opt/nvidia/deepstream/deepstream";
        const std::string config_filename = get_tracker_config_filename(config.tracker.type);
        const std::string config_path = deepstream_path + "/samples/configs/deepstream-app/" + config_filename;

        if (std::filesystem::exists(config_path)) {
            return config_path;
        } else {
            sf::error(roi_input, "Could not find DeepStream tracker configuration file at %s", config_path.c_str());
            return "";
        }
    }

    /**
     * @brief Checks if custom tracker parameters are needed.
     * @param config Encoder configuration.
     * @return true if custom parameters are needed.
     */
    [[nodiscard]] bool needs_custom_tracker_params(const encoder_config &config) {
        // Check if any custom tracker parameters differ from defaults
        // Defaults: max_shadow=40, reassoc=true, projection=150, tracklet_range=25,
        //           max_targets=300, probation=5, early_term=1, min_conf=0.8049, use_pred=true
        return config.tracker.max_shadow_tracking_age != 40 || config.tracker.enable_reassoc != true ||
               config.tracker.trajectory_projection_length != 150 ||
               config.tracker.max_tracklet_matching_time_range != 25 || config.tracker.max_targets_per_stream != 300 ||
               config.tracker.probation_age != 5 || config.tracker.early_termination_age != 1 ||
               std::abs(config.tracker.min_confidence - 0.8049) > 0.0001 ||
               config.tracker.use_prediction_for_assoc != true;
    }

    /**
     * @brief Generates custom tracker config file if needed.
     * @param config Encoder configuration.
     * @param tracker_config_path Original config path.
     * @return Final config path (may be temporary) or empty string on error.
     */
    [[nodiscard]] std::string generate_custom_tracker_config_if_needed(const encoder_config &config,
                                                                       const std::string &tracker_config_path) {
        if (!needs_custom_tracker_params(config) || !config.tracker.config_file.empty()) {
            return tracker_config_path;
        }

        // Create a temporary config file with custom parameters
        const std::string temp_dir = std::filesystem::temp_directory_path().string();
        const std::string temp_config_path = temp_dir + "/tracker_config_" + std::to_string(getpid()) + ".yml";

        sf::info(roi_input,
                 "Generating tracker config with custom parameters: maxShadowTrackingAge=%d, enableReAssoc=%s, "
                 "trajectoryProjectionLength=%d, maxTrackletMatchingTimeRange=%d, maxTargetsPerStream=%d, "
                 "probationAge=%d, earlyTerminationAge=%d, minConfidence=%.4f, usePrediction4Assoc=%s",
                 config.tracker.max_shadow_tracking_age, config.tracker.enable_reassoc ? "true" : "false",
                 config.tracker.trajectory_projection_length, config.tracker.max_tracklet_matching_time_range,
                 config.tracker.max_targets_per_stream, config.tracker.probation_age,
                 config.tracker.early_termination_age, config.tracker.min_confidence,
                 config.tracker.use_prediction_for_assoc ? "true" : "false");

        if (!generate_tracker_config_file(tracker_config_path, temp_config_path, config.tracker.max_shadow_tracking_age,
                                          config.tracker.enable_reassoc, config.tracker.trajectory_projection_length,
                                          config.tracker.max_tracklet_matching_time_range,
                                          config.tracker.max_targets_per_stream, config.tracker.probation_age,
                                          config.tracker.early_termination_age, config.tracker.min_confidence,
                                          config.tracker.use_prediction_for_assoc)) {
            sf::error(roi_input, "Failed to generate tracker config file with custom parameters");
            return "";
        }

        sf::info(roi_input, "Generated temporary tracker config file: %s", temp_config_path.c_str());
        return temp_config_path;
    }

    /**
     * @brief Validates DeepStream tracker library path.
     * @return Library path or empty string if not found.
     */
    [[nodiscard]] std::string validate_tracker_library_path() {
        const std::string deepstream_path = "/opt/nvidia/deepstream/deepstream";
        const std::string deepstream_lib_path = deepstream_path + "/lib/libnvds_nvmultiobjecttracker.so";

        if (!std::filesystem::exists(deepstream_lib_path)) {
            sf::error(roi_input, "Could not find DeepStream tracker library at %s", deepstream_lib_path.c_str());
            return "";
        }

        return deepstream_lib_path;
    }

    /**
     * @brief Configures tracker element with parameters.
     * @param tracker Tracker element.
     * @param lib_path Library path.
     * @param config_path Config path.
     * @param width Tracker width.
     * @param height Tracker height.
     */
    void configure_tracker_element(GstElement *tracker, const std::string &lib_path, const std::string &config_path,
                                   int width, int height) {
        // Increase user-meta-pool-size to reduce buffer exhaustion at higher frame rates.
        // Default is 16; use 512 to handle larger metadata pressure without frequent reallocations.
        constexpr guint user_meta_pool_size = 512;
        g_object_set(G_OBJECT(tracker), "ll-lib-file", lib_path.c_str(), "ll-config-file", config_path.c_str(),
                     "tracker-width", width, "tracker-height", height, "gpu-id", 0, "display-tracking-id", TRUE,
                     "user-meta-pool-size", user_meta_pool_size, nullptr);
        sf::info(roi_input, "Configured nvtracker with user-meta-pool-size=%u to prevent buffer exhaustion",
                 user_meta_pool_size);
        sf::info(roi_input, "Tracker memory monitoring enabled - watch for unbounded growth");
    }

    /**
     * @brief Verifies tracker configuration.
     * @param tracker Tracker element.
     */
    void verify_tracker_configuration(GstElement *tracker) {
        gchar *ll_lib_file = nullptr;
        gchar *ll_config_file = nullptr;
        g_object_get(G_OBJECT(tracker), "ll-lib-file", &ll_lib_file, "ll-config-file", &ll_config_file, nullptr);
        const auto ll_lib_file_raii = sf::unique_g_char_ptr(ll_lib_file);
        const auto ll_config_file_raii = sf::unique_g_char_ptr(ll_config_file);
        sf::info(roi_input, "nvtracker configuration - Library: %s, Config: %s",
                 ll_lib_file_raii ? ll_lib_file_raii.get() : "nullptr",
                 ll_config_file_raii ? ll_config_file_raii.get() : "nullptr");
    }
} // namespace

[[nodiscard]] bool configure_tracker(const encoder_config &config, GstElement *tracker) {
    // Skip tracker configuration if tracker is disabled or not created
    if (!tracker) {
        if (config.inference_engine && !config.detection.enable_tracker) {
            sf::info(roi_input, "Skipping tracker configuration - tracker explicitly disabled");
        } else {
            sf::info(roi_input, "Skipping tracker configuration - inference engine disabled");
        }
        return true;
    }

    const std::string tracker_config_path = determine_tracker_config_path(config);
    if (tracker_config_path.empty()) {
        return false;
    }

    const std::string final_config_path = generate_custom_tracker_config_if_needed(config, tracker_config_path);
    if (final_config_path.empty()) {
        return false;
    }

    const std::string deepstream_lib_path = validate_tracker_library_path();
    if (deepstream_lib_path.empty()) {
        return false;
    }

    // Use common video dimensions
    const int tracker_width = config.width;
    const int tracker_height = config.height;

    sf::info(roi_input, "Configuring nvtracker: type=%s, config=%s, resolution=%dx%d", config.tracker.type.c_str(),
             final_config_path.c_str(), tracker_width, tracker_height);

    configure_tracker_element(tracker, deepstream_lib_path, final_config_path, tracker_width, tracker_height);
    verify_tracker_configuration(tracker);

    return true;
}

void configure_nvvideoconvert_elements(const deepstream_elements &elements, input_ingress_facade &data) {
    // Configure first nvvideoconvert element to use GPU compute instead of VIC
    g_object_set(G_OBJECT(elements.nvvidconv), "compute-hw", 1, nullptr);
    sf::info(roi_input, "Configured nvvideoconvert element to use GPU compute (compute-hw=1)");

    // Configure second nvvideoconvert element to use GPU compute instead of VIC
    g_object_set(G_OBJECT(elements.nvvidconv2), "compute-hw", 1, nullptr);
    sf::info(roi_input, "Configured nvvideoconvert2 element to use GPU compute (compute-hw=1)");

    // Configure pre-inference nvvideoconvert element to use GPU compute instead of VIC
    g_object_set(G_OBJECT(elements.nvvidconv_pre_inference), "compute-hw", 1, nullptr);
    sf::info(roi_input, "Configured nvvideoconvert-pre-inference element to use GPU compute (compute-hw=1)");

    // Configure capsfilter_inference to force system memory format.
    // ROI processing uses CPU-mapped frames, so we convert NVMM to system memory here.
    // The encoding pipeline uploads frames back to GPU memory before the encoder.
    if (elements.capsfilter_inference) {
        // Set caps to force system memory format (I420 is a common system memory format).
        const int target_width = data.params.width;
        const int target_height = data.params.height;
        if (target_width > 0 && target_height > 0) {
            sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", "width",
                                                             G_TYPE_INT, target_width, "height", G_TYPE_INT,
                                                             target_height, nullptr));
            if (caps) {
                g_object_set(G_OBJECT(elements.capsfilter_inference), "caps", caps.get(), nullptr);
                sf::info(roi_input, "Configured capsfilter_inference to force system memory (I420) at %dx%d",
                         target_width, target_height);
            }
        } else {
            sf::unique_gst_caps_ptr caps(gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "I420", nullptr));
            if (caps) {
                g_object_set(G_OBJECT(elements.capsfilter_inference), "caps", caps.get(), nullptr);
                sf::info(roi_input, "Configured capsfilter_inference to force system memory (I420)");
            }
        }
    }

    data.memory_runtime.roi_expected_domain.store(memory_domain_contract::system);
}
