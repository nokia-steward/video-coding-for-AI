// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "input_analyzer.h"

#include "../global_data.h"
#include "../processing/frame_processing_utils.h"
#include "libvcai/include/logging.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string>

#if defined(HAVE_ONNXRUNTIME)
#include <onnxruntime_cxx_api.h>
#endif

namespace {

    GST_DEBUG_CATEGORY_STATIC(input_analyzer_cat);

    void init_input_analyzer_debug_category() {
        GST_DEBUG_CATEGORY_INIT(input_analyzer_cat, "input-analyzer", 0, "Adaptive luma input analyzer");
    }

    constexpr int k_analyzer_input_size = 512;
    constexpr float k_mean_r = 0.451f;
    constexpr float k_mean_g = 0.421f;
    constexpr float k_mean_b = 0.391f;
    constexpr float k_std_r = 0.293f;
    constexpr float k_std_g = 0.286f;
    constexpr float k_std_b = 0.292f;

    /** QP scaler: map [22, 47] to [0, 1] as in the Python example. */
    constexpr double transform_qp(const int qp) { return (qp - 22) / 25.0; }

    /** Compute upscale multiplier from backscaling_ratio and downscale_factor: upscale_multiplier = ((red_value -
     * red_value*downscale_factor)/downscale_factor) + 1. */
    constexpr double backscaling_ratio_to_upscale(const double backscaling_ratio, const double downscale_factor) {
        if (downscale_factor <= 1e-9) {
            return 1.0;
        }
        return ((backscaling_ratio - backscaling_ratio * downscale_factor) / downscale_factor) + 1.0;
    }

} // namespace

struct input_analyzer {
#if defined(HAVE_ONNXRUNTIME)
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "encoder_cli_input_analyzer"};
    Ort::SessionOptions session_options;
    std::unique_ptr<Ort::Session> session;
    std::vector<float> input_image_buffer;
    std::vector<float> input_size_buffer;

    input_analyzer() :
        env(ORT_LOGGING_LEVEL_WARNING, "encoder_cli_input_analyzer"), session_options(), session(),
        input_image_buffer(), input_size_buffer(), valid(false) {}
#else
    input_analyzer() : valid(false) {}
#endif
    bool valid{false};
};

input_analyzer_ptr input_analyzer_create() { return input_analyzer_ptr(new input_analyzer{}, input_analyzer_destroy); }

bool input_analyzer_init(input_analyzer *analyzer, const std::string &model_path) {
    if (analyzer == nullptr || model_path.empty()) {
        return false;
    }
    init_input_analyzer_debug_category();

#if defined(HAVE_ONNXRUNTIME)
    try {
        analyzer->session_options.SetIntraOpNumThreads(1);
        analyzer->session =
                std::make_unique<Ort::Session>(analyzer->env, model_path.c_str(), analyzer->session_options);
        analyzer->input_image_buffer.resize(1 * 4 * k_analyzer_input_size * k_analyzer_input_size);
        analyzer->input_size_buffer.resize(1 * 2);
        analyzer->valid = true;
        sf::info(input_analyzer_cat, "Input analyzer loaded: %s", model_path.c_str());
        return true;
    } catch (const std::exception &e) {
        sf::error(input_analyzer_cat, "Input analyzer failed to load %s: %s", model_path.c_str(), e.what());
        return false;
    }
#else
    (void) model_path;
    sf::warning(input_analyzer_cat,
                "Input analyzer not built with ONNX Runtime; enable_adaptive_luma_adaptation will fail at startup");
    return false;
#endif
}

void input_analyzer_destroy(input_analyzer *analyzer) {
    if (analyzer == nullptr) {
        return;
    }
    std::unique_ptr<input_analyzer> owned_analyzer(analyzer);
#if defined(HAVE_ONNXRUNTIME)
    owned_analyzer->session.reset();
    owned_analyzer->input_image_buffer.clear();
    owned_analyzer->input_size_buffer.clear();
#endif
    owned_analyzer->valid = false;
}

bool input_analyzer_built_with_onnx_runtime() {
#if defined(HAVE_ONNXRUNTIME)
    return true;
#else
    return false;
#endif
}

bool input_analyzer_is_valid(const input_analyzer *analyzer) { return analyzer != nullptr && analyzer->valid; }

#if defined(HAVE_ONNXRUNTIME)
namespace {

