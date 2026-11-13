// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "encoder_backend_view.h"

#include "../global_data.h"

bool encoder_backend_view::requires_gst_encoding_pipeline() const {
    return backend != nullptr && backend->requires_gst_encoding_pipeline();
}

bool encoder_backend_view::submit_frame(global_data &data, sf::unique_gst_buffer_ptr output_buffer,
                                        std::optional<qp_map_frame_payload> qp_map_payload) {
    if (backend == nullptr) {
        return true;
    }
    return backend->submit_frame(data, std::move(output_buffer), std::move(qp_map_payload));
}

void encoder_backend_view::signal_eos(global_data &data) {
    if (backend != nullptr) {
        backend->signal_eos(data);
    }
}
