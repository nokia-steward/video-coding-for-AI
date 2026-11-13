# Troubleshooting and FAQ

## [INSTALL.md](../../INSTALL.md) — Troubleshooting

--8<-- "INSTALL.md:163:212"

## FAQ

**Why does ROI packing conflict with background_blur / qp_map_delta?**  
Non-off background modes change pixels outside ROI; packing assumes a single consistent reconstruction path. See **Part IX — Feature options matrix**.

**Why does qp_map_delta fail on non-x86 systems?**  
`background_mode=qp_map_delta` is supported only on x86 desktop systems; encoder_cli exits at startup with a validation error on non-x86. Use `background_blur` or `off`. See **[FEATURE_OPTIONS_INSTRUCTIONS.md](../../FEATURE_OPTIONS_INSTRUCTIONS.md)** (Part IX).

**Why does qp_map_delta still fall back to background_blur on x86?**  
On x86, runtime encoder capability checks can still fail (for example unsupported encoder path), and encoder_cli logs a fallback reason. See **[FEATURE_OPTIONS_INSTRUCTIONS.md](../../FEATURE_OPTIONS_INSTRUCTIONS.md)** (Part IX).

**Which GStreamer is expected?**  
Both applications use GStreamer. See [**INSTALL.md**](../../INSTALL.md) for package and bootstrap steps.

**Kvazaar plugin not found at runtime**  
`encoder_cli/run.sh` uses standalone installs from `lib/kvazaar` and `lib/openhevc` (single-plugin dirs). Verify with `gst-inspect-1.0` and `GST_DEBUG=3`.

**Where are config options documented?**  
[**encoder_cli/encoder_config.ini**](../../encoder_cli/encoder_config.ini) and [**rtp_server/rtp_server_config.ini**](../../rtp_server/rtp_server_config.ini) are in the manual under **Config reference** (encoder_cli and rtp_server config pages).

**Apptainer vs Docker**  
Apptainer uses [**dev.def**](../../dev.def) (similar package list to Dockerfile_base); see **Chapter 27** and [**APPTAINER_DEV_ENV.md**](../../APPTAINER_DEV_ENV.md) in repo.

**Why are there two `*_vcai` shared libraries?**  
Same sources, different object sets: encoder uses COMMON only; rtp_server adds `h265_sps_parser` for SPS/PPS/QP parsing on receive. See **Chapter 23**.

**How does PRI get from encoder to receiver?**  
Packed into a prefix SEI NAL and injected into the H.265 access unit; rtp_server’s pipeline parses it after depay. See **PRI and EOI SEI messages** and **Chapter 17**.

**Where is the high-level dependency graph documented?**  
**Chapter 23** (build-time and runtime dependencies) and **Chapter 13** / **Encoder dataflow** for encoder package flow.
