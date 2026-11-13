# Chapter 10 — Build, run, and configuration

## Build and run (from encoder_cli README)

Excerpt: **build.sh** and **run.sh** reference (committed source).

--8<-- "encoder_cli/README.md:23:106"

## Configuration file

Default config path: [**encoder_config.ini**](../../encoder_cli/encoder_config.ini) (in [**encoder_cli/**](../../encoder_cli/)). Override with `--config` after `--` in [**run.sh**](../../encoder_cli/run.sh).

**Config reference (manual):**

- [encoder_config overview](../config/encoder_config-overview.md) — Common, Output, Encoder
- [Video / Camera / UDP](../config/encoder_config-video-udp.md)
- [ROI](../config/encoder_config-roi.md)
- [Tracker / Performance](../config/encoder_config-tracker-performance.md)
- [Detection / Stabilization](../config/encoder_config-detection-stabilization.md)
- [Debug / Logging](../config/encoder_config-debug-logging.md)
- [Communication / WebRTC](../config/encoder_config-communication-webrtc.md)

Full CLI option tables remain in **Chapter 9** (encoder_cli README include).
