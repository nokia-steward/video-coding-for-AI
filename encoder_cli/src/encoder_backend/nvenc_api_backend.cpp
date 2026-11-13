// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_backend.h"

#include "../../libvcai/include/area_copy.h"
#include "../../libvcai/include/logging.h"
#include "../encoder_cli_debug.h"
#include "../global_data.h"
#include "../gst/input_types/frame_path_meta.h"
#include "../input_analyzer/input_analyzer.h"
#include "../processing/frame_processing_utils.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <dlfcn.h>
#include <gst/gst.h>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(HAVE_NVENCODEAPI_HEADERS) && HAVE_NVENCODEAPI_HEADERS
#if __has_include(<ffnvcodec/nvEncodeAPI.h>)
#include <ffnvcodec/nvEncodeAPI.h>
#define SF_HAS_NVENCODEAPI_INCLUDE 1
#elif __has_include(<nvEncodeAPI.h>)
#include <nvEncodeAPI.h>
#define SF_HAS_NVENCODEAPI_INCLUDE 1
#else
#define SF_HAS_NVENCODEAPI_INCLUDE 0
#endif
#else
#define SF_HAS_NVENCODEAPI_INCLUDE 0
#endif

namespace {
    [[nodiscard]] bool is_x86_architecture() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] size_t expected_qp_map_elements(const qp_map_frame_payload &payload) {
        const int ctb = std::max(1, payload.ctb_size);
        const int columns = (payload.width + ctb - 1) / ctb;
        const int rows = (payload.height + ctb - 1) / ctb;
        return static_cast<size_t>(columns) * static_cast<size_t>(rows);
    }

    [[nodiscard]] bool validate_qp_map_payload(const qp_map_frame_payload &payload, std::string &reason) {
        constexpr int direct_nvenc_ctb_size = 32;

        if (payload.width <= 0 || payload.height <= 0) {
            reason = "QP-map payload has invalid frame dimensions";
            return false;
        }
        if (payload.ctb_size <= 0) {
            reason = "QP-map payload has invalid CTB size";
            return false;
        }
        if (payload.ctb_size != direct_nvenc_ctb_size) {
            reason = "Direct NvEncodeAPI backend expects CTB size 32, got " + std::to_string(payload.ctb_size);
            return false;
        }
        if (payload.delta_map.empty()) {
            reason = "QP-map payload delta map is empty";
            return false;
        }

        const size_t expected = expected_qp_map_elements(payload);
        if (payload.delta_map.size() != expected) {
            reason = "QP-map payload size mismatch: expected " + std::to_string(expected) + ", got " +
                     std::to_string(payload.delta_map.size());
            return false;
        }

        for (const int8_t delta: payload.delta_map) {
            if (delta < 0) {
                reason = "QP-map payload delta value out of range: " + std::to_string(delta);
                return false;
            }
        }

        reason.clear();
        return true;
    }
} // namespace

bool validate_qp_map_frame_payload(const qp_map_frame_payload &payload, std::string *reason) {
    std::string local_reason;
    const bool is_valid = validate_qp_map_payload(payload, local_reason);
    if (reason) {
        *reason = local_reason;
    }
    return is_valid;
}

#if SF_HAS_NVENCODEAPI_INCLUDE
namespace {
    [[nodiscard]] const char *nvenc_status_to_string(const NVENCSTATUS status) {
        switch (status) {
            case NV_ENC_SUCCESS:
                return "NV_ENC_SUCCESS";
            case NV_ENC_ERR_NO_ENCODE_DEVICE:
                return "NV_ENC_ERR_NO_ENCODE_DEVICE";
            case NV_ENC_ERR_UNSUPPORTED_DEVICE:
                return "NV_ENC_ERR_UNSUPPORTED_DEVICE";
            case NV_ENC_ERR_INVALID_ENCODERDEVICE:
                return "NV_ENC_ERR_INVALID_ENCODERDEVICE";
            case NV_ENC_ERR_INVALID_DEVICE:
                return "NV_ENC_ERR_INVALID_DEVICE";
            case NV_ENC_ERR_DEVICE_NOT_EXIST:
                return "NV_ENC_ERR_DEVICE_NOT_EXIST";
            case NV_ENC_ERR_INVALID_PTR:
                return "NV_ENC_ERR_INVALID_PTR";
            case NV_ENC_ERR_INVALID_PARAM:
                return "NV_ENC_ERR_INVALID_PARAM";
            case NV_ENC_ERR_INVALID_CALL:
                return "NV_ENC_ERR_INVALID_CALL";
            case NV_ENC_ERR_OUT_OF_MEMORY:
                return "NV_ENC_ERR_OUT_OF_MEMORY";
            case NV_ENC_ERR_ENCODER_NOT_INITIALIZED:
                return "NV_ENC_ERR_ENCODER_NOT_INITIALIZED";
            case NV_ENC_ERR_UNSUPPORTED_PARAM:
                return "NV_ENC_ERR_UNSUPPORTED_PARAM";
            case NV_ENC_ERR_LOCK_BUSY:
                return "NV_ENC_ERR_LOCK_BUSY";
            case NV_ENC_ERR_NOT_ENOUGH_BUFFER:
                return "NV_ENC_ERR_NOT_ENOUGH_BUFFER";
            case NV_ENC_ERR_INVALID_VERSION:
                return "NV_ENC_ERR_INVALID_VERSION";
            case NV_ENC_ERR_MAP_FAILED:
                return "NV_ENC_ERR_MAP_FAILED";
            case NV_ENC_ERR_NEED_MORE_INPUT:
                return "NV_ENC_ERR_NEED_MORE_INPUT";
            case NV_ENC_ERR_ENCODER_BUSY:
                return "NV_ENC_ERR_ENCODER_BUSY";
            case NV_ENC_ERR_GENERIC:
                return "NV_ENC_ERR_GENERIC";
            case NV_ENC_ERR_UNIMPLEMENTED:
                return "NV_ENC_ERR_UNIMPLEMENTED";
            case NV_ENC_ERR_RESOURCE_REGISTER_FAILED:
                return "NV_ENC_ERR_RESOURCE_REGISTER_FAILED";
            case NV_ENC_ERR_RESOURCE_NOT_REGISTERED:
                return "NV_ENC_ERR_RESOURCE_NOT_REGISTERED";
            case NV_ENC_ERR_RESOURCE_NOT_MAPPED:
                return "NV_ENC_ERR_RESOURCE_NOT_MAPPED";
            default:
                return "NV_ENC_ERR_UNKNOWN";
        }
    }

