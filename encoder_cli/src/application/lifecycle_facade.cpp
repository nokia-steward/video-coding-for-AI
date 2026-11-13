// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "lifecycle_facade.h"

#include "../global_data.h"

lifecycle_facade::lifecycle_facade(global_data &data) :
    params(data.params), monitoring(data.monitoring), input_pipeline(data.input_pipeline),
    encoding_pipeline(data.encoding_pipeline), output_pipeline(data.output_pipeline),
    backend(data.active_encoder_backend), pgie_class_ids_to_filter(data.pgie_class_ids_to_filter),
    stop_processing(data.stop_processing), shutdown_started(data.shutdown_started),
    num_input_frames(data.num_input_frames), h265_filesink(data.h265_filesink), data_(data) {}

global_data &lifecycle_facade::instance() const { return data_; }

void lifecycle_facade::shutdown() { data_.shutdown(); }

void lifecycle_facade::reset_processing_state() { data_.reset_processing_state(); }

bool lifecycle_facade::request_worker_stop_once(bool close_metadata_queues) {
    return data_.request_worker_stop_once(close_metadata_queues);
}