    void yuv_i420_to_rgb_512(const std::uint8_t *y_plane, const std::uint8_t *u_plane, const std::uint8_t *v_plane,
                             int src_width, int src_height, int luma_stride, int uv_stride,
                             std::vector<float> *out_rgb) {
        out_rgb->resize(3 * k_analyzer_input_size * k_analyzer_input_size);
        for (int dy = 0; dy < k_analyzer_input_size; ++dy) {
            const float sy = (static_cast<float>(dy) + 0.5f) * static_cast<float>(src_height) /
                             static_cast<float>(k_analyzer_input_size);
            const int iy = std::min(static_cast<int>(sy), src_height - 1);
            for (int dx = 0; dx < k_analyzer_input_size; ++dx) {
                const float sx = (static_cast<float>(dx) + 0.5f) * static_cast<float>(src_width) /
                                 static_cast<float>(k_analyzer_input_size);
                const int ix = std::min(static_cast<int>(sx), src_width - 1);
                const int luma_index = iy * luma_stride + ix;
                const int uv_idx = (iy / 2) * uv_stride + (ix / 2) * 2;
                const float luma_value = (static_cast<float>(y_plane[luma_index]) - 16.0f) / 219.0f;
                const float chroma_u_value = (static_cast<float>(u_plane[uv_idx]) - 128.0f) / 224.0f;
                const float chroma_v_value = (static_cast<float>(v_plane[uv_idx + 1]) - 128.0f) / 224.0f;
                float red_value = luma_value + 1.402f * chroma_v_value;
                float green_value = luma_value - 0.344f * chroma_u_value - 0.714f * chroma_v_value;
                float blue_value = luma_value + 1.772f * chroma_u_value;
                red_value = std::clamp(red_value, 0.0f, 1.0f);
                green_value = std::clamp(green_value, 0.0f, 1.0f);
                blue_value = std::clamp(blue_value, 0.0f, 1.0f);
                const std::size_t out_idx = static_cast<std::size_t>((dy * k_analyzer_input_size + dx) * 3);
                (*out_rgb)[out_idx + 0] = (red_value - k_mean_r) / k_std_r;
                (*out_rgb)[out_idx + 1] = (green_value - k_mean_g) / k_std_g;
                (*out_rgb)[out_idx + 2] = (blue_value - k_mean_b) / k_std_b;
            }
        }
    }

    void yuv_nv12_to_rgb_512(const std::uint8_t *y_plane, const std::uint8_t *uv_plane, int src_width, int src_height,
                             int luma_stride, int uv_stride, std::vector<float> *out_rgb) {
        out_rgb->resize(3 * k_analyzer_input_size * k_analyzer_input_size);
        for (int dy = 0; dy < k_analyzer_input_size; ++dy) {
            const float sy = (static_cast<float>(dy) + 0.5f) * static_cast<float>(src_height) /
                             static_cast<float>(k_analyzer_input_size);
            const int iy = std::min(static_cast<int>(sy), src_height - 1);
            for (int dx = 0; dx < k_analyzer_input_size; ++dx) {
                const float sx = (static_cast<float>(dx) + 0.5f) * static_cast<float>(src_width) /
                                 static_cast<float>(k_analyzer_input_size);
                const int ix = std::min(static_cast<int>(sx), src_width - 1);
                const int luma_index = iy * luma_stride + ix;
                const int uv_idx = (iy / 2) * uv_stride + (ix / 2) * 2;
                const float luma_value = (static_cast<float>(y_plane[luma_index]) - 16.0f) / 219.0f;
                const float chroma_u_value = (static_cast<float>(uv_plane[uv_idx]) - 128.0f) / 224.0f;
                const float chroma_v_value = (static_cast<float>(uv_plane[uv_idx + 1]) - 128.0f) / 224.0f;
                float red_value = luma_value + 1.402f * chroma_v_value;
                float green_value = luma_value - 0.344f * chroma_u_value - 0.714f * chroma_v_value;
                float blue_value = luma_value + 1.772f * chroma_u_value;
                red_value = std::clamp(red_value, 0.0f, 1.0f);
                green_value = std::clamp(green_value, 0.0f, 1.0f);
                blue_value = std::clamp(blue_value, 0.0f, 1.0f);
                const std::size_t out_idx = static_cast<std::size_t>((dy * k_analyzer_input_size + dx) * 3);
                (*out_rgb)[out_idx + 0] = (red_value - k_mean_r) / k_std_r;
                (*out_rgb)[out_idx + 1] = (green_value - k_mean_g) / k_std_g;
                (*out_rgb)[out_idx + 2] = (blue_value - k_mean_b) / k_std_b;
            }
        }
    }

} // namespace
#endif