    [[nodiscard]] std::string format_nvenc_failure_reason(const char *operation, const NVENCSTATUS status) {
        return std::string(operation) + " failed (" + nvenc_status_to_string(status) +
               ", code=" + std::to_string(static_cast<int>(status)) + ")";
    }

    using CUdevice = int;
    using CUcontext = void *;
    using CUdeviceptr = unsigned long long;
    using CUresult = int;

    using nvenc_get_max_supported_version_fn = NVENCSTATUS(NVENCAPI *)(uint32_t *version);
    using nvenc_create_instance_fn = NVENCSTATUS(NVENCAPI *)(NV_ENCODE_API_FUNCTION_LIST *function_list);

    using cu_init_fn = CUresult (*)(unsigned int);
    using cu_device_get_fn = CUresult (*)(CUdevice *device, int ordinal);
    using cu_ctx_create_fn = CUresult (*)(CUcontext *pctx, unsigned int flags, CUdevice dev);
    using cu_ctx_destroy_fn = CUresult (*)(CUcontext ctx);
    using cu_mem_alloc_fn = CUresult (*)(CUdeviceptr *dptr, size_t bytesize);
    using cu_mem_free_fn = CUresult (*)(CUdeviceptr dptr);
    using cu_memcpy_htod_fn = CUresult (*)(CUdeviceptr dst_device, const void *src_host, size_t byte_count);
    using cu_ctx_set_current_fn = CUresult (*)(CUcontext ctx);

    struct library_handle {
        void *handle = nullptr;

        library_handle() = default;
        explicit library_handle(void *lib_handle) : handle(lib_handle) {}

        library_handle(const library_handle &) = delete;
        library_handle &operator=(const library_handle &) = delete;

        library_handle(library_handle &&other) noexcept : handle(other.handle) { other.handle = nullptr; }

        library_handle &operator=(library_handle &&other) noexcept {
            if (this == &other) {
                return *this;
            }
            if (handle) {
                dlclose(handle);
            }
            handle = other.handle;
            other.handle = nullptr;
            return *this;
        }

        ~library_handle() {
            if (handle) {
                dlclose(handle);
            }
        }
    };

    struct cuda_driver_api {
        cu_init_fn cu_init = nullptr;
        cu_device_get_fn cu_device_get = nullptr;
        cu_ctx_create_fn cu_ctx_create = nullptr;
        cu_ctx_destroy_fn cu_ctx_destroy = nullptr;
        cu_mem_alloc_fn cu_mem_alloc = nullptr;
        cu_mem_free_fn cu_mem_free = nullptr;
        cu_memcpy_htod_fn cu_memcpy_htod = nullptr;
        cu_ctx_set_current_fn cu_ctx_set_current = nullptr;

        [[nodiscard]] bool is_complete() const {
            return cu_init && cu_device_get && cu_ctx_create && cu_ctx_destroy && cu_mem_alloc && cu_mem_free &&
                   cu_memcpy_htod && cu_ctx_set_current;
        }
    };

    [[nodiscard]] bool load_cuda_driver_api(void *cuda_lib, cuda_driver_api &api, std::string &reason) {
        api.cu_init = reinterpret_cast<cu_init_fn>(dlsym(cuda_lib, "cuInit"));
        api.cu_device_get = reinterpret_cast<cu_device_get_fn>(dlsym(cuda_lib, "cuDeviceGet"));
        api.cu_ctx_create = reinterpret_cast<cu_ctx_create_fn>(dlsym(cuda_lib, "cuCtxCreate_v2"));
        if (!api.cu_ctx_create) {
            api.cu_ctx_create = reinterpret_cast<cu_ctx_create_fn>(dlsym(cuda_lib, "cuCtxCreate"));
        }

        api.cu_ctx_destroy = reinterpret_cast<cu_ctx_destroy_fn>(dlsym(cuda_lib, "cuCtxDestroy_v2"));
        if (!api.cu_ctx_destroy) {
            api.cu_ctx_destroy = reinterpret_cast<cu_ctx_destroy_fn>(dlsym(cuda_lib, "cuCtxDestroy"));
        }

        api.cu_mem_alloc = reinterpret_cast<cu_mem_alloc_fn>(dlsym(cuda_lib, "cuMemAlloc_v2"));
        if (!api.cu_mem_alloc) {
            api.cu_mem_alloc = reinterpret_cast<cu_mem_alloc_fn>(dlsym(cuda_lib, "cuMemAlloc"));
        }

        api.cu_mem_free = reinterpret_cast<cu_mem_free_fn>(dlsym(cuda_lib, "cuMemFree_v2"));
        if (!api.cu_mem_free) {
            api.cu_mem_free = reinterpret_cast<cu_mem_free_fn>(dlsym(cuda_lib, "cuMemFree"));
        }

        api.cu_memcpy_htod = reinterpret_cast<cu_memcpy_htod_fn>(dlsym(cuda_lib, "cuMemcpyHtoD_v2"));
        if (!api.cu_memcpy_htod) {
            api.cu_memcpy_htod = reinterpret_cast<cu_memcpy_htod_fn>(dlsym(cuda_lib, "cuMemcpyHtoD"));
        }
        api.cu_ctx_set_current = reinterpret_cast<cu_ctx_set_current_fn>(dlsym(cuda_lib, "cuCtxSetCurrent"));
        if (!api.cu_ctx_set_current) {
            api.cu_ctx_set_current = reinterpret_cast<cu_ctx_set_current_fn>(dlsym(cuda_lib, "cuCtxSetCurrent_v2"));
        }

        if (!api.is_complete()) {
            reason = "Required CUDA driver API symbols are missing from libcuda.so.1";
            return false;
        }

        reason.clear();
        return true;
    }

