// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <glib.h>
#include <gst/gst.h>

#include "gst_raii.h"

struct pipeline_statistics;

/**
 * @brief Buffer memory-domain classification used by decoded bridge policies.
 */
enum class decoded_memory_domain {
    system,
    gpu_backed,
};

/**
 * @brief CPU mutation stage requiring writable system-memory buffers.
 */
enum class cpu_mutation_stage {
    pri_reconstruction,
    luma_backscale,
    tet_apply,
};

/**
 * @brief Copies a GstBuffer from CUDA memory to system memory.
 * @param cuda_buffer The buffer in CUDA memory (non-owning reference).
 * @param caps The caps describing the buffer format (non-owning reference).
 * @return A new buffer in system memory, or nullptr on failure.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr
copy_cuda_buffer_to_system_memory(const sf::unique_gst_buffer_no_delete_ptr &cuda_buffer,
                                  const sf::unique_gst_caps_no_delete_ptr &caps);

/**
 * @brief Converts a CUDA memory sample to a system memory sample.
 * @param cuda_sample The sample in CUDA memory.
 * @param frame_number Frame number for logging.
 * @param port Port number for logging.
 * @return System memory sample or nullptr on failure.
 */
[[nodiscard]] sf::unique_gst_sample_ptr
convert_cuda_sample_to_system_memory(const sf::unique_gst_sample_ptr &cuda_sample, guint64 frame_number, guint16 port);

/**
 * @brief Classifies caps into system or GPU-backed memory domain.
 * @param caps Caps to inspect.
 * @return Classified memory domain.
 */
[[nodiscard]] decoded_memory_domain classify_memory_domain_from_caps(const GstCaps *caps);

/**
 * @brief Checks if caps describe NVIDIA GPU-backed buffers.
 * @param caps Caps to inspect.
 * @return true when caps indicate CUDA, NVMM, or nvbuf-backed memory.
 */
[[nodiscard]] bool is_cuda_memory_caps(const GstCaps *caps);

/**
 * @brief Ensures a sample is in system memory for a CPU mutation stage.
 * @param sample Input sample (may be GPU-backed or system memory).
 * @param frame_number Frame number for logging.
 * @param port Port number for logging.
 * @param stage Mutation stage requesting system memory.
 * @param stats Optional statistics sink for conversion/failure counters.
 * @return System-memory sample, or nullptr on failure.
 */
[[nodiscard]] sf::unique_gst_sample_ptr
ensure_system_memory_sample_for_cpu_mutation(sf::unique_gst_sample_ptr sample, guint64 frame_number, guint16 port,
                                             cpu_mutation_stage stage, pipeline_statistics *stats = nullptr);

/**
 * @brief Ensures sample buffer is writable for a CPU mutation stage.
 * @param sample Input sample.
 * @param frame_number Frame number for logging.
 * @param port Port number for logging.
 * @param stage Mutation stage requesting writable access.
 * @param stats Optional statistics sink for writable-copy/failure counters.
 * @return Writable sample, or nullptr on failure.
 */
[[nodiscard]] sf::unique_gst_sample_ptr ensure_writable_sample_for_cpu_mutation(sf::unique_gst_sample_ptr sample,
                                                                                guint64 frame_number, guint16 port,
                                                                                cpu_mutation_stage stage,
                                                                                pipeline_statistics *stats = nullptr);

/**
 * @brief Legacy wrapper: Ensures a sample is in system memory, converting from CUDA if needed.
 * @param sample Input sample (may be CUDA or system memory).
 * @param frame_number Frame number for logging.
 * @param port Port number for logging.
 * @return System memory sample.
 */
[[nodiscard]] sf::unique_gst_sample_ptr ensure_system_memory_sample(sf::unique_gst_sample_ptr sample,
                                                                    guint64 frame_number, guint16 port);

/**
 * @brief Prepares a buffer for pushing while enforcing decoded-appsrc memory contract.
 *        The current contract requires system-memory buffers at decoded_appsrc push boundary.
 * @param sample The sample to prepare.
 * @param frame_number Frame number for logging.
 * @param port Port number for logging.
 * @return Buffer ready for pushing to appsrc.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr
prepare_buffer_for_push_with_domain_check(const sf::unique_gst_sample_ptr &sample, guint64 frame_number, guint16 port);

/**
 * @brief Legacy wrapper for push-buffer preparation.
 * @param sample The sample to prepare.
 * @param frame_number Frame number for logging.
 * @param port Port number for logging.
 * @return Buffer ready for pushing to appsrc.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr prepare_buffer_for_push(const sf::unique_gst_sample_ptr &sample,
                                                                guint64 frame_number, guint16 port);