std::optional<std::pair<double, double>> input_analyzer_analyze(input_analyzer *analyzer, GstBuffer *buffer,
                                                                const frame_properties &props, int qp) {
    if (analyzer == nullptr || !analyzer->valid || buffer == nullptr || props.width <= 0 || props.height <= 0) {
        return std::nullopt;
    }

#if defined(HAVE_ONNXRUNTIME)
    GstMapInfo map_info;
    if (!gst_buffer_map(buffer, &map_info, GST_MAP_READ)) {
        sf::warning(input_analyzer_cat, "Failed to map buffer for input analyzer");
        return std::nullopt;
    }

    std::vector<float> rgb_512;
    const bool is_i420 = (props.format != nullptr && std::strcmp(props.format, "I420") == 0);
    const bool is_nv12 = (props.format != nullptr && std::strcmp(props.format, "NV12") == 0);

    if (is_i420) {
        const int luma_stride = props.width;
        const int uv_stride = props.width / 2;
        const size_t luma_plane_size = static_cast<size_t>(luma_stride * props.height);
        const size_t uv_size = static_cast<size_t>(uv_stride * (props.height / 2));
        if (map_info.size < luma_plane_size + uv_size * 2) {
            gst_buffer_unmap(buffer, &map_info);
            return std::nullopt;
        }
        yuv_i420_to_rgb_512(map_info.data, map_info.data + luma_plane_size, map_info.data + luma_plane_size + uv_size,
                            props.width, props.height, luma_stride, uv_stride, &rgb_512);
    } else if (is_nv12) {
        const int luma_stride = props.width;
        const int uv_stride = props.width;
        const size_t luma_plane_size = static_cast<size_t>(luma_stride * props.height);
        if (map_info.size < luma_plane_size + static_cast<size_t>(uv_stride * (props.height / 2))) {
            gst_buffer_unmap(buffer, &map_info);
            return std::nullopt;
        }
        yuv_nv12_to_rgb_512(map_info.data, map_info.data + luma_plane_size, props.width, props.height, luma_stride,
                            uv_stride, &rgb_512);
    } else {
        gst_buffer_unmap(buffer, &map_info);
        sf::warning(input_analyzer_cat, "Input analyzer supports only I420 and NV12, got %s",
                    props.format != nullptr ? props.format : "null");
        return std::nullopt;
    }
    gst_buffer_unmap(buffer, &map_info);

    const double qp_scaled = transform_qp(std::clamp(qp, 22, 47));
    float *const img_buf = analyzer->input_image_buffer.data();
    const int num_pixels = k_analyzer_input_size * k_analyzer_input_size;
    for (int index = 0; index < num_pixels; ++index) {
        const std::size_t rgb_base = static_cast<std::size_t>(index) * 3u;
        img_buf[index * 4 + 0] = rgb_512[rgb_base + 0];
        img_buf[index * 4 + 1] = rgb_512[rgb_base + 1];
        img_buf[index * 4 + 2] = rgb_512[rgb_base + 2];
        img_buf[index * 4 + 3] = static_cast<float>(qp_scaled);
    }
    analyzer->input_size_buffer[0] = static_cast<float>(props.width) / 1024.0f;
    analyzer->input_size_buffer[1] = static_cast<float>(props.height) / 1024.0f;

    try {
        Ort::AllocatorWithDefaultOptions allocator;
        const std::vector<int64_t> image_shape = {1, 4, k_analyzer_input_size, k_analyzer_input_size};
        const std::vector<int64_t> size_shape = {1, 2};
        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value image_tensor = Ort::Value::CreateTensor<float>(
                memory_info, img_buf, analyzer->input_image_buffer.size(), image_shape.data(), image_shape.size());
        Ort::Value size_tensor = Ort::Value::CreateTensor<float>(memory_info, analyzer->input_size_buffer.data(), 2,
                                                                 size_shape.data(), size_shape.size());

        auto *session_ptr = analyzer->session.get();
        Ort::Session &session = *session_ptr;
        std::vector<const char *> input_names = {"image", "size"};
        std::vector<Ort::Value> input_tensors;
        input_tensors.push_back(std::move(image_tensor));
        input_tensors.push_back(std::move(size_tensor));

        const std::vector<std::string> output_names_str = session.GetOutputNames();
        std::vector<const char *> output_names;
        for (const auto &name: output_names_str) {
            output_names.push_back(name.c_str());
        }
        auto run_options = Ort::RunOptions{};
        auto output_tensors = session.Run(run_options, input_names.data(), input_tensors.data(), input_tensors.size(),
                                          output_names.data(), output_names.size());

        if (output_tensors.size() < 1) {
            return std::nullopt;
        }
        float *const out_data = output_tensors[0].GetTensorMutableData<float>();
        const size_t out_count = output_tensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
        if (out_count < 2) {
            return std::nullopt;
        }
        double downscale_factor = static_cast<double>(out_data[0]);
        double backscaling_ratio = static_cast<double>(out_data[1]);
        if (backscaling_ratio <= 0.0 || backscaling_ratio >= 1.0) {
            backscaling_ratio = 1.0 / (1.0 + std::exp(-static_cast<double>(out_data[1])));
        }
        downscale_factor = std::clamp(downscale_factor, 0.2, 1.0);
        const double upscale_multiplier = backscaling_ratio_to_upscale(backscaling_ratio, downscale_factor);
        return std::make_pair(downscale_factor, upscale_multiplier);
    } catch (const std::exception &e) {
        sf::warning(input_analyzer_cat, "Input analyzer inference failed: %s", e.what());
        return std::nullopt;
    }
#else
    (void) analyzer;
    (void) buffer;
    (void) props;
    (void) qp;
    return std::nullopt;
#endif
}