    [[nodiscard]] std::vector<uint8_t> convert_i420_to_nv12(const uint8_t *src, const int width, const int height) {
        const size_t luma_plane_size = static_cast<size_t>(width) * static_cast<size_t>(height);
        const size_t uv_plane_size = luma_plane_size / 4U;

        std::vector<uint8_t> dst(luma_plane_size + (2U * uv_plane_size));
        std::memcpy(dst.data(), src, luma_plane_size);

        const uint8_t *u_plane = src + luma_plane_size;
        const uint8_t *v_plane = u_plane + uv_plane_size;
        uint8_t *uv_dst = dst.data() + luma_plane_size;

        for (size_t index = 0; index < uv_plane_size; ++index) {
            uv_dst[2U * index] = u_plane[index];
            uv_dst[2U * index + 1U] = v_plane[index];
        }

        return dst;
    }

    class nvenc_api_backend final : public encoder_backend {
    public:
        nvenc_api_backend(library_handle nvenc_lib, library_handle cuda_lib, cuda_driver_api cuda_api,
                          NV_ENCODE_API_FUNCTION_LIST api, void *session, CUcontext cuda_context, CUdeviceptr input_ptr,
                          NV_ENC_REGISTERED_PTR registered_input, NV_ENC_INPUT_PTR mapped_input,
                          NV_ENC_OUTPUT_PTR output_bitstream, const uint32_t width, const uint32_t height) :
            nvenc_lib_handle(std::move(nvenc_lib)),
            cuda_lib_handle(std::move(cuda_lib)), cuda_driver(cuda_api), nvenc_api(api), encoder_session(session),
            cuda_context_handle(cuda_context), input_device_ptr(input_ptr), registered_input_resource(registered_input),
            mapped_input_resource(mapped_input), output_bitstream_buffer(output_bitstream), frame_width(width),
            frame_height(height),
            h265_caps_value(gst_caps_new_simple("video/x-h265", "stream-format", G_TYPE_STRING, "byte-stream",
                                                "alignment", G_TYPE_STRING, "au", nullptr)) {}

        nvenc_api_backend(const nvenc_api_backend &) = delete;
        nvenc_api_backend &operator=(const nvenc_api_backend &) = delete;
        nvenc_api_backend(nvenc_api_backend &&) = delete;
        nvenc_api_backend &operator=(nvenc_api_backend &&) = delete;

        ~nvenc_api_backend() override { release_resources(); }

        [[nodiscard]] encoder_backend_kind kind() const override { return encoder_backend_kind::nvenc_api; }

        [[nodiscard]] bool requires_gst_encoding_pipeline() const override { return false; }

        [[nodiscard]] bool submit_frame(global_data &data, sf::unique_gst_buffer_ptr output_buffer,
                                        std::optional<qp_map_frame_payload> qp_map_payload) override {
            if (eos_sent_flag) {
                sf::warning(encoder_cli_main, "NvEncodeAPI backend received frame after EOS was sent");
                return false;
            }
            if (!output_buffer) {
                return false;
            }
            if (!qp_map_payload.has_value()) {
                sf::warning(encoder_cli_main, "NvEncodeAPI backend expected QP-map payload but none was provided");
                return false;
            }

            std::string validation_reason;
            if (!validate_qp_map_frame_payload(*qp_map_payload, &validation_reason)) {
                sf::warning(encoder_cli_main, "Invalid QP-map payload for frame %d: %s", qp_map_payload->frame_number,
                            validation_reason.c_str());
                return false;
            }

            const bool input_is_nv12 = !data.params.format.has_value() || data.params.format == sf::video_format::nv12;
            encoded_frame_luma_adaptation_state luma_adaptation_state{};
            const auto frame_contract = get_frame_contract_from_buffer(output_buffer.get());
            if (!frame_contract.has_value()) {
                data.metadata_runtime.record_validation_failure();
                data.request_metadata_contract_fail_stop("nvenc backend missing frame contract metadata");
                sf::error(encoder_cli_main, "NvEncodeAPI backend submission missing frame contract metadata");
                return false;
            }
            if (data.params.enable_luma_adaptation) {
                const char *const input_format =
                        data.params.format.has_value() ? sf::video_format_to_string(*data.params.format) : "NV12";
                const frame_properties props = {
                        .width = static_cast<int>(frame_width),
                        .height = static_cast<int>(frame_height),
                        .format = input_format,
                };
                const luma_factors_for_frame factors = get_luma_factors_for_frame(data, output_buffer.get(), props);
                luma_adaptation_state =
                        maybe_apply_luma_adaptation_to_buffer(output_buffer, props, true, factors.downscale_factor);
                if (luma_adaptation_state.enabled) {
                    luma_adaptation_state.upscale_multiplier = factors.upscale_multiplier;
                } else {
                    sf::warning(encoder_cli_main, "Failed to apply luma downscaling before direct NvEncodeAPI upload; "
                                                  "continuing without luma-adaptation EOI for this frame");
                }
            }

            if (!copy_input_frame_to_device(*output_buffer, input_is_nv12)) {
                return false;
            }

            NV_ENC_PIC_PARAMS pic_params = {};
            pic_params.version = NV_ENC_PIC_PARAMS_VER;
            pic_params.inputBuffer = mapped_input_resource;
            pic_params.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
            pic_params.inputWidth = frame_width;
            pic_params.inputHeight = frame_height;
            pic_params.inputPitch = frame_width;
            pic_params.outputBitstream = output_bitstream_buffer;
            pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
            const GstClockTime input_pts = GST_BUFFER_PTS(output_buffer.get());
            const GstClockTime input_duration = GST_BUFFER_DURATION(output_buffer.get());
            pic_params.inputTimeStamp = (input_pts == GST_CLOCK_TIME_NONE) ? 0 : input_pts;
            pic_params.inputDuration = (input_duration == GST_CLOCK_TIME_NONE) ? 0 : input_duration;
            pic_params.qpDeltaMap = qp_map_payload->delta_map.data();
            pic_params.qpDeltaMapSize = static_cast<uint32_t>(qp_map_payload->delta_map.size());

            const NVENCSTATUS encode_status = nvenc_api.nvEncEncodePicture(encoder_session, &pic_params);
            if (encode_status == NV_ENC_ERR_NEED_MORE_INPUT) {
                pending_contract_states_queue.push_back(
                        {.sequence_id = frame_contract->sequence_id, .luma_adaptation = luma_adaptation_state});
                if (data.monitoring.status_reporter) {
                    data.monitoring.status_reporter->update_pushed_to_encoder();
                }
                return true;
            }
            if (encode_status != NV_ENC_SUCCESS) {
                sf::warning(encoder_cli_main, "NvEncodeAPI encode failed for frame %d: %s",
                            qp_map_payload->frame_number,
                            format_nvenc_failure_reason("NvEncEncodePicture", encode_status).c_str());
                return false;
            }

            pending_contract_states_queue.push_back(
                    {.sequence_id = frame_contract->sequence_id, .luma_adaptation = luma_adaptation_state});
            if (!drain_one_output_packet(data, *output_buffer)) {
                return false;
            }

            if (data.monitoring.status_reporter) {
                data.monitoring.status_reporter->update_pushed_to_encoder();
            }
            return true;
        }

