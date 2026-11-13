// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

#include "../../include/text_drawer.h"

namespace sf {
    /**
     * @brief Returns the bitmap glyph pattern for a drawable character enum.
     * @param char_type Drawable character identifier.
     * @return Pointer to immutable glyph pattern, or nullptr if unsupported.
     */
    [[nodiscard]] const bool (
            *get_char_pattern_from_enum(drawable_character char_type))[TEXT_CHAR_HEIGHT][TEXT_CHAR_WIDTH];
} // namespace sf
