#pragma once

#include "../branch_interface.h"

/**
 * @brief Create RTP output branch function table.
 * @return RTP output branch functions.
 */
output_branch_functions create_rtp_output_branch_functions();

/**
 * @brief Create RTP output branch context.
 * @return RTP output branch context.
 */
output_branch_context create_rtp_output_branch_context();
