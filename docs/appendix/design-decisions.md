# Design decisions (whys)

Short rationale for choices that recur in issues and config. Operational FAQs remain in **Troubleshooting and FAQ** (Part I).

## Two shared libraries from one source tree

**Decision:** `encoder_cli` links `encoder_cli_vcai` from `LIBVCAI_SOURCES_COMMON`; `rtp_server` links `rtp_server_vcai` from `LIBVCAI_SOURCES_WITH_H265` (COMMON + `h265_sps_parser.cpp`).

**Why:** The receiver needs SPS/PPS parsing for QP-related and NAL-type handling; the encoder build does not need that object file in its shared lib. Splitting keeps the encoder artifact smaller and makes the dependency explicit in CMake.

## PRI in prefix SEI

**Decision:** Pack ROI geometry into H.274-style PRI and inject as **prefix SEI** NAL units in the access unit.

**Why:** In-band metadata avoids a parallel protocol; prefix placement keeps decode and reconstruction aligned per AU. See **PRI and EOI SEI messages** (Part III).

## Output branch factory (encoder)

**Decision:** Multiple output modes (udp, webrtc, file_h265, file_mp4, display, debug) are separate branches under `gst/output_pipeline/output_branches/` with shared plumbing.

**Why:** Each branch has different caps, sinks, and probe needs; a factory keeps pipeline construction modular and avoids a single mega-pipeline that is hard to test and extend.

## WebRTC vs UDP branch

**Decision:** WebRTC is a dedicated branch, not “UDP with extra steps.”

**Why:** Signaling, ICE, and payloader setup differ from raw RTP/UDP send; isolating WebRTC avoids coupling encoder RTP timing to session lifecycle.

## readthedocs theme for the manual

**Decision:** MkDocs uses the built-in **readthedocs** theme instead of Material.

**Why:** Large nested nav and sidebar scrolling behaved poorly with Material’s drawer/scroll wrapper; readthedocs gives a conventional scrolling sidebar. See [`docs/README.md`](../../docs/README.md).

## libvcai not a standalone install target here

**Decision:** No top-level `libvcai/CMakeLists.txt` install library; subprojects compile the source list into their own `*_vcai` shared library.

**Why:** Avoids circular dependencies (lib must not depend on encoder_cli/rtp_server) and keeps each app’s link line explicit. See **Chapter 19 (Part V)** / [`libvcai/README.md`](../../libvcai/README.md).

## Related

- **Chapter 23** — CMake graphs and runtime coupling.
- **Chapter 4** — CMake and subprojects.
- **Part IX** — Feature options matrix (what conflicts with what).
