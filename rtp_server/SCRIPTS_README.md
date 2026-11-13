# rtp_server Script Reference

This document describes top-level scripts in `rtp_server/` and how arguments are interpreted.

For helper scripts inside `rtp_server/scripts/`, see `rtp_server/scripts/README.md`.

---

## `build.sh`

Builds the `rtp_server` target against GStreamer and also builds the `vcai_decoder` plugin target.

```bash
./build.sh [clean] [debug] [asan]
```

### Parameters

- `clean` (optional): remove `../build/rtp_server` before configure/build
- `debug` (optional): add `-DCMAKE_BUILD_TYPE=Debug`
- `asan` (optional): add `-DENABLE_ASAN=ON`

### Notes

- Argument order does not matter; flags are detected by value.
- Script exits with error if `pkg-config` cannot find `gstreamer-1.0`.
- Output binary is built via CMake target `rtp_server`.

### Examples

```bash
./build.sh
./build.sh clean
./build.sh debug asan
./build.sh clean debug
```

---

## `run.sh`

Primary launcher: configures environment, prepares model artifacts, builds (unless `NO_BUILD=true`), and runs `rtp_server`.

```bash
./run.sh <transport> [host|host:port] [-- <rtp_server_args...>]
./run.sh -- --help
```

### Script Parameters (before `--`)

- `transport` (required): one of `rtp` or `webrtc`
- `host` or `host:port` (optional): overrides `[RTPOutput]` forwarding destination
  - port must be numeric and in range `1..65535`

### Pass-through Parameters (after `--`)

- Every argument after `--` is passed directly to the `rtp_server` binary.
- Use this for options like `--config`, `--server-handshake-port`, debug overlay flags, WebRTC options, TET options, etc.

### Special Help Modes

- `./run.sh -h` or `./run.sh --help`: shows script help
- `./run.sh -- --help`: runs binary help without requiring `transport`
- `./run.sh rtp -- --help`: runs binary help with transport context

### Behavior Notes

- Script writes runtime output to `rtp_server_output.txt`.
- Config file resolution priority:
  1. `RTP_SERVER_CONFIG_FILE` env var (if file exists)
  2. binary pass-through `--config <path>` or `--config=<path>`
  3. local `rtp_server_config.ini`
- If YOLO is configured, `run.sh` uses `[Common] width/height` unless overridden by pass-through `--common-width` / `--common-height`.

### Examples

```bash
./run.sh rtp
./run.sh webrtc
./run.sh rtp 192.168.1.100
./run.sh rtp 192.168.1.100:5006
./run.sh rtp -- --server-handshake-port 5004
./run.sh webrtc -- --webrtc-signaling-port 8080
./run.sh rtp -- --tet-enable=true --tet-tile-file tiles/example.tet
./run.sh rtp -- --debug-draw-bitrate --debug-draw-fps
./run.sh -- --help
```

---

## `test_client.sh`

Test RTP client for handshake + stream send to `rtp_server` using either generated patterns or an H.265 file.

```bash
./test_client.sh [options] [video_file]
```

### Options

- `-s, --server HOST` (default: `127.0.0.1`)
- `-p, --port PORT` (default: `5000`) - handshake port
- `-t, --pattern PATTERN` (default: `ball`) - used when no file is provided
- `-w, --width WIDTH` (default: `640`) - test-pattern mode
- `-h, --height HEIGHT` (default: `480`) - test-pattern mode
- `-d, --duration SEC` (default: `5`) - `0` means infinite
- `-f, --file FILE` - explicit H.265 file path (overrides pattern mode)
- `-v, --verbose` - verbose `gst-launch-1.0` output
- `--debug` - enable GStreamer debug level on launch
- `--help` - show help

### Positional Argument

- `video_file` (optional): treated as input file if `--file` was not provided.

### Pattern Values

- `ball`, `smpte`, `colors`, `circular`, `snow`, `black`, `white`

### Notes

- `-h` means **height**, not help.
- Script validates required tools (`socat`, `gst-launch-1.0`) and required plugins before streaming.

---

## `play_h265.sh`

Playback helper for raw H.265 streams and common containerized HEVC files.

```bash
./play_h265.sh [options] <video_file>
```

### Options

- `-p, --player PLAYER` - `ffplay` (default), `vlc`, `mpv`
- `-f, --fullscreen` - fullscreen playback
- `-l, --loop` - loop playback
- `-v, --verbose` - verbose player output and file info
- `-h, --help` - show usage

### Supported File Types

- Raw: `.h265`, `.hevc`, `.265`
- Containers: `.mp4`, `.mkv`, `.avi`, `.mov`, `.m4v`

---

## `run_static_analysis.sh`

Runs static checks (Clang Static Analyzer, Cppcheck, and additional grep-based checks) for `rtp_server/src`.

```bash
./run_static_analysis.sh [analysis_dir] [timestamp]
```

### Parameters

- `analysis_dir` (optional): output directory for reports  
  default: `../static_analysis_results`
- `timestamp` (optional): suffix used in output file names  
  default: current time in `YYYYMMDD_HHMMSS`

### Help

```bash
./run_static_analysis.sh --help
```

### Outputs

- text report: `rtp_server_analysis_<timestamp>.txt`
- html analyzer report directory: `rtp_server_html_<timestamp>/`
- cppcheck xml: `rtp_server_cppcheck_<timestamp>.xml`

