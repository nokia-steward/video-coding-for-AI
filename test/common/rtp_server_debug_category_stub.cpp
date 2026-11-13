// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include <gst/gst.h>

// ponytail: libvcai tests in test_rtp_server call these inits via
// test_debug_categories*.h. Production rtp_server sources that define them
// are omitted without DeepStream; upgrade by compiling those sources when
// DEEPSTREAM_PATH is set.
GST_DEBUG_CATEGORY_STATIC(server_category);
GST_DEBUG_CATEGORY_STATIC(client_manager_category);
GST_DEBUG_CATEGORY_STATIC(pipeline_category);
GST_DEBUG_CATEGORY_STATIC(gpu_elements_debug);
GST_DEBUG_CATEGORY_STATIC(configuration_manager_category);

void init_server_debug_category() { GST_DEBUG_CATEGORY_INIT(server_category, "server", 0, "Server"); }

void init_client_manager_debug_category() {
    GST_DEBUG_CATEGORY_INIT(client_manager_category, "client-manager", 0, "Client Connection and Pipeline Management");
}

void init_pipeline_debug_category() { GST_DEBUG_CATEGORY_INIT(pipeline_category, "pipeline", 0, "Gstreamer pipeline"); }

void init_gpu_elements_debug_category() {
    GST_DEBUG_CATEGORY_INIT(gpu_elements_debug, "gpu-elements", 0, "GPU encoder/decoder element helpers");
}

void init_configuration_manager_debug_category() {
    GST_DEBUG_CATEGORY_INIT(configuration_manager_category, "configuration-manager", 0, "Configuration Management");
}
