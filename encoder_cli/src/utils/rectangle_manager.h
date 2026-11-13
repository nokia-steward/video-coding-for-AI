// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <random>
#include <vector>

#include "rectangle.h"

/**
 * @file rectangle_manager.height
 * @brief Defines a simulation of moving rectangles for video content generation.
 */

/**
 * @struct vector_2d
 * @brief A simple 2D vector for position and velocity.
 */
struct vector_2d {
    float horizontal_axis = 0.0f;
    float vertical_axis = 0.0f;
};

/**
 * @struct bouncing_rectangle
 * @brief Represents a single rectangle with geometry and velocity.
 */
struct bouncing_rectangle {
    sf::rectangle geometry{};
    vector_2d velocity{};
};

/**
 * @struct rectangle_manager
 * @brief Manages the state and updates for a collection of bouncing rectangles.
 */
struct rectangle_manager {
    /**
     * @brief Constructs a rectangle_manager for a given screen size.
     * @param width The width of the screen boundary.
     * @param height The height of the screen boundary.
     */
    rectangle_manager(int width, int height);

    /**
     * @brief Updates the rectangle positions based on the elapsed time.
     * @param timestamp The current timestamp in nanoseconds.
     * @return A const reference to the vector of updated rectangles.
     */
    [[nodiscard]] const std::vector<bouncing_rectangle> &get_updated_rectangles(uint64_t timestamp);

private:
    /**
     * @brief Sets the initial random positions and velocities of the rectangles.
     */
    void initialize_rectangles();

    /**
     * @brief Updates rectangle positions and handles bouncing off walls.
     * @param delta_time The time elapsed since the last update, in seconds.
     */
    void update_positions(float delta_time);

    int screen_width;
    int screen_height;
    std::vector<bouncing_rectangle> rectangles;
    uint64_t last_timestamp;
    bool initialized;
    std::mt19937 random_generator;
};
