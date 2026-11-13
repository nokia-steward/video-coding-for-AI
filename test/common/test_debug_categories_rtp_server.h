// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <gst/gst.h>

/**
 * @file test_debug_categories_rtp_server.h
 * @brief Centralized initialization of GStreamer debug categories for rtp_server tests.
 *
 * This file provides a function to initialize debug categories used by rtp_server code.
 * The initialization functions are defined in the rtp_server source files that are
 * compiled as part of test_rtp_server.
 */

// Forward declarations for rtp_server debug category initialization functions
// These are defined in:
// - server.cpp: init_server_debug_category()
// - client_manager.cpp: init_client_manager_debug_category()
// - pipeline.cpp: init_pipeline_debug_category()
// - gpu_elements.cpp: init_gpu_elements_debug_category()
extern void init_server_debug_category();
extern void init_client_manager_debug_category();
extern void init_pipeline_debug_category();
extern void init_gpu_elements_debug_category();

/**
 * @brief Initialize all GStreamer debug categories used by rtp_server code.
 *
 * This function should be called once in test setup to ensure all categories
 * are initialized before any logging calls are made.
 */
inline void init_all_rtp_server_test_debug_categories() {
    // Initialize debug categories that might be used by rtp_server code
    init_server_debug_category();
    init_client_manager_debug_category();
    init_pipeline_debug_category();
    init_gpu_elements_debug_category();
}
