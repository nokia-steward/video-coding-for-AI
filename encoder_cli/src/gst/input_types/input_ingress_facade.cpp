// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "input_ingress_facade.h"

#include "../../global_data.h"

input_ingress_facade::input_ingress_facade(global_data &data) :
    params(data.params), input_pipeline(data.input_pipeline), ingress_runtime(data.ingress_runtime),
    monitoring(data.monitoring), udp_input_runtime(data.udp_input_runtime),
    udp_frame_drop_manager(data.udp_frame_drop_manager), bypass(data.bypass), memory_runtime(data.memory_runtime),
    udp_bitrate_tracker(data.udp_bitrate_tracker), stop_processing(data.stop_processing),
    expected_file_input_frames(data.expected_file_input_frames),
    file_input_forced_raw_eos(data.file_input_forced_raw_eos), raw_video_parser(data.raw_video_parser), data_(data) {}

global_data &input_ingress_facade::instance() const { return data_; }

bool input_ingress_facade::should_process_raw_sample() const { return data_.ing_should_process_raw_sample(); }
