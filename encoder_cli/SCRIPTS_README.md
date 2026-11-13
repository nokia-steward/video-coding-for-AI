# encoder_cli Scripts

This document describes all scripts in the encoder_cli directory.

## Build Script

### build.sh

Compiles the encoder_cli application.

```bash
./build.sh [clean] [debug]
```

**Options:**

- `clean` - Remove build artifacts before building
- `debug` - Build with debug symbols

**Prerequisites checked:**

- DeepStream YOLO export environment (`.env/ds_export/`)
- DeepStream-Yolo library (`libnvdsinfer_custom_impl_Yolo.so`)

**Output:** Binary at `../build/encoder_cli/encoder_cli`

## Run Script

The run script shares common functionality:

- Automatic environment setup (GStreamer, CUDA, DeepStream paths)
- YOLO configuration generation based on video dimensions
- Video property parsing from filename patterns (file input)

### run.sh

Runs encoder_cli for all input/output combinations.

```bash
./run.sh <input_mode> <output_mode> [<input_arg>] [<output_arg>] [-- <encoder_cli_args...>]
./run.sh <input_mode> <output_mode> -o <output_arg> [-- <encoder_cli_args...>]
```

**Input modes:**

- `file` - Optional input_arg: input file path (must include WIDTHxHEIGHT and optional FPS)
- `camera` - Optional input_arg: camera device (e.g., `/dev/video0`)
- `udp` - No input_arg (uses encoder_config.ini)

**Output modes:**

- `rtp` - Optional output_arg: communication host or host:port
- `webrtc` - Optional output_arg: communication host/host:port, or signaling URL (ws://...)

**Examples:**
```bash
./run.sh file rtp /path/to/video_1920x1080_30.yuv
./run.sh udp webrtc
./run.sh camera rtp /dev/video1
./run.sh file webrtc -o ws://127.0.0.1:8080
./run.sh file rtp /path/to/video_1920x1080_30.yuv -o 192.168.1.100:5000
./run.sh udp webrtc -o 127.0.0.1:5000 -- --webrtc-signaling-url ws://127.0.0.1:8080

# Encode at constant QP 37 (instead of default CBR)
./run.sh file rtp /path/to/video_1920x1080_30.yuv -- --encoder-qp 37

# Use COCO groundtruth ROIs instead of inference (for TET evaluation)
./run.sh file rtp /path/to/video_1920x1080_30.yuv -- --roi-groundtruth-file /path/to/groundtruth.json
```

**Configuration file defaults (`encoder_config.ini`):**

While CLI parameters are optional, the underlying settings are still required. If a CLI parameter is omitted, the corresponding value must be set in `encoder_config.ini`:

| CLI parameter | Config fallback | Notes |
|---------------|-----------------|-------|
| `<input_arg>` (file path) | `[Video] file_name` | For file mode. Width/height/framerate are auto-parsed from filename if it matches `NAME_WIDTHxHEIGHT[_FPS].ext` |
| (width/height/framerate) | `[Common] width`, `height`, `framerate` | Required for file mode if not parsed from filename. Required for udp/camera modes (always read from config) |
| `<input_arg>` (camera device) | `[Camera] device` | For camera mode |
| (UDP settings) | `[UDP] bind_address`, `port`, `encoding`, etc. | For udp mode, all UDP settings come from config |
| `<output_arg>` (host/host:port) | `[Communication] host`, `port` | For rtp output mode |
| `<output_arg>` (ws:// URL) | `[WebRTC] signaling_url` | For webrtc output mode |
| `<output_arg>` (host:port for webrtc) | `[Communication] host`, `port` | For webrtc output mode (non-URL form) |

**Output file:** `encoder_output.txt`

## Utility Scripts

### show_webcam.sh

Lists available camera devices on the system.

```bash
./show_webcam.sh
```

### run_static_analysis.sh

Runs static analysis on the encoder_cli source code.

```bash
./run_static_analysis.sh
```

## Helper Scripts (scripts/ subdirectory)

These scripts are sourced by the run scripts and provide common functionality.

| Script | Purpose |
|--------|---------|
| `common_encoder_setup.sh` | Common environment setup, YOLO configuration, execution helpers |
| `input_handlers.sh` | Legacy input parsing (no longer used by `run.sh`) |
| `setup_yolo.sh` | Wrapper to compile DeepStream-Yolo library |
| `convert_yolo_model.sh` | Wrapper to convert YOLO models to ONNX |
| `generate_yolo_config.sh` | Wrapper to generate YOLO configuration files |
| `generate_peoplenet_config.sh` | Generate PeopleNet detection configuration |
| `set_cuda_ver.sh` | CUDA version detection utilities |
