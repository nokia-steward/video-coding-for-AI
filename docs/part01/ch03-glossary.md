# Chapter 3 — Glossary

Consolidated terminology used across VCAI documentation.

| Term | Meaning |
|------|--------|
| **VCAI** | Video Coding for AI Tasks — project and product name |
| **PRI** | Packed Regions Information — H.274-style SEI payload carrying ROI/region semantics |
| **SEI** | Supplemental Enhancement Information — NAL units in H.265 carrying metadata |
| **EOI** | Encoder Optimization Information — SEI used for luma adaptation signaling |
| **TET** | Tile-based enhancement — receiver-side post-processing in rtp_server |
| **encoder_cli** | Encoder application: ingest → detection → pack/inject SEI → RTP/WebRTC/file |
| **rtp_server** | Receiver application: handshake → RTP/WebRTC → decode → PRI/EOI → overlay/display |
| **libvcai** | Shared C++20 library: bitstream, nal_utils, communication, ROI helpers |
| **vcai_decoder** | GStreamer plugin (`libgstvcai_decoder.so`): H.265 SEI extract, PRI reconstruct, EOI luma, TET |
| **low_delay / random_access** | ROI packer types (`--roi-packer-type`) |
| **background_blur / qp_map_delta** | ROI background modes (mutually exclusive with ROI packing when non-off) |
| **Kvazaar / gst-kvazaar** | HEVC encoder and GStreamer plugin; optional local install via `bootstrap.sh kvazaar` (e.g. `lib/kvazaar`) |
| **openhevcdec** | Software H.265 decoder plugin (gst-openhevc) for rtp_server optional path |
| **BD-rate** | Bjontegaard delta rate; metric for comparing rate-distortion curves (e.g. in TET evaluation) |
| **COCO** | Common Objects in Context; bbox format used for test-mode groundtruth |
| **PandaSet** | Dataset used in TET/semseg evaluation and YUV generation |
| **IRAP** / **IDR** | Intra random access point; keyframes where PRI/EOI and resets are relevant |
| **NAL** | Network Abstraction Layer unit; H.265 bitstream is organized in NAL units (e.g. VPS, SPS, PPS, SEI, slice) |
| **access unit** | Set of NAL units for one decoded picture; prefix SEI is associated with the following slice in the same AU |

See also **Part IX — Feature options matrix** for compatibility between options.
