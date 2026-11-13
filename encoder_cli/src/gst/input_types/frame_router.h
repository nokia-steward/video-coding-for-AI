// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include <atomic>
#include <mutex>

/**
 * @file frame_router.height
 * @brief Declares the frame router for controlling frame routing in dual-path bypass mode.
 */

/**
 * @brief Forward declaration for the bypass runtime slice.
 */
struct bypass_state;

/**
 * @brief Routing decision for a frame.
 */
enum class routing_decision {
    inference, ///< Send frame through DeepStream inference path
    bypass ///< Send frame through fast bypass path
};

/**
 * @brief State and logic for routing frames between inference and bypass paths.
 *
 * The frame router implements interval-based routing: every Nth frame is
 * routed to the bypass path while the rest go through inference.
 */
struct frame_router {
    /**
     * @brief Constructs a frame router with the given bypass interval.
     *
     * @param bypass_interval Every Nth frame bypasses inference.
     *                        0 = bypass disabled (all frames to inference).
     *                        2 = every 2nd frame bypasses (50% bypass).
     *                        3 = every 3rd frame bypasses (33% bypass).
     */
    explicit frame_router(int bypass_interval);

    /**
     * @brief Decides which path a frame should take.
     *
     * This is called for every frame that passes through the tee element.
     * The decision is based on the configured bypass_interval and the
     * current frame count.
     *
     * @return The routing decision (inference or bypass).
     */
    [[nodiscard]] routing_decision decide_route();

    /**
     * @brief Gets the current bypass interval.
     *
     * @return The bypass interval (0 = disabled).
     */
    [[nodiscard]] int get_bypass_interval() const;

    /**
     * @brief Gets the number of frames routed to inference.
     */
    [[nodiscard]] guint64 get_inference_frame_count() const;

    /**
     * @brief Gets the number of frames routed to bypass.
     */
    [[nodiscard]] guint64 get_bypass_frame_count() const;

    /**
     * @brief Resets the frame counter.
     *
     * Called during pipeline reset or for testing.
     */
    void reset();

private:
    int bypass_interval;
    std::atomic<guint64> frame_counter{0};
    std::atomic<guint64> inference_count{0};
    std::atomic<guint64> bypass_count{0};
};

/**
 * @brief Context passed to the routing probe callback.
 */
struct routing_probe_context {
    bypass_state *bypass = nullptr;
    frame_router *router = nullptr;
};

/**
 * @brief Pad probe callback for routing frames at the tee element.
 *
 * This probe is attached to the tee's sink pad. For each buffer, it:
 * 1. Calls the frame router to decide the path
 * 2. Sets the valve elements accordingly
 * 3. Records the routing decision for the frame merger
 *
 * @param pad The pad the probe is attached to.
 * @param info Information about the probe event.
 * @param user_data Pointer to routing_probe_context.
 * @return GST_PAD_PROBE_OK to continue processing.
 */
GstPadProbeReturn routing_probe_callback(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);

/**
 * @brief Installs the routing probe on the tee element.
 *
 * @param tee The tee element to attach the probe to.
 * @param context The routing probe context.
 * @return The probe ID, or 0 on failure.
 */
gulong install_routing_probe(GstElement *tee, routing_probe_context *context);
