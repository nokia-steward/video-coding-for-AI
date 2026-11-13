# Chapter 11 — encoder_cli CLI reference

## Quick entry

```bash
./build/encoder_cli/encoder_cli --help   # after encoder_cli/build.sh
./run.sh -- --help                       # from encoder_cli/, see encoder_cli/run.sh
```

## Option categories (in Chapter 9)

The encoder_cli README in **Chapter 9** contains exhaustive tables. Categories:

| Category | README section | Typical use |
|----------|----------------|-------------|
| Common / Config | `--config`, `--common-input-type`, `--output-directory` | Always need `--config`; run.sh sets it |
| Video / Camera / UDP | `--video-file-name`, `--width`, `--udp-port`, … | File/camera/UDP input sizing and RTP depay |
| ROI / Detection / Tracking | `--roi-pack-rois`, `--roi-background-mode`, `--tracker-type`, … | PRI packing vs blur/QP-map; tracker selection |
| Encoder / Debug | `--encoder-element`, `--encoder-qp`, `--debug-save-frames`, … | Encoder factory and overlays |
| Communication / WebRTC | `--communication-transport-type`, `--webrtc-signaling-url`, … | RTP vs WebRTC destination |

## Interaction notes (summary)

- `--config` required; run.sh prepends `encoder_config.ini`—pass-through `--config` after wrapper args wins by parser order.
- `qp_map_delta` is supported only on x86 desktop systems; non-x86 startup fails with a validation error.
- On x86 systems, unsupported runtime encoder paths can still fall back to `background_blur`; see **Part IX — Feature options**.
- Boolean-string options: `--roi-pack-rois true|false`, `--stabilization-enabled true|false`.

Open **Chapter 9** and search within the built manual for the full tables, or read [**encoder_cli/README.md**](../../encoder_cli/README.md) in the repo.
