// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "../../encoder_cli/src/gst/input_types/roi_input.h"
#include "../../encoder_cli/src/processing/roi_detection_extraction.h"

#include <vector>

// ponytail: test-only stand-ins when DeepStream headers are absent. Production
// roi_input_pipelines.cpp and roi_detection_extraction.cpp include nvds APIs;
// upgrade by compiling those files once DEEPSTREAM_PATH is available.
bool create_roi_info_input_pipeline(global_data &) { return false; }

bool create_roi_detection_input_pipeline(global_data &) { return false; }

std::vector<detection_result> get_detections_from_metadata(GstBuffer *, global_data &) { return {}; }
