// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include <gst/gst.h>

#include "../../../libvcai/include/gst_raii.h"

/**
 * @file encoder_bridge_runtime.h
 * @brief Runtime flags for encoder egress bridge lifecycle.
 */

/**
 * @brief Tracks egress appsrc EOS lifecycle for one encoder instance.
 */
struct encoded_bridge_runtime {
    std::atomic_bool encoding_eos_sent{false};
    std::atomic_bool sei_eos_sent{false};
    std::atomic_bool encoding_startup_configured{false};
    std::atomic_bool sei_startup_configured{false};
    std::atomic_bool encoding_caps_locked{false};
    std::atomic_bool sei_caps_locked{false};
    std::atomic_bool metadata_queues_closed{false};
    mutable std::mutex caps_mutex{};
    sf::unique_gst_caps_ptr encoding_locked_caps{};
    sf::unique_gst_caps_ptr sei_locked_caps{};

    /**
     * @brief Resets EOS tracking flags for reconnect/restart paths.
     */
    void reset() {
        encoding_eos_sent = false;
        sei_eos_sent = false;
        encoding_startup_configured = false;
        sei_startup_configured = false;
        encoding_caps_locked = false;
        sei_caps_locked = false;
        metadata_queues_closed = false;
        std::lock_guard lock(caps_mutex);
        encoding_locked_caps.reset();
        sei_locked_caps.reset();
    }

    /**
     * @brief Marks appsrc startup contract as configured for one endpoint.
     * @param encoding_target True for encoding_src, false for sei_src.
     * @return True when transition to configured happened now, false when already configured.
     */
    [[nodiscard]] bool mark_startup_configured_once(const bool encoding_target) {
        if (encoding_target) {
            return !encoding_startup_configured.exchange(true);
        }
        return !sei_startup_configured.exchange(true);
    }

    /**
     * @brief Returns startup contract state for one endpoint.
     * @param encoding_target True for encoding_src, false for sei_src.
     * @return True when startup contract is configured.
     */
    [[nodiscard]] bool is_startup_configured(const bool encoding_target) const {
        if (encoding_target) {
            return encoding_startup_configured.load();
        }
        return sei_startup_configured.load();
    }

    /**
     * @brief Locks expected caps for one endpoint.
     * @param encoding_target True for encoding_src, false for sei_src.
     * @param caps Expected caps to lock.
     * @return True when caps were locked or matched existing lock.
     */
    [[nodiscard]] bool lock_caps_contract(const bool encoding_target, const GstCaps *caps) {
        if (caps == nullptr) {
            return false;
        }

        std::lock_guard lock(caps_mutex);
        auto &locked_caps = encoding_target ? encoding_locked_caps : sei_locked_caps;
        auto &locked_flag = encoding_target ? encoding_caps_locked : sei_caps_locked;
        if (!locked_caps) {
            locked_caps.reset(gst_caps_copy(caps));
            locked_flag = (locked_caps != nullptr);
            return locked_flag.load();
        }

        return gst_caps_is_equal(locked_caps.get(), caps);
    }

    /**
     * @brief Validates runtime caps against the locked contract for one endpoint.
     * @param encoding_target True for encoding_src, false for sei_src.
     * @param current_caps Caps observed at push time.
     * @param expected_caps_text Optional expected caps string for logging.
     * @param current_caps_text Optional current caps string for logging.
     * @return True when current caps satisfy the locked contract.
     */
    [[nodiscard]] bool validate_caps_contract(const bool encoding_target, const GstCaps *current_caps,
                                              std::string *expected_caps_text, std::string *current_caps_text) const {
        if (current_caps_text != nullptr) {
            current_caps_text->clear();
            if (current_caps != nullptr) {
                sf::unique_g_char_ptr caps_string(gst_caps_to_string(current_caps));
                *current_caps_text = caps_string ? caps_string.get() : "null";
            } else {
                *current_caps_text = "null";
            }
        }

        std::lock_guard lock(caps_mutex);
        const auto &locked_caps = encoding_target ? encoding_locked_caps : sei_locked_caps;
        const bool locked = encoding_target ? encoding_caps_locked.load() : sei_caps_locked.load();
        if (expected_caps_text != nullptr) {
            expected_caps_text->clear();
            if (locked_caps != nullptr) {
                sf::unique_g_char_ptr caps_string(gst_caps_to_string(locked_caps.get()));
                *expected_caps_text = caps_string ? caps_string.get() : "null";
            } else {
                *expected_caps_text = "null";
            }
        }
        if (!locked || locked_caps == nullptr || current_caps == nullptr) {
            return false;
        }
        return gst_caps_is_equal(locked_caps.get(), current_caps);
    }
};
