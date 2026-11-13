// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#pragma once

/**
 * @file ctu_alignment.height
 * @brief Re-exports shared CTU alignment utilities for backward compatibility.
 *
 * The actual implementation lives in encoder_cli/src/roi/ctu_alignment.height.
 * This header exists so that existing low_delay_packer includes continue to work.
 */

#include "../ctu_alignment.h"
