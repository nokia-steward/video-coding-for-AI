// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

#include "../../../libvcai/include/gst_raii.h"
#include "../app/ingress_app_adapter.h"
#include "../context/ingress_bridge_context.h"
#include "../core/ingress_bridge_core.h"

/**
 * @file ingress_bridge_orchestrator.h
 * @brief Orchestrates ingress appsink pulls with app-side queue and lifecycle policy.
 */

/**
 * @brief Handles raw-input appsink callback via ingress bridge policy.
 * @param context Ingress bridge context for current encoder instance.
 * @param app_adapter App-side adapter.
 * @param sink Appsink that emitted the callback.
 * @param category Debug category for logging.
 * @return GStreamer flow result expected by callback contract.
 */
[[nodiscard]] GstFlowReturn ingress_handle_raw_new_sample_via_orchestrator(ingress_bridge_context &context,
                                                                           const ingress_app_adapter &app_adapter,
                                                                           GstElement *sink,
                                                                           GstDebugCategory *category);

/**
 * @brief Handles raw-input EOS callback via ingress bridge policy.
 * @param context Ingress bridge context for current encoder instance.
 * @param app_adapter App-side adapter.
 * @param category Debug category for logging.
 */
void ingress_handle_raw_eos_via_orchestrator(ingress_bridge_context &context, const ingress_app_adapter &app_adapter,
                                             GstDebugCategory *category);

/**
 * @brief Handles encoded appsink callback via ingress bridge policy.
 * @param context Ingress bridge context for current encoder instance.
 * @param app_adapter App-side adapter.
 * @param sink Appsink that emitted the callback.
 * @param category Debug category for logging.
 * @return GStreamer flow result expected by callback contract.
 */
[[nodiscard]] GstFlowReturn ingress_handle_encoded_new_sample_via_orchestrator(ingress_bridge_context &context,
                                                                               const ingress_app_adapter &app_adapter,
                                                                               GstElement *sink,
                                                                               GstDebugCategory *category);

/**
 * @brief Handles encoded appsink EOS callback via ingress bridge policy.
 * @param context Ingress bridge context for current encoder instance.
 * @param app_adapter App-side adapter.
 * @param category Debug category for logging.
 */
void ingress_handle_encoded_eos_via_orchestrator(ingress_bridge_context &context,
                                                 const ingress_app_adapter &app_adapter, GstDebugCategory *category);

/**
 * @brief Tries to pull one sample from appsink with fail-fast policy on fatal errors.
 * @param context Ingress bridge context for current encoder instance.
 * @param app_adapter App-side adapter.
 * @param appsink Appsink element.
 * @param timeout Timeout in nanoseconds.
 * @return Pull result status and optional sample.
 */
[[nodiscard]] ingress_bridge_pull_result
ingress_try_pull_sample_via_orchestrator(ingress_bridge_context &context, const ingress_app_adapter &app_adapter,
                                         GstElement *appsink, GstClockTime timeout);

/**
 * @brief Enqueues a merged raw sample with ingress queue policy.
 * @param context Ingress bridge context for current encoder instance.
 * @param app_adapter App-side adapter.
 * @param sample Sample to enqueue.
 * @param category Debug category for logging.
 * @return True when sample was enqueued, false when intentionally dropped.
 */
[[nodiscard]] bool ingress_enqueue_merged_sample_via_orchestrator(ingress_bridge_context &context,
                                                                  const ingress_app_adapter &app_adapter,
                                                                  sf::unique_gst_sample_ptr sample,
                                                                  frame_path_type path, GstDebugCategory *category);
