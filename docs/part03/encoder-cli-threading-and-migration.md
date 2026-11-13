# encoder_cli threading, EOS, and plugin-migration notes

This page supports Phase A work toward moving appsrc/appsink bridges into project-owned elements: it
inventories application threads and documents new structural boundaries (`encoded_stream_ports`,
`encoder_cli_app_host`, encoding pipeline split).

## Encoded egress handles (`encoded_stream_ports`)

`global_data::encoded_ports` groups non-owning `GstElement*` pointers for the encoding and SEI appsrc
endpoints. Bridge, encoder backend, and pipeline code should use `data.encoded_ports.encoding_src` and
`data.encoded_ports.sei_src` instead of scattering raw `encoding_src` / `sei_src` fields on
`global_data`.

## Bridge app host (`encoder_cli_app_host`)

Bridge adapters (`ingress_app_adapter`, `pipeline_app_adapter`, `encoder_app_adapter`,
`random_access_app_adapter`) depend on the abstract `encoder_cli_app_host` interface. `global_data`
implements this interface; tests and future elements can supply alternate implementations without
pulling the full `global_data` type into adapter headers.

## Encoding pipeline module split

`create_encoding_pipeline()` lives in `src/gst/input_pipeline_encoding.cpp` beside
`src/gst/input_pipeline.cpp`, sharing the `input_pipeline` GStreamer debug category via
`input_pipeline_debug.h`. Input decoding probes and mode selection remain in `input_pipeline.cpp`.

## `roi_input` layout (split + per-type branch factories)

The former monolith `gst/input_types/roi_input.cpp` lives under `gst/input_types/roi_input/`; the public API
remains in `gst/input_types/roi_input.h` (`create_deepstream_elements`, `configure_*`, `link_*`,
`setup_pts_preservation_probes`, `create_roi_*_input_pipeline`, etc.). **No pipeline graph or property
changes** were made in the structural passes.

| File | Role |
|------|------|
| `roi_input/roi_input_debug.cpp` | `GST_DEBUG_CATEGORY(roi_input)` and `init_roi_input_debug_category()` |
| `roi_input/roi_input_deepstream_factory.cpp` | DeepStream element create/configure, tracker config file helper |
| `roi_input/roi_input_deepstream_link.cpp` | `link_deepstream_elements`, `add_deepstream_elements_to_pipeline`, inference performance probes |
| `roi_input/roi_input_pts_probes.cpp` | PTS preservation probes and queue sizing helper |
| `roi_input/roi_input_branch_factories.cpp` | Per-`input_type` branch factories (file / camera / UDP): create, add-to-bin, link to `nvvidconv`; UDP post-link helpers |
| `roi_input/roi_input_pipelines.cpp` | `create_roi_info_input_pipeline`, `create_roi_detection_input_pipeline` (orchestration + DeepStream + bypass) |
| `roi_input/roi_input_internal.h` | Internal declarations shared between split units (not part of the public header) |
| `roi_input/roi_input_pipeline_internal.h` | `roi_input_elements`, `roi_input_branch_factory`, `get_roi_input_branch_factory()` (not public API) |

**Path B progress:**

- **Bypass path:** inference and bypass `appsink` polling use **`GstTask`** (`start_appsink_poll_gst_task` /
  `join_appsink_poll_gst_task` in `gst/input_types/appsink_polling.cpp`). The **merger consumer** also uses **`GstTask`**
  (`start_merger_poll_gst_task` / `join_merger_poll_gst_task`); `frame_merger::request_consumer_shutdown()` wakes
  `get_next_frame()` waits so the task can stop/join cleanly.
- **Pipeline bus workers:** `process_pipeline_messages` kept as a blocking helper; live runs use
  `start_pipeline_message_gst_task` / `join_pipeline_message_gst_task` (`processing/pipeline_message_processing.cpp`)
  for input/output pipelines (`worker_thread_bundle::input_message_slot` / `output_message_slot`).
- **Encoded frames:** `process_encoded_frames` remains the blocking reference loop; the main path uses
  `start_encoded_frames_gst_task` / `join_encoded_frames_gst_task` (`processing/encoded_frame_processing.cpp`) with
  `worker_thread_bundle::encoded_frame_slot`.