        void signal_eos(global_data &data) override {
            if (eos_sent_flag) {
                sf::info(encoder_cli_main, "Direct NvEncodeAPI signal_eos called after EOS already sent");
                return;
            }

            sf::info(encoder_cli_main, "Direct NvEncodeAPI signal_eos: flushing encoder");
            NV_ENC_PIC_PARAMS eos_params = {};
            eos_params.version = NV_ENC_PIC_PARAMS_VER;
            eos_params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
            const NVENCSTATUS status = nvenc_api.nvEncEncodePicture(encoder_session, &eos_params);
            if (status != NV_ENC_SUCCESS && status != NV_ENC_ERR_NEED_MORE_INPUT) {
                sf::warning(encoder_cli_main, "NvEncodeAPI EOS encode call failed: %s",
                            format_nvenc_failure_reason("NvEncEncodePicture(EOS)", status).c_str());
            }

            drain_pending_output_packets(data);
            if (!pending_contract_states_queue.empty()) {
                sf::warning(encoder_cli_main,
                            "Direct NvEncodeAPI flush completed with %zu unpaired encoded contract state entries; "
                            "clearing them during shutdown",
                            pending_contract_states_queue.size());
                pending_contract_states_queue.clear();
            }
            eos_sent_flag = true;
            data.close_encoded_queue_once();
            sf::info(encoder_cli_main, "Direct NvEncodeAPI signal_eos: flush complete, encoded queue closed");
        }

    private:
        [[nodiscard]] bool copy_input_frame_to_device(GstBuffer &buffer, const bool input_is_nv12) {
            constexpr size_t bytes_per_pixel_num = 3;
            constexpr size_t bytes_per_pixel_den = 2;
            const size_t expected_size = static_cast<size_t>(frame_width) * static_cast<size_t>(frame_height) *
                                         bytes_per_pixel_num / bytes_per_pixel_den;

            GstMapInfo map_info = {};
            if (!gst_buffer_map(&buffer, &map_info, GST_MAP_READ)) {
                sf::warning(encoder_cli_main, "Failed to map input GstBuffer for NvEncodeAPI backend");
                return false;
            }

            const CUresult set_ctx_result = cuda_driver.cu_ctx_set_current(cuda_context_handle);
            if (set_ctx_result != 0) {
                gst_buffer_unmap(&buffer, &map_info);
                sf::warning(encoder_cli_main, "Failed to set CUDA context current for frame upload (cuResult=%d)",
                            static_cast<int>(set_ctx_result));
                return false;
            }

            bool success = false;
            if (map_info.size < expected_size) {
                sf::warning(encoder_cli_main, "NvEncodeAPI input buffer too small: expected %zu bytes, got %zu",
                            expected_size, map_info.size);
            } else if (input_is_nv12) {
                const CUresult copy_result = cuda_driver.cu_memcpy_htod(input_device_ptr, map_info.data, expected_size);
                if (copy_result != 0) {
                    sf::warning(encoder_cli_main, "cuMemcpyHtoD failed for NV12 upload (cuResult=%d)",
                                static_cast<int>(copy_result));
                }
                success = (copy_result == 0);
            } else {
                const auto nv12 = convert_i420_to_nv12(map_info.data, static_cast<int>(frame_width),
                                                       static_cast<int>(frame_height));
                const CUresult copy_result = cuda_driver.cu_memcpy_htod(input_device_ptr, nv12.data(), nv12.size());
                if (copy_result != 0) {
                    sf::warning(encoder_cli_main, "cuMemcpyHtoD failed for I420->NV12 upload (cuResult=%d)",
                                static_cast<int>(copy_result));
                }
                success = (copy_result == 0);
            }

            gst_buffer_unmap(&buffer, &map_info);
            if (!success) {
                sf::warning(encoder_cli_main, "Failed to upload frame data to CUDA device buffer");
            }
            return success;
        }