luma_factors_for_frame get_luma_factors_for_frame(global_data &data, GstBuffer *buffer, const frame_properties &props) {
    luma_factors_for_frame result{};
    if (!data.params.enable_luma_adaptation) {
        result.downscale_factor = 1.0;
        result.upscale_multiplier = std::nullopt;
        data.last_applied_luma_downscale.store(1.0);
        data.last_applied_luma_upscale.store(1.0);
        return result;
    }
    if (data.params.enable_adaptive_luma_adaptation && data.adaptive_luma_analyzer &&
        input_analyzer_is_valid(data.adaptive_luma_analyzer.get())) {
        const int qp = data.params.encoder_qp.value_or(32);
        std::optional<std::pair<double, double>> factors;
        {
            const std::lock_guard lock(data.adaptive_luma_mutex);
            const uint64_t frame_index = data.adaptive_luma_frame_count++;
            const int keyframe_period = std::max(get_encoder_keyframe_period(data.params), 1);
            const bool is_keyframe = (frame_index % static_cast<uint64_t>(keyframe_period) == 0);
            const bool run_analyzer = !data.params.adaptive_luma_analyzer_on_keyframes_only || is_keyframe;
            if (run_analyzer) {
                factors = input_analyzer_analyze(data.adaptive_luma_analyzer.get(), buffer, props, qp);
                if (factors.has_value()) {
                    data.adaptive_luma_cached_factors = *factors;
                }
            }
            if (!factors.has_value()) {
                factors = data.adaptive_luma_cached_factors;
                if (run_analyzer) {
                    sf::warning(input_analyzer_cat,
                                "Input analyzer failed for this frame; reusing previous "
                                "(downscale_factor,upscale_multiplier)=(%.3f,%.3f)",
                                factors->first, factors->second);
                }
            }
        }
        if (factors.has_value()) {
            result.downscale_factor = factors->first;
            result.upscale_multiplier = factors->second;
            data.last_applied_luma_downscale.store(factors->first);
            data.last_applied_luma_upscale.store(factors->second);
            return result;
        }
    }

    // Fixed path: use configured luma_downscale_factor (adaptive disabled, or analyzer unavailable/failed)
    if (data.params.enable_adaptive_luma_adaptation) {
        static std::atomic<bool> logged_once{false};
        if (!logged_once.exchange(true)) {
            if (!data.adaptive_luma_analyzer) {
                sf::warning(input_analyzer_cat,
                            "Adaptive luma enabled but analyzer is null; using fixed luma_downscale_factor=%.2f",
                            data.params.luma_downscale_factor);
            } else if (!input_analyzer_is_valid(data.adaptive_luma_analyzer.get())) {
                sf::warning(input_analyzer_cat,
                            "Adaptive luma enabled but analyzer not valid; using fixed luma_downscale_factor=%.2f",
                            data.params.luma_downscale_factor);
            } else {
                sf::warning(
                        input_analyzer_cat,
                        "Adaptive luma enabled but no factors from analyzer; using fixed luma_downscale_factor=%.2f",
                        data.params.luma_downscale_factor);
            }
        }
    }
    result.downscale_factor = data.params.luma_downscale_factor;
    result.upscale_multiplier = std::nullopt;
    data.last_applied_luma_downscale.store(data.params.luma_downscale_factor);
    data.last_applied_luma_upscale.store(1.0);
    return result;
}
