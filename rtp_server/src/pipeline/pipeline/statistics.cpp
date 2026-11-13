// This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

#include "statistics.h"
#include "../pipeline.h"
#include "logging.h"

// Debug category for pipeline-level logging (defined elsewhere).
GST_DEBUG_CATEGORY_EXTERN(pipeline_category);

void log_pipeline_statistics(const rtp_pipeline &pipeline) {
    const auto &stats = pipeline.stats_context.statistics;
    sf::info(pipeline_category, "RTP Input Layer:");
    sf::info(pipeline_category, "  Packets Received: %lu", stats.rtp_input_packets_received.load());
    sf::info(pipeline_category, "  Bytes Received: %lu", stats.rtp_input_bytes_received.load());
    sf::info(pipeline_category, "RTP Layer:");
    sf::info(pipeline_category, "  Packets Depayloaded: %lu", stats.rtp_packets_depayloaded.load());
    sf::info(pipeline_category, "H.265 Layer:");
    sf::info(pipeline_category, "  Frames Parsed: %lu", stats.h265_frames_parsed.load());
    sf::info(pipeline_category, "  Bytes Parsed: %lu", stats.h265_bytes_parsed.load());
    sf::info(pipeline_category, "Encoded stream (post byte-stream capsfilter, via probes):");
    sf::info(pipeline_category, "  Samples observed: %lu", stats.appsink_samples_received.load());
    sf::info(pipeline_category, "  Bytes observed: %lu", stats.appsink_bytes_received.load());
    sf::info(pipeline_category, "  (Legacy appsrc counter mirrors same stream: %lu samples, %lu bytes)",
             stats.appsrc_samples_pushed.load(), stats.appsrc_bytes_pushed.load());
    sf::info(pipeline_category, "Decoded Layer (post-TET / display path via probes):");
    sf::info(pipeline_category, "  Frames Received: %lu", stats.decoded_frames_received.load());
    sf::info(pipeline_category, "  Bytes Received: %lu", stats.decoded_bytes_received.load());
    sf::info(pipeline_category, "  Display-path samples: %lu", stats.decoded_appsrc_samples_pushed.load());
    sf::info(pipeline_category, "  Display-path bytes: %lu", stats.decoded_appsrc_bytes_pushed.load());
    sf::info(pipeline_category, "Memory-Domain / Mutability:");
    sf::info(pipeline_category, "  GPU->CPU Sample Conversions: %lu", stats.gpu_to_cpu_sample_conversions.load());
    sf::info(pipeline_category, "  Writable Buffer Conversions: %lu", stats.writable_buffer_conversions.load());
    sf::info(pipeline_category, "  CPU Mutation Stage Failures: %lu", stats.cpu_mutation_stage_failures.load());
    sf::info(pipeline_category, "Output Layer:");
    sf::info(pipeline_category, "  Display Frames Sent: %lu", stats.display_frames_sent.load());
    sf::info(pipeline_category, "  File Bytes Written: %lu", stats.file_bytes_written.load());
    if (pipeline.rtp_output.enabled) {
        sf::info(pipeline_category, "  RTP Output Packets Sent: %lu", stats.rtp_output_packets_sent.load());
        sf::info(pipeline_category, "  RTP Output Bytes Sent: %lu", stats.rtp_output_bytes_sent.load());
        sf::info(pipeline_category, "  RTP Output Destination: %s:%u", pipeline.rtp_output.host.c_str(),
                 pipeline.rtp_output.port);
    }

    guint64 encoded_bytes = stats.h265_bytes_parsed.load();
    guint64 encoded_frames = stats.h265_frames_parsed.load();
    if (encoded_frames == 0) {
        encoded_bytes = stats.appsink_bytes_received.load();
        encoded_frames = stats.appsink_samples_received.load();
    }
    if (encoded_frames > 0 && stats.framerate_numerator > 0 && stats.framerate_denominator > 0) {
        const double fps =
                static_cast<double>(stats.framerate_numerator) / static_cast<double>(stats.framerate_denominator);
        const double duration_s = static_cast<double>(encoded_frames) / fps;
        const guint64 bitrate_bps =
                duration_s > 0.0 ? static_cast<guint64>(static_cast<double>(encoded_bytes) * 8.0 / duration_s) : 0;
        sf::info(pipeline_category,
                 "Encoded bitrate summary: h265_bytes=%lu h265_frames=%lu framerate=%.2f"
                 " duration_s=%.3f bitrate_bps=%lu",
                 encoded_bytes, encoded_frames, fps, duration_s, bitrate_bps);
    }
}

void log_periodic_statistics(const rtp_pipeline &pipeline) {
    const auto &stats = pipeline.stats_context.statistics;

    if (pipeline.rtp_output.enabled) {
        sf::info(pipeline_category,
                 "Port %u Status: RTPIn:%lu RTP:%lu H265:%lu Enc:%lu Decoded:%lu Display:%lu File:%lu "
                 "RTPOut:%lu MemConv:%lu Writable:%lu MutFail:%lu",
                 pipeline.port, stats.rtp_input_packets_received.load(), stats.rtp_packets_depayloaded.load(),
                 stats.h265_frames_parsed.load(), stats.appsink_samples_received.load(),
                 stats.decoded_frames_received.load(), stats.display_frames_sent.load(),
                 stats.file_bytes_written.load(), stats.rtp_output_packets_sent.load(),
                 stats.gpu_to_cpu_sample_conversions.load(), stats.writable_buffer_conversions.load(),
                 stats.cpu_mutation_stage_failures.load());
    } else {
        sf::info(pipeline_category,
                 "Port %u Status: RTPIn:%lu RTP:%lu H265:%lu Enc:%lu Decoded:%lu Display:%lu File:%lu "
                 "MemConv:%lu Writable:%lu MutFail:%lu",
                 pipeline.port, stats.rtp_input_packets_received.load(), stats.rtp_packets_depayloaded.load(),
                 stats.h265_frames_parsed.load(), stats.appsink_samples_received.load(),
                 stats.decoded_frames_received.load(), stats.display_frames_sent.load(),
                 stats.file_bytes_written.load(), stats.gpu_to_cpu_sample_conversions.load(),
                 stats.writable_buffer_conversions.load(), stats.cpu_mutation_stage_failures.load());
    }
}
