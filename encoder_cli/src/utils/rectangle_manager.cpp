// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "rectangle_manager.h"

/**
 * @file rectangle_manager.cpp
 * @brief Implementation of the bouncing rectangle simulation.
 */

rectangle_manager::rectangle_manager(const int width, const int height) :
    screen_width(width), screen_height(height), rectangles(3), last_timestamp(0), initialized(false),
    random_generator(std::random_device{}()) {}

const std::vector<bouncing_rectangle> &rectangle_manager::get_updated_rectangles(const uint64_t timestamp) {
    if (!initialized) {
        last_timestamp = timestamp;
        initialize_rectangles();
        initialized = true;
    }

    const auto delta_time = static_cast<float>(timestamp - last_timestamp) / 1'000'000'000.0f;
    last_timestamp = timestamp;

    update_positions(delta_time);

    return rectangles;
}

/**
 * @brief Initializes the rectangles with random positions, sizes, and velocities.
 */
void rectangle_manager::initialize_rectangles() {
    auto vel_dist = std::uniform_real_distribution(-150.0f, 150.0f);
    auto size_dist = std::uniform_int_distribution(50, 250);

    for (auto &rect: rectangles) {
        rect.geometry.width = size_dist(random_generator) * 2;
        rect.geometry.height = size_dist(random_generator) * 2;

        auto horizontal_position_dist =
                std::uniform_real_distribution(0.0f, static_cast<float>(screen_width - rect.geometry.width));
        auto vertical_position_dist =
                std::uniform_real_distribution(0.0f, static_cast<float>(screen_height - rect.geometry.height));

        rect.geometry.left = static_cast<int>(horizontal_position_dist(random_generator)) & ~1;
        rect.geometry.top = static_cast<int>(vertical_position_dist(random_generator)) & ~1;
        rect.velocity = {vel_dist(random_generator), vel_dist(random_generator)};
    }
}

/**
 * @brief Updates the positions of the rectangles based on their velocity and the elapsed time.
 * @param delta_time The time elapsed since the last update in seconds.
 */
void rectangle_manager::update_positions(const float delta_time) {
    for (auto &rect: rectangles) {
        rect.geometry.left += static_cast<int>(rect.velocity.horizontal_axis * delta_time);
        rect.geometry.top += static_cast<int>(rect.velocity.vertical_axis * delta_time);

        rect.geometry.left &= ~1;
        rect.geometry.top &= ~1;

        if (rect.geometry.left <= 0) {
            rect.geometry.left = 0;
            rect.velocity.horizontal_axis *= -1;
        } else if (rect.geometry.left + rect.geometry.width >= screen_width) {
            rect.geometry.left = (screen_width - rect.geometry.width) & ~1;
            rect.velocity.horizontal_axis *= -1;
        }

        if (rect.geometry.top <= 0) {
            rect.geometry.top = 0;
            rect.velocity.vertical_axis *= -1;
        } else if (rect.geometry.top + rect.geometry.height >= screen_height) {
            rect.geometry.top = (screen_height - rect.geometry.height) & ~1;
            rect.velocity.vertical_axis *= -1;
        }
    }
}
