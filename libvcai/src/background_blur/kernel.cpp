// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "internal.h"

#include <algorithm>
#include <cmath>

namespace sf::background_blur_internal {
    int calculate_kernel_size(const double sigma) {
        const int size = static_cast<int>(std::ceil(3.0 * sigma)) * 2 + 1;
        return std::max(3, size | 1);
    }

    std::vector<double> generate_gaussian_kernel(const int kernel_size, const double sigma) {
        std::vector<double> kernel(static_cast<size_t>(kernel_size));
        const int center = kernel_size / 2;
        double sum = 0.0;

        for (int i = 0; i < kernel_size; ++i) {
            const double kernel_offset = static_cast<double>(i - center);
            kernel[static_cast<size_t>(i)] = std::exp(-(kernel_offset * kernel_offset) / (2.0 * sigma * sigma));
            sum += kernel[static_cast<size_t>(i)];
        }

        for (int i = 0; i < kernel_size; ++i) {
            kernel[static_cast<size_t>(i)] /= sum;
        }

        return kernel;
    }
} // namespace sf::background_blur_internal