- **ROI detection:** `roi_detection_frame_processing` remains the blocking reference loop; the main path uses
  `start_roi_detection_gst_task` / `join_roi_detection_gst_task` (`processing/roi_detection_processing/api.cpp`) with
  `worker_thread_bundle::roi_detection_slot`.
- **Random-access packer path:** `random_access_frame_processing` remains the blocking reference loop; the main path uses
  `start_random_access_gst_task` / `join_random_access_gst_task` (`processing/roi_detection_random_access.cpp`) with
  `worker_thread_bundle::random_access_slot` when `pack_rois` uses `random_access_packer`.
- **File-mode idle watchdog:** `start_file_mode_watchdog_gst_task` / `join_file_mode_watchdog_gst_task`
  (`application/app_periodic_gst_tasks.cpp`) using `global_data::file_mode_watchdog_slot` (one GstTask tick per second of
  wall time, chunked `g_usleep` for stop responsiveness).
- **Periodic status / performance reporting:** `simple_status_reporter::start_periodic_reporting` /
  `performance_monitor::start_periodic_reporting` only **arm** atomics and UDP wait logic; a shared
  `start_app_periodic_reporting_gst_task` drives ticks via `global_data::app_periodic_reporting_slot`. Modes call
  `join_app_periodic_reporting_gst_task` during shutdown; `global_data::shutdown()` / `~global_data` join defensively.

`start_worker_threads` returns **`std::unique_ptr<worker_thread_bundle>`** so `bypass_gst_poll_slot` mutex addresses stay
stable for GstTask locks (the struct is not movable once tasks are started).

**Still `std::jthread` (application-centric):** RTP/WebRTC heartbeat (`gst/rtp_heartbeat.cpp`) — **joinable**, owned on
`global_data`. Optional POC custom element. Call graph maps in `appsrc-appsink-application-space-report.md` remain the
guide for further extraction.

## Future element map (plugin decomposition hint)

| Notional element / concern | Current home (encoder_cli) |
|----------------------------|---------------------------|
| Bypass router + dual appsink + merger | `bypass_state`, `appsink_polling.cpp`, `frame_merger` |
| ROI detect / pack / push to encoder | `roi_detection_processing/*`, GstTask in `api.cpp` |
| Random-access period pusher | `random_access_bridge_*`, `roi_detection_random_access.cpp` GstTask |
| Encoded pull + SEI inject | `encoded_frame_processing.cpp`, bridge orchestrators |
| Pipeline bus / EOS policy | `pipeline_message_processing.cpp` GstTask |
| App telemetry (status, perf) | `app_periodic_gst_tasks.cpp` + reporters (could become properties/signals on a host bin) |

## `std::jthread` inventory (application-centric threading)

| Area | Location | Role |
|------|----------|------|
| Main processing | `application/thread_management.cpp` — `start_worker_threads` | ROI, random-access (when enabled), encoded, and message workers are **`GstTask`** slots on `worker_thread_bundle` (+ `global_data` watchdog/reporting slots) |
| Bypass path | `application/thread_management.cpp` — `start_bypass_polling_threads` | Inference/bypass/merger polling via **`GstTask`** slots on `bypass_state` |
| Pipeline messages | `application/thread_management.cpp` — `start_worker_threads` | Input/output bus dispatch via **`GstTask`** (`input_message_slot` / `output_message_slot`) |
| Bypass state | `global_data.h` — `bypass_state` | `bypass_gst_poll_slot` for inference, bypass, merger |
| File mode watchdog | `application/app_periodic_gst_tasks.cpp` | **`GstTask`** on `global_data::file_mode_watchdog_slot` |
| Reporting | `application/app_periodic_gst_tasks.cpp` + reporters | Shared **`GstTask`** on `global_data::app_periodic_reporting_slot` |
| RTP | `gst/rtp_heartbeat.cpp` | Heartbeat **`jthread`** owned by `global_data` (joinable; not detached) |

## Target element model (design direction)

For each loop above, a future element-based design should map work to `GstTask` or pad `chain`/`chain_list`
handlers, with EOS and flush expressed as `GST_EVENT_EOS` / `GST_EVENT_FLUSH_*` on the relevant pads.
`random_access_bridge_core` uses **`g_usleep`** for short pacing/yield in the random-access path (GstTask-friendly);
longer pacing still maps to element scheduling or queue limits in a full element design.

No runtime behavior change is implied by this document; it records migration targets for POC work.
