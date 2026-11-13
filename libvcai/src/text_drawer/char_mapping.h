// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../include/text_drawer.h"

namespace sf {
    /**
     * @brief Maps an input character to the internal drawable-character enum.
     * @param c Input character.
     * @return Drawable enum value or std::nullopt for unsupported characters.
     */
    [[nodiscard]] std::optional<drawable_character> char_to_drawable_character(char c);
} // namespace sf
