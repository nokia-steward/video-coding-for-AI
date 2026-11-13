// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "gst_raii.h"
#include "low_delay_packer.h"
#include "packer_common.h"
#include "random_access_packer.h"
#include "rectangle.h"

/**
 * @file pri_packer_view.height
 * @brief Non-owning view over the PRI packer, exposing only the packer slice.
 *
 * The PRI rectangle packers (low_delay_packer and random_access_packer) are stored
 * together in the aggregate as a `pri_packer_storage` variant. This view presents
 * that storage through a small, typed surface so a consumer can drive the active
 * packer without reaching into the variant or the whole aggregate. It is non-owning:
 * the aggregate still owns the packers.
 */

/**
 * @brief Storage for the PRI packer (one of low-delay or random-access packing).
 */
using pri_packer_storage = std::variant<std::unique_ptr<low_delay_packer>, std::unique_ptr<random_access_packer>>;

/**
 * @brief A non-owning view over the PRI packer.
 *
 * Exposes exactly the packer operations the output pipeline consumes, dispatching
 * to the active low-delay or random-access packer. Operations that do not apply to
 * the active packer are safe no-ops (returning std::nullopt where a result is
 * expected), mirroring the existing call-site behavior.
 */
struct pri_packer_view {
    /**
     * @brief Construct a view over an existing packer storage.
     * @param storage The packer storage owned by the aggregate. Must outlive the view.
     */
    explicit pri_packer_view(pri_packer_storage &storage) : packer(storage) {}

    pri_packer_view(const pri_packer_view &) = delete;
    pri_packer_view &operator=(const pri_packer_view &) = delete;

    /// True when the active packer is a low_delay_packer.
    [[nodiscard]] bool is_low_delay() const;

    /// True when the active packer is a random_access_packer.
    [[nodiscard]] bool is_random_access() const;

    /// True when a random_access_packer is present and its pointer is non-null (actively usable).
    [[nodiscard]] bool has_active_random_access() const;

    /**
     * @brief Pack rectangles through the low_delay_packer (no-op otherwise).
     * @param rectangles_with_ids Rectangle/object-ID pairs to pack.
     * @param frame_number Frame number for logging.
     * @return The packed result, or std::nullopt when the active packer is not a low_delay_packer.
     */
    [[nodiscard]] std::optional<pack_result>
    pack_low_delay(const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
                   int frame_number = -1);

    /**
     * @brief Feed a frame to the random_access_packer (no-op otherwise).
     * @param frame_number The frame number.
     * @param sample The frame sample; the packer takes ownership.
     * @param rectangles_with_ids Source ROI rectangles with object IDs for this frame.
     * @param packing_rectangles_with_ids Optional packing-placement rectangles.
     */
    void add_random_access_frame(int frame_number, sf::unique_gst_sample_ptr sample,
                                 const std::vector<std::pair<sf::rectangle, std::string>> &rectangles_with_ids,
                                 const std::optional<std::vector<std::pair<sf::rectangle, std::string>>>
                                         &packing_rectangles_with_ids = std::nullopt);

    /**
     * @brief Get the next complete period from the random_access_packer.
     * @return Optional complete period; std::nullopt when the active packer is not a
     *         random_access_packer (or the packer is closed and drained).
     */
    [[nodiscard]] std::optional<random_access_packer::complete_period> get_random_access_period();

    /**
     * @brief Close the random_access_packer (no-op otherwise), signaling no more frames.
     */
    void close_random_access();

private:
    pri_packer_storage &packer;
};
