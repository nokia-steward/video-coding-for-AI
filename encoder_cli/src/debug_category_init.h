// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

/**
 * @file debug_category_init.height
 * @brief Declares the function to initialize all debug categories for the application.
 */

/**
 * @brief Initialize all debug categories for the application.
 *
 * This function initializes all GStreamer debug categories used throughout
 * the encoder_cli application. It should be called early in the application
 * startup, after gst_init() but before any logging operations.
 */
void initialize_debug_categories();