        [[nodiscard]] bool drain_one_output_packet(global_data &data, const GstBuffer &source_timing) {
            NV_ENC_LOCK_BITSTREAM lock_params = {};
            lock_params.version = NV_ENC_LOCK_BITSTREAM_VER;
            lock_params.outputBitstream = output_bitstream_buffer;
            lock_params.doNotWait = 0;

            const NVENCSTATUS lock_status = nvenc_api.nvEncLockBitstream(encoder_session, &lock_params);
            if (lock_status == NV_ENC_ERR_NEED_MORE_INPUT) {
                return true;
            }
            if (lock_status != NV_ENC_SUCCESS) {
                sf::warning(encoder_cli_main, "NvEncodeAPI lock bitstream failed: %s",
                            format_nvenc_failure_reason("NvEncLockBitstream", lock_status).c_str());
                return false;
            }

            const bool pushed = push_locked_packet_to_queue(data, lock_params, &source_timing);
            nvenc_api.nvEncUnlockBitstream(encoder_session, output_bitstream_buffer);
            return pushed;
        }

        [[nodiscard]] pending_encoded_contract_state consume_next_contract_state() {
            if (pending_contract_states_queue.empty()) {
                sf::warning(encoder_cli_main, "Direct NvEncodeAPI emitted a packet without queued contract state; "
                                              "defaulting to invalid sequence and disabled luma-adaptation EOI");
                return {};
            }

            pending_encoded_contract_state state = pending_contract_states_queue.front();
            pending_contract_states_queue.pop_front();
            return state;
        }

        [[nodiscard]] bool push_locked_packet_to_queue(global_data &data, const NV_ENC_LOCK_BITSTREAM &lock_params,
                                                       const GstBuffer *source_timing) {
            if (lock_params.bitstreamSizeInBytes == 0U) {
                static_cast<void>(consume_next_contract_state());
                return true;
            }
            if (lock_params.bitstreamBufferPtr == nullptr) {
                static_cast<void>(consume_next_contract_state());
                sf::warning(encoder_cli_main,
                            "NvEncodeAPI returned null bitstream pointer with non-zero packet size (%u), "
                            "dropping packet to avoid invalid memory access",
                            lock_params.bitstreamSizeInBytes);
                return true;
            }

            const pending_encoded_contract_state contract_state = consume_next_contract_state();
            const auto out_buffer = sf::unique_gst_buffer_ptr(
                    gst_buffer_new_allocate(nullptr, lock_params.bitstreamSizeInBytes, nullptr));
            if (!out_buffer) {
                return false;
            }

            GstMapInfo out_map_info = {};
            if (!gst_buffer_map(out_buffer.get(), &out_map_info, GST_MAP_WRITE)) {
                return false;
            }
            if (out_map_info.size < lock_params.bitstreamSizeInBytes) {
                gst_buffer_unmap(out_buffer.get(), &out_map_info);
                sf::warning(encoder_cli_main, "Allocated GstBuffer is smaller than NVENC bitstream packet (%zu < %u)",
                            out_map_info.size, lock_params.bitstreamSizeInBytes);
                return false;
            }
            std::memcpy(out_map_info.data, lock_params.bitstreamBufferPtr, lock_params.bitstreamSizeInBytes);
            gst_buffer_unmap(out_buffer.get(), &out_map_info);

            if (source_timing) {
                GST_BUFFER_PTS(out_buffer.get()) = GST_BUFFER_PTS(source_timing);
                GST_BUFFER_DTS(out_buffer.get()) = GST_BUFFER_DTS(source_timing);
                GST_BUFFER_DURATION(out_buffer.get()) = GST_BUFFER_DURATION(source_timing);
                GST_BUFFER_OFFSET(out_buffer.get()) = GST_BUFFER_OFFSET(source_timing);
                GST_BUFFER_OFFSET_END(out_buffer.get()) = GST_BUFFER_OFFSET_END(source_timing);
            } else {
                GST_BUFFER_PTS(out_buffer.get()) = GST_CLOCK_TIME_NONE;
                GST_BUFFER_DTS(out_buffer.get()) = GST_CLOCK_TIME_NONE;
                GST_BUFFER_DURATION(out_buffer.get()) = GST_CLOCK_TIME_NONE;
            }

            auto sample = sf::unique_gst_sample_ptr(
                    gst_sample_new(out_buffer.get(), h265_caps_value.get(), nullptr, nullptr));
            if (!sample) {
                return false;
            }
            data.encoded_frame_queue.push(make_encoded_frame_item(std::move(sample), contract_state.luma_adaptation,
                                                                  contract_state.sequence_id));
            return true;
        }

        void drain_pending_output_packets(global_data &data) {
            constexpr int max_drain_iterations = 64;
            constexpr int max_lock_busy_retries = 100;
            for (int iter = 0; iter < max_drain_iterations; ++iter) {
                NV_ENC_LOCK_BITSTREAM lock_params = {};
                lock_params.version = NV_ENC_LOCK_BITSTREAM_VER;
                lock_params.outputBitstream = output_bitstream_buffer;
                lock_params.doNotWait = 1;

                NVENCSTATUS lock_status = NV_ENC_ERR_LOCK_BUSY;
                for (int retry = 0; retry < max_lock_busy_retries; ++retry) {
                    lock_status = nvenc_api.nvEncLockBitstream(encoder_session, &lock_params);
                    if (lock_status == NV_ENC_SUCCESS) {
                        break;
                    }
                    if (lock_status == NV_ENC_ERR_LOCK_BUSY) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        continue;
                    }
                    break;
                }

                if (lock_status == NV_ENC_SUCCESS) {
                    const bool pushed = push_locked_packet_to_queue(data, lock_params, nullptr);
                    nvenc_api.nvEncUnlockBitstream(encoder_session, output_bitstream_buffer);
                    if (!pushed || lock_params.bitstreamSizeInBytes == 0U) {
                        break;
                    }
                    continue;
                }

                if (lock_status == NV_ENC_ERR_LOCK_BUSY) {
                    sf::warning(encoder_cli_main,
                                "NvEncodeAPI EOS drain timed out waiting for bitstream after %d retries",
                                max_lock_busy_retries);
                    break;
                }
                if (lock_status == NV_ENC_ERR_NEED_MORE_INPUT || lock_status == NV_ENC_ERR_INVALID_CALL) {
                    break;
                }

                sf::warning(encoder_cli_main, "NvEncodeAPI EOS drain lock failed: %s",
                            format_nvenc_failure_reason("NvEncLockBitstream(EOS)", lock_status).c_str());
                break;
            }
        }

