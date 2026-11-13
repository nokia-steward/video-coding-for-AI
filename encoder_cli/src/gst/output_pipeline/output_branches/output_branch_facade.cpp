// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "output_branch_facade.h"

#include "../../../global_data.h"

output_branch_facade::output_branch_facade(global_data &data) :
    params(data.params), output_pipeline(data.output_pipeline), display_regions_queue(data.display_regions_queue),
    debug_regions_queue(data.debug_regions_queue), metadata_runtime(data.metadata_runtime), monitoring(data.monitoring),
    output_display_runtime(data.output_display_runtime), output_debug_runtime(data.output_debug_runtime),
    udp_bitrate_tracker(data.udp_bitrate_tracker), assigned_rtp_port(data.assigned_rtp_port),
    assigned_rtp_session_id(data.assigned_rtp_session_id), data_(data) {}

global_data &output_branch_facade::instance() const { return data_; }

void output_branch_facade::request_metadata_contract_fail_stop(const char *reason) const {
    data_.request_metadata_contract_fail_stop(reason);
}
