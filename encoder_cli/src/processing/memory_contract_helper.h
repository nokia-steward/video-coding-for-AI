// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../../libvcai/include/gst_raii.h"
#include "../bridge/context/memory_policy_runtime.h"

/**
 * @brief Status for memory-contract operations.
 */
enum class memory_contract_status { ok, fallback_applied, fatal_stop };

/**
 * @brief Result of making a pad-probe buffer writable.
 */
struct writable_probe_buffer_result {
    memory_contract_status status = memory_contract_status::ok;
    GstBuffer *buffer = nullptr;
};

/**
 * @brief Maps caps memory features into a coarse domain contract.
 * @param caps Caps to inspect.
 * @return Derived memory domain contract.
 */
[[nodiscard]] memory_domain_contract classify_caps_memory_domain(const GstCaps *caps);

/**
 * @brief Converts a memory-domain enum into a readable string.
 * @param domain Memory domain enum value.
 * @return Static string name.
 */
[[nodiscard]] const char *memory_domain_to_string(memory_domain_contract domain);

/**
 * @brief Creates a deep-copy system-memory fallback buffer and increments counters.
 * @param input_buffer Source buffer to clone.
 * @param runtime Per-instance memory policy runtime.
 * @param category Debug category used for logging.
 * @param stage Stage label for logging.
 * @return Owned fallback buffer when successful, otherwise null.
 */
[[nodiscard]] sf::unique_gst_buffer_ptr create_system_memory_fallback_copy(GstBuffer *input_buffer,
                                                                           memory_policy_runtime &runtime,
                                                                           GstDebugCategory *category,
                                                                           const char *stage);

/**
 * @brief Ensures pad-probe buffer mutability using controlled fallback.
 * @param info Pad-probe info whose buffer may need replacement.
 * @param input_buffer Buffer from probe info.
 * @param runtime Per-instance memory policy runtime.
 * @param category Debug category used for logging.
 * @param stage Stage label for logging.
 * @return Writable buffer result and status.
 */
[[nodiscard]] writable_probe_buffer_result ensure_writable_probe_buffer_with_fallback(GstPadProbeInfo *info,
                                                                                      GstBuffer *input_buffer,
                                                                                      memory_policy_runtime &runtime,
                                                                                      GstDebugCategory *category,
                                                                                      const char *stage);