        void release_resources() {
            pending_contract_states_queue.clear();
            if (encoder_session && mapped_input_resource) {
                nvenc_api.nvEncUnmapInputResource(encoder_session, mapped_input_resource);
                mapped_input_resource = nullptr;
            }
            if (encoder_session && registered_input_resource) {
                nvenc_api.nvEncUnregisterResource(encoder_session, registered_input_resource);
                registered_input_resource = nullptr;
            }
            if (encoder_session && output_bitstream_buffer) {
                nvenc_api.nvEncDestroyBitstreamBuffer(encoder_session, output_bitstream_buffer);
                output_bitstream_buffer = nullptr;
            }
            if (encoder_session) {
                nvenc_api.nvEncDestroyEncoder(encoder_session);
                encoder_session = nullptr;
            }
            if (input_device_ptr != 0U) {
                cuda_driver.cu_mem_free(input_device_ptr);
                input_device_ptr = 0U;
            }
            if (cuda_context_handle) {
                cuda_driver.cu_ctx_destroy(cuda_context_handle);
                cuda_context_handle = nullptr;
            }
        }

        library_handle nvenc_lib_handle{};
        library_handle cuda_lib_handle{};
        cuda_driver_api cuda_driver{};
        NV_ENCODE_API_FUNCTION_LIST nvenc_api = {};
        void *encoder_session = nullptr;
        CUcontext cuda_context_handle = nullptr;
        CUdeviceptr input_device_ptr = 0;
        NV_ENC_REGISTERED_PTR registered_input_resource = nullptr;
        NV_ENC_INPUT_PTR mapped_input_resource = nullptr;
        NV_ENC_OUTPUT_PTR output_bitstream_buffer = nullptr;
        uint32_t frame_width = 0;
        uint32_t frame_height = 0;
        bool eos_sent_flag = false;
        sf::unique_gst_caps_ptr h265_caps_value{};
        std::deque<pending_encoded_contract_state> pending_contract_states_queue{};
    };

    [[nodiscard]] bool has_required_nvenc_functions(const NV_ENCODE_API_FUNCTION_LIST &api) {
        return api.nvEncOpenEncodeSessionEx && api.nvEncInitializeEncoder && api.nvEncRegisterResource &&
               api.nvEncMapInputResource && api.nvEncUnmapInputResource && api.nvEncCreateBitstreamBuffer &&
               api.nvEncDestroyBitstreamBuffer && api.nvEncEncodePicture && api.nvEncLockBitstream &&
               api.nvEncUnlockBitstream && api.nvEncUnregisterResource && api.nvEncDestroyEncoder;
    }

    [[nodiscard]] bool load_preset_config(void *session, const NV_ENCODE_API_FUNCTION_LIST &api, NV_ENC_CONFIG &config,
                                          const GUID &codec_guid, const GUID &preset_guid,
                                          const NV_ENC_TUNING_INFO tuning_info, std::string &reason) {
        NV_ENC_PRESET_CONFIG preset = {};
        preset.version = NV_ENC_PRESET_CONFIG_VER;
        preset.presetCfg.version = NV_ENC_CONFIG_VER;

        NVENCSTATUS status = NV_ENC_ERR_UNIMPLEMENTED;
        if (api.nvEncGetEncodePresetConfigEx) {
            status = api.nvEncGetEncodePresetConfigEx(session, codec_guid, preset_guid, tuning_info, &preset);
            if (status != NV_ENC_SUCCESS) {
                reason = format_nvenc_failure_reason("NvEncGetEncodePresetConfigEx", status);
                return false;
            }
        } else if (api.nvEncGetEncodePresetConfig) {
            status = api.nvEncGetEncodePresetConfig(session, codec_guid, preset_guid, &preset);
            if (status != NV_ENC_SUCCESS) {
                reason = format_nvenc_failure_reason("NvEncGetEncodePresetConfig", status);
                return false;
            }
        } else {
            reason = "NvEncodeAPI function list missing preset config query APIs";
            return false;
        }

        config = preset.presetCfg;
        config.version = NV_ENC_CONFIG_VER;
        reason.clear();
        return true;
    }
} // namespace

