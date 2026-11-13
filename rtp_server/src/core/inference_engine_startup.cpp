// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "inference_engine_startup.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <gst/gst.h>
#include "logging.h"

GST_DEBUG_CATEGORY_STATIC(inference_engine_startup_category);

namespace {
    [[nodiscard]] std::string trim_copy(const std::string &value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return "";
        }
        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    [[nodiscard]] std::string strip_quotes(std::string value) {
        if (value.size() >= 2) {
            const char first = value.front();
            const char last = value.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                value = value.substr(1, value.size() - 2);
            }
        }
        return value;
    }

    [[nodiscard]] std::string to_lower_copy(const std::string &value) {
        std::string result;
        result.reserve(value.size());
        for (const char ch: value) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        return result;
    }

    [[nodiscard]] std::optional<std::filesystem::path> extract_config_value(const std::filesystem::path &config_path,
                                                                            const std::string &key) {
        std::ifstream config_file(config_path);
        if (!config_file.is_open()) {
            return std::nullopt;
        }

        const std::string key_lower = to_lower_copy(key);
        std::string line;
        while (std::getline(config_file, line)) {
            const std::string line_lower = to_lower_copy(line);
            if (line_lower.find(key_lower) == std::string::npos) {
                continue;
            }

            size_t separator_pos = line.find('=');
            if (separator_pos == std::string::npos) {
                separator_pos = line.find(':');
            }
            if (separator_pos == std::string::npos) {
                continue;
            }

            std::string value = trim_copy(line.substr(separator_pos + 1));
            value = strip_quotes(value);
            if (value.empty()) {
                continue;
            }

            std::filesystem::path result(value);
            if (result.is_relative()) {
                result = config_path.parent_path() / result;
            }
            return result;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::filesystem::path>
    resolve_nvinfer_config_path(const std::filesystem::path &deepstream_config_path) {
        const auto config_path = extract_config_value(deepstream_config_path, "config-file-path");
        if (config_path) {
            return config_path;
        }
        return deepstream_config_path;
    }

    [[nodiscard]] std::optional<std::string> extract_config_string(const std::filesystem::path &config_path,
                                                                   const std::string &key) {
        std::ifstream config_file(config_path);
        if (!config_file.is_open()) {
            return std::nullopt;
        }

        const std::string key_lower = to_lower_copy(key);
        std::string line;
        while (std::getline(config_file, line)) {
            const std::string line_lower = to_lower_copy(line);
            if (line_lower.find(key_lower) == std::string::npos) {
                continue;
            }

            size_t separator_pos = line.find('=');
            if (separator_pos == std::string::npos) {
                separator_pos = line.find(':');
            }
            if (separator_pos == std::string::npos) {
                continue;
            }

            std::string value = trim_copy(line.substr(separator_pos + 1));
            value = strip_quotes(value);
            if (!value.empty()) {
                return value;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] int parse_non_negative_int_or_default(const std::optional<std::string> &value, const int fallback) {
        if (!value) {
            return fallback;
        }
        try {
            const int parsed = std::stoi(trim_copy(value.value()));
            if (parsed >= 0) {
                return parsed;
            }
        } catch (const std::exception &) {
        }
        return fallback;
    }

    [[nodiscard]] std::optional<std::filesystem::path>
    find_sibling_engine_candidate(const std::filesystem::path &model_source_path, const int batch_size,
                                  const int gpu_id, const std::filesystem::path &configured_engine_path) {
        const auto parent_dir = model_source_path.parent_path();
        if (parent_dir.empty() || !std::filesystem::exists(parent_dir) || !std::filesystem::is_directory(parent_dir)) {
            return std::nullopt;
        }

        const std::string prefix = model_source_path.filename().string() + "_b" + std::to_string(batch_size) + "_gpu" +
                                   std::to_string(gpu_id) + "_";
        std::optional<std::filesystem::path> best_candidate;
        std::filesystem::file_time_type best_write_time;
        bool has_write_time = false;

        for (const auto &entry: std::filesystem::directory_iterator(parent_dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const auto candidate_path = entry.path();
            if (candidate_path == configured_engine_path) {
                continue;
            }
            if (candidate_path.extension() != ".engine") {
                continue;
            }

            const auto candidate_name = candidate_path.filename().string();
            if (candidate_name.rfind(prefix, 0) != 0) {
                continue;
            }

            std::error_code time_ec;
            const auto write_time = std::filesystem::last_write_time(candidate_path, time_ec);
            if (time_ec) {
                if (!best_candidate) {
                    best_candidate = candidate_path;
                }
                continue;
            }

            if (!has_write_time || write_time > best_write_time) {
                best_candidate = candidate_path;
                best_write_time = write_time;
                has_write_time = true;
            }
        }

        return best_candidate;
    }

    [[nodiscard]] bool ensure_engine_alias(const std::filesystem::path &configured_engine_path,
                                           const std::filesystem::path &candidate_engine_path) {
        if (configured_engine_path == candidate_engine_path) {
            return true;
        }
        if (std::filesystem::exists(configured_engine_path)) {
            return true;
        }

        std::error_code ec;
        if (!configured_engine_path.parent_path().empty()) {
            std::filesystem::create_directories(configured_engine_path.parent_path(), ec);
            ec.clear();
        }

        std::filesystem::create_symlink(candidate_engine_path, configured_engine_path, ec);
        if (!ec) {
            return true;
        }
        ec.clear();

        std::filesystem::copy_file(candidate_engine_path, configured_engine_path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }

    [[nodiscard]] std::optional<std::filesystem::path>
    build_engine_with_nvinfer(const std::filesystem::path &nvinfer_config_path,
                              const std::filesystem::path &engine_path, const std::filesystem::path &model_source_path,
                              const int batch_size, const int gpu_id) {
        sf::info(inference_engine_startup_category, "Building TensorRT engine with nvinfer using config: %s",
                 nvinfer_config_path.c_str());

        GstElement *pipeline = gst_pipeline_new("engine-build-pipeline");
        GstElement *nvinfer = gst_element_factory_make("nvinfer", "primary-nvinference-engine");
        if (!pipeline || !nvinfer) {
            if (pipeline) {
                gst_object_unref(pipeline);
            }
            sf::error(inference_engine_startup_category, "Failed to create nvinfer pipeline elements.");
            return std::nullopt;
        }

        g_object_set(G_OBJECT(nvinfer), "config-file-path", nvinfer_config_path.c_str(), nullptr);
        gst_bin_add(GST_BIN(pipeline), nvinfer);

        const GstStateChangeReturn state_ret = gst_element_set_state(pipeline, GST_STATE_PAUSED);
        if (state_ret == GST_STATE_CHANGE_FAILURE) {
            sf::error(inference_engine_startup_category, "Failed to set nvinfer pipeline to PAUSED state.");
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            return std::nullopt;
        }

        constexpr int poll_seconds = 1;
        constexpr int max_wait_seconds = 900;
        int waited = 0;
        while (waited < max_wait_seconds) {
            if (std::filesystem::exists(engine_path)) {
                sf::info(inference_engine_startup_category, "TensorRT engine created: %s", engine_path.c_str());
                gst_element_set_state(pipeline, GST_STATE_NULL);
                gst_object_unref(pipeline);
                return engine_path;
            }

            const auto sibling_engine_opt =
                    find_sibling_engine_candidate(model_source_path, batch_size, gpu_id, engine_path);
            if (sibling_engine_opt) {
                sf::warning(inference_engine_startup_category,
                            "Configured engine path %s was not produced; found sibling engine candidate: %s",
                            engine_path.c_str(), sibling_engine_opt->c_str());
                gst_element_set_state(pipeline, GST_STATE_NULL);
                gst_object_unref(pipeline);
                return sibling_engine_opt.value();
            }
            std::this_thread::sleep_for(std::chrono::seconds(poll_seconds));
            waited += poll_seconds;
        }

        sf::error(inference_engine_startup_category, "Timed out waiting for TensorRT engine build (%d seconds).",
                  max_wait_seconds);
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return std::nullopt;
    }
} // namespace

void init_inference_engine_startup_debug_category() {
    GST_DEBUG_CATEGORY_INIT(inference_engine_startup_category, "inference-engine-startup", 0,
                            "TensorRT engine prebuild at startup");
}

bool ensure_inference_engine_ready(const rtp_server_config &config) {
    if (!config.inference_engine) {
        return true;
    }

    if (config.deepstream_config_file.empty()) {
        sf::error(inference_engine_startup_category,
                  "Inference engine enabled but deepstream_config_file is empty. Aborting startup.");
        return false;
    }

    const std::filesystem::path deepstream_config_path(config.deepstream_config_file);
    if (!std::filesystem::exists(deepstream_config_path)) {
        sf::error(inference_engine_startup_category, "DeepStream config file not found: %s",
                  deepstream_config_path.c_str());
        return false;
    }

    const auto nvinfer_config_path_opt = resolve_nvinfer_config_path(deepstream_config_path);
    if (!nvinfer_config_path_opt) {
        sf::error(inference_engine_startup_category, "Failed to resolve nvinfer config path from %s",
                  deepstream_config_path.c_str());
        return false;
    }

    const auto nvinfer_config_path = nvinfer_config_path_opt.value();
    if (!std::filesystem::exists(nvinfer_config_path)) {
        sf::error(inference_engine_startup_category, "Resolved nvinfer config file not found: %s",
                  nvinfer_config_path.c_str());
        return false;
    }

    const auto engine_path_opt = extract_config_value(nvinfer_config_path, "model-engine-file");
    const auto onnx_path_opt = extract_config_value(nvinfer_config_path, "onnx-file");
    const auto tlt_path_opt = extract_config_value(nvinfer_config_path, "tlt-encoded-model");
    const auto infer_dims_opt = extract_config_string(nvinfer_config_path, "infer-dims");
    const auto network_mode_opt = extract_config_string(nvinfer_config_path, "network-mode");
    const int batch_size =
            parse_non_negative_int_or_default(extract_config_string(nvinfer_config_path, "batch-size"), 1);
    const int gpu_id = parse_non_negative_int_or_default(extract_config_string(nvinfer_config_path, "gpu-id"), 0);

    if (!engine_path_opt) {
        sf::error(inference_engine_startup_category, "model-engine-file not found in %s", nvinfer_config_path.c_str());
        return false;
    }
    if (!onnx_path_opt && !tlt_path_opt) {
        sf::error(inference_engine_startup_category, "Neither onnx-file nor tlt-encoded-model found in %s",
                  nvinfer_config_path.c_str());
        return false;
    }

    const auto engine_path = engine_path_opt.value();
    std::filesystem::path model_source_path;
    const char *model_source_label = nullptr;
    if (onnx_path_opt) {
        model_source_path = onnx_path_opt.value();
        model_source_label = "ONNX";
    } else {
        model_source_path = tlt_path_opt.value();
        model_source_label = "TLT encoded";
    }

    if (std::filesystem::exists(engine_path)) {
        sf::info(inference_engine_startup_category, "TensorRT engine already exists: %s", engine_path.c_str());
        return true;
    }

    const auto sibling_engine_opt = find_sibling_engine_candidate(model_source_path, batch_size, gpu_id, engine_path);
    if (sibling_engine_opt) {
        sf::warning(inference_engine_startup_category,
                    "Configured engine file is missing (%s), but found sibling engine candidate: %s",
                    engine_path.c_str(), sibling_engine_opt->c_str());
        if (ensure_engine_alias(engine_path, sibling_engine_opt.value())) {
            sf::info(inference_engine_startup_category, "Created configured-engine alias: %s -> %s",
                     engine_path.c_str(), sibling_engine_opt->c_str());
        } else {
            sf::warning(inference_engine_startup_category,
                        "Failed to create configured-engine alias; continuing with sibling engine candidate only.");
        }
        return true;
    }

    if (!std::filesystem::exists(model_source_path)) {
        sf::error(inference_engine_startup_category, "%s model file missing: %s", model_source_label,
                  model_source_path.c_str());
        return false;
    }

    if (!infer_dims_opt) {
        sf::warning(inference_engine_startup_category, "infer-dims not found in %s", nvinfer_config_path.c_str());
    }

    if (network_mode_opt && trim_copy(network_mode_opt.value()) == "2") {
        sf::warning(inference_engine_startup_category,
                    "INT8 network-mode requested; engine build may require calibration data.");
    }

    sf::info(inference_engine_startup_category,
             "TensorRT engine missing. Building before accepting connections (engine: %s).", engine_path.c_str());
    const auto built_engine_opt =
            build_engine_with_nvinfer(nvinfer_config_path, engine_path, model_source_path, batch_size, gpu_id);
    if (!built_engine_opt) {
        return false;
    }

    const auto built_engine_path = built_engine_opt.value();
    if (built_engine_path != engine_path) {
        sf::warning(inference_engine_startup_category,
                    "Engine build completed with alternative filename. configured=%s actual=%s", engine_path.c_str(),
                    built_engine_path.c_str());
        if (ensure_engine_alias(engine_path, built_engine_path)) {
            sf::info(inference_engine_startup_category, "Created configured-engine alias: %s -> %s",
                     engine_path.c_str(), built_engine_path.c_str());
        } else {
            sf::warning(
                    inference_engine_startup_category,
                    "Failed to create configured-engine alias; runtime may rebuild if strict filename is required.");
        }
    }

    sf::info(inference_engine_startup_category, "TensorRT engine successfully created: %s", built_engine_path.c_str());
    return true;
}
