// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "pri_packer_view.h"

namespace {
    template<typename T>
    T *active_packer(const pri_packer_storage &storage) {
        if (auto *packer = std::get_if<std::unique_ptr<T>>(&storage); packer && *packer) {
            return packer->get();
        }
        return nullptr;
    }
} // namespace

bool pri_packer_view::is_low_delay() const { return std::holds_alternative<std::unique_ptr<low_delay_packer>>(packer); }

bool pri_packer_view::is_random_access() const {
    return std::holds_alternative<std::unique_ptr<random_access_packer>>(packer);
}

bool pri_packer_view::has_active_random_access() const {
    return active_packer<random_access_packer>(packer) != nullptr;
}

std::optional<pack_result>
pri_packer_view::pack_low_delay(const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
                                const int frame_number) {
    if (low_delay_packer *active = active_packer<low_delay_packer>(packer)) {
        return active->get_packed_rectangles_with_ids(rectangles_with_ids, frame_number);
    }
    return std::nullopt;
}

void pri_packer_view::add_random_access_frame(
        int frame_number, sf::unique_gst_sample_ptr sample,
        const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
        const std::optional<std::vector<std::pair<sf::rectangle, std::string>>> &packing_rectangles_with_ids) {
    if (random_access_packer *active = active_packer<random_access_packer>(packer)) {
        active->add_frame(frame_number, std::move(sample), rectangles_with_ids, packing_rectangles_with_ids);
    }
}

std::optional<random_access_packer::complete_period> pri_packer_view::get_random_access_period() {
    if (random_access_packer *active = active_packer<random_access_packer>(packer)) {
        return active->get_complete_period();
    }
    return std::nullopt;
}

void pri_packer_view::close_random_access() {
    if (random_access_packer *active = active_packer<random_access_packer>(packer)) {
        active->close();
    }
}