encoder_backend_factory_result try_create_nvenc_api_backend(const encoder_config &params) {
    encoder_backend_factory_result result{};

    if (!is_x86_architecture()) {
        result.reason = "NvEncodeAPI direct backend is enabled only on x86 desktop systems";
        return result;
    }
    if (!params.is_qp_map_delta_requested()) {
        result.reason = "NvEncodeAPI direct backend is only selected for qp_map_delta mode";
        return result;
    }

    void *nvenc_lib = dlopen("libnvidia-encode.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!nvenc_lib) {
        result.reason = "libnvidia-encode.so.1 not available";
        return result;
    }

    const auto get_max_supported_version =
            reinterpret_cast<nvenc_get_max_supported_version_fn>(dlsym(nvenc_lib, "NvEncodeAPIGetMaxSupportedVersion"));
    const auto create_instance =
            reinterpret_cast<nvenc_create_instance_fn>(dlsym(nvenc_lib, "NvEncodeAPICreateInstance"));
    if (!get_max_supported_version || !create_instance) {
        result.reason = "Required NvEncodeAPI entry points are missing from libnvidia-encode.so.1";
        dlclose(nvenc_lib);
        return result;
    }

    uint32_t max_supported_version = 0;
    const NVENCSTATUS version_status = get_max_supported_version(&max_supported_version);
    if (version_status != NV_ENC_SUCCESS) {
        result.reason = format_nvenc_failure_reason("NvEncodeAPIGetMaxSupportedVersion", version_status);
        dlclose(nvenc_lib);
        return result;
    }
    sf::info(encoder_cli_main, "Detected NvEncodeAPI max supported version: 0x%08x", max_supported_version);

    NV_ENCODE_API_FUNCTION_LIST api = {};
    api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
    const NVENCSTATUS create_status = create_instance(&api);
    if (create_status != NV_ENC_SUCCESS) {
        result.reason = format_nvenc_failure_reason("NvEncodeAPICreateInstance", create_status);
        dlclose(nvenc_lib);
        return result;
    }
    if (!has_required_nvenc_functions(api)) {
        result.reason = "NvEncodeAPI function list does not expose required encode operations";
        dlclose(nvenc_lib);
        return result;
    }

    void *cuda_lib = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!cuda_lib) {
        result.reason = "libcuda.so.1 is unavailable";
        dlclose(nvenc_lib);
        return result;
    }

    cuda_driver_api cuda_api{};
    if (!load_cuda_driver_api(cuda_lib, cuda_api, result.reason)) {
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    CUdevice cuda_device = 0;
    CUcontext cuda_context = nullptr;
    if (cuda_api.cu_init(0) != 0 || cuda_api.cu_device_get(&cuda_device, 0) != 0 ||
        cuda_api.cu_ctx_create(&cuda_context, 0U, cuda_device) != 0) {
        result.reason = "Failed to initialize CUDA context for NvEncodeAPI";
        if (cuda_context) {
            cuda_api.cu_ctx_destroy(cuda_context);
        }
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    void *session = nullptr;
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open_params = {};
    open_params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    open_params.device = cuda_context;
    open_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    open_params.apiVersion = NVENCAPI_VERSION;
    const NVENCSTATUS open_status = api.nvEncOpenEncodeSessionEx(&open_params, &session);
    if (open_status != NV_ENC_SUCCESS) {
        result.reason = format_nvenc_failure_reason("NvEncOpenEncodeSessionEx", open_status);
        cuda_api.cu_ctx_destroy(cuda_context);
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    constexpr NV_ENC_TUNING_INFO tuning_info = NV_ENC_TUNING_INFO_LOW_LATENCY;

    NV_ENC_CONFIG encode_config = {};
    if (!load_preset_config(session, api, encode_config, NV_ENC_CODEC_HEVC_GUID, NV_ENC_PRESET_P4_GUID, tuning_info,
                            result.reason)) {
        api.nvEncDestroyEncoder(session);
        cuda_api.cu_ctx_destroy(cuda_context);
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    encode_config.profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
    encode_config.gopLength = 4;
    encode_config.frameIntervalP = 1;
    encode_config.encodeCodecConfig.hevcConfig.maxCUSize = NV_ENC_HEVC_CUSIZE_32x32;
    encode_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CONSTQP;
    encode_config.rcParams.qpMapMode = NV_ENC_QP_MAP_DELTA;
    constexpr int default_direct_qp = 28;
    const int requested_qp = params.encoder_qp.value_or(default_direct_qp);
    const int clamped_qp = std::clamp(requested_qp, 0, 51);
    encode_config.rcParams.constQP.qpInterP = static_cast<uint32_t>(clamped_qp);
    encode_config.rcParams.constQP.qpInterB = static_cast<uint32_t>(clamped_qp);
    encode_config.rcParams.constQP.qpIntra = static_cast<uint32_t>(clamped_qp);

    if (params.encoder_qp.has_value()) {
        if (requested_qp != clamped_qp) {
            sf::warning(encoder_cli_main, "Direct NvEncodeAPI requested constant QP %d clamped to %d", requested_qp,
                        clamped_qp);
        } else {
            sf::info(encoder_cli_main, "Direct NvEncodeAPI using CLI constant QP=%d", clamped_qp);
        }
    } else {
        sf::info(encoder_cli_main, "Direct NvEncodeAPI constant QP not set; using default QP=%d", clamped_qp);
    }

    const int qp_delta = params.qp_map_background_delta;
    const int object_qp_i = static_cast<int>(encode_config.rcParams.constQP.qpIntra);
    const int object_qp_p = static_cast<int>(encode_config.rcParams.constQP.qpInterP);
    const int object_qp_b = static_cast<int>(encode_config.rcParams.constQP.qpInterB);
    const int background_qp_i = std::clamp(object_qp_i + qp_delta, 0, 51);
    const int background_qp_p = std::clamp(object_qp_p + qp_delta, 0, 51);
    const int background_qp_b = std::clamp(object_qp_b + qp_delta, 0, 51);

    sf::info(encoder_cli_main,
             "Direct NvEncodeAPI QP targets: ctb_size=%d, background_delta=%d, "
             "object_qp(I/P/B)=(%d/%d/%d), background_qp(I/P/B)=(%d/%d/%d)",
             params.qp_map_ctb_size, qp_delta, object_qp_i, object_qp_p, object_qp_b, background_qp_i, background_qp_p,
             background_qp_b);

    NV_ENC_INITIALIZE_PARAMS init_params = {};
    init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
    init_params.encodeGUID = NV_ENC_CODEC_HEVC_GUID;
    init_params.presetGUID = NV_ENC_PRESET_P4_GUID;
    init_params.encodeWidth = static_cast<uint32_t>(std::max(1, params.width));
    init_params.encodeHeight = static_cast<uint32_t>(std::max(1, params.height));
    init_params.darWidth = init_params.encodeWidth;
    init_params.darHeight = init_params.encodeHeight;
    init_params.frameRateNum = static_cast<uint32_t>(std::max(1, params.framerate));
    init_params.frameRateDen = 1;
    init_params.enablePTD = 1;
    init_params.tuningInfo = tuning_info;
    init_params.maxEncodeWidth = init_params.encodeWidth;
    init_params.maxEncodeHeight = init_params.encodeHeight;
    init_params.encodeConfig = &encode_config;
    sf::info(encoder_cli_main,
             "Direct NvEncodeAPI init params: %ux%u @ %u/%u fps, tuning=%d, rcMode=%d, qpMapMode=%d, "
             "constQP(P/B/I)=(%u/%u/%u)",
             init_params.encodeWidth, init_params.encodeHeight, init_params.frameRateNum, init_params.frameRateDen,
             static_cast<int>(init_params.tuningInfo), static_cast<int>(encode_config.rcParams.rateControlMode),
             static_cast<int>(encode_config.rcParams.qpMapMode), encode_config.rcParams.constQP.qpInterP,
             encode_config.rcParams.constQP.qpInterB, encode_config.rcParams.constQP.qpIntra);
    const NVENCSTATUS init_status = api.nvEncInitializeEncoder(session, &init_params);
    if (init_status != NV_ENC_SUCCESS) {
        result.reason = format_nvenc_failure_reason("NvEncInitializeEncoder", init_status) +
                        " [width=" + std::to_string(init_params.encodeWidth) +
                        ", height=" + std::to_string(init_params.encodeHeight) +
                        ", fps_num=" + std::to_string(init_params.frameRateNum) +
                        ", fps_den=" + std::to_string(init_params.frameRateDen) + "]";
        api.nvEncDestroyEncoder(session);
        cuda_api.cu_ctx_destroy(cuda_context);
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    const uint32_t width = static_cast<uint32_t>(std::max(1, params.width));
    const uint32_t height = static_cast<uint32_t>(std::max(1, params.height));
    const size_t frame_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3U / 2U;

    CUdeviceptr input_ptr = 0;
    if (cuda_api.cu_mem_alloc(&input_ptr, frame_bytes) != 0) {
        result.reason = "Failed to allocate CUDA input frame buffer";
        api.nvEncDestroyEncoder(session);
        cuda_api.cu_ctx_destroy(cuda_context);
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    NV_ENC_REGISTER_RESOURCE register_resource = {};
    register_resource.version = NV_ENC_REGISTER_RESOURCE_VER;
    register_resource.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
    register_resource.resourceToRegister = reinterpret_cast<void *>(input_ptr);
    register_resource.width = width;
    register_resource.height = height;
    register_resource.pitch = width;
    register_resource.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
    register_resource.bufferUsage = NV_ENC_INPUT_IMAGE;
    const NVENCSTATUS register_status = api.nvEncRegisterResource(session, &register_resource);
    if (register_status != NV_ENC_SUCCESS) {
        result.reason = format_nvenc_failure_reason("NvEncRegisterResource", register_status);
        cuda_api.cu_mem_free(input_ptr);
        api.nvEncDestroyEncoder(session);
        cuda_api.cu_ctx_destroy(cuda_context);
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    NV_ENC_MAP_INPUT_RESOURCE map_input = {};
    map_input.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
    map_input.registeredResource = register_resource.registeredResource;
    const NVENCSTATUS map_status = api.nvEncMapInputResource(session, &map_input);
    if (map_status != NV_ENC_SUCCESS) {
        result.reason = format_nvenc_failure_reason("NvEncMapInputResource", map_status);
        api.nvEncUnregisterResource(session, register_resource.registeredResource);
        cuda_api.cu_mem_free(input_ptr);
        api.nvEncDestroyEncoder(session);
        cuda_api.cu_ctx_destroy(cuda_context);
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    NV_ENC_CREATE_BITSTREAM_BUFFER create_bitstream = {};
    create_bitstream.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
    create_bitstream.memoryHeap = NV_ENC_MEMORY_HEAP_AUTOSELECT;
    const NVENCSTATUS bitstream_status = api.nvEncCreateBitstreamBuffer(session, &create_bitstream);
    if (bitstream_status != NV_ENC_SUCCESS) {
        result.reason = format_nvenc_failure_reason("NvEncCreateBitstreamBuffer", bitstream_status);
        api.nvEncUnmapInputResource(session, map_input.mappedResource);
        api.nvEncUnregisterResource(session, register_resource.registeredResource);
        cuda_api.cu_mem_free(input_ptr);
        api.nvEncDestroyEncoder(session);
        cuda_api.cu_ctx_destroy(cuda_context);
        dlclose(cuda_lib);
        dlclose(nvenc_lib);
        return result;
    }

    result.backend = std::make_unique<nvenc_api_backend>(library_handle(nvenc_lib), library_handle(cuda_lib), cuda_api,
                                                         api, session, cuda_context, input_ptr,
                                                         register_resource.registeredResource, map_input.mappedResource,
                                                         create_bitstream.bitstreamBuffer, width, height);
    result.qp_map_direct_supported = true;
    result.reason.clear();
    return result;
}
#else
encoder_backend_factory_result try_create_nvenc_api_backend(const encoder_config &params) {
    encoder_backend_factory_result result{};

    if (!is_x86_architecture()) {
        result.reason = "NvEncodeAPI direct backend is enabled only on x86 desktop systems";
        return result;
    }
    if (!params.is_qp_map_delta_requested()) {
        result.reason = "NvEncodeAPI direct backend is only selected for qp_map_delta mode";
        return result;
    }

    result.reason = "System nvEncodeAPI headers are unavailable (install nv-codec-headers/ffnvcodec)";
    return result;
}
#endif
