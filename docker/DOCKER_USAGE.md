# Docker Usage Guide

This guide documents the Docker wrappers in `docker/` and their current argument contracts.

## Prerequisites

- Docker installed and running
- NVIDIA container runtime available (`--gpus all` support)
- X11 available if you use display/debug windows

Quick GPU sanity check:

```bash
docker run --rm --gpus all nvidia/cuda:11.0-base nvidia-smi
```

## Auto-Created Host Directories

On first wrapper run, `docker/init_directories.sh` creates:

- `~/.VCAI/rtp_server` (RTP server logs/output)
- `~/.VCAI/encoder_cli` (encoder logs/output)
- `~/.VCAI/videos` (optional user storage)
- `~/.VCAI/configs` (optional user storage)

The host data directory was previously `~/.ShapeFuture` or `~/.VCTM`. Copy any existing files into `~/.VCAI` if you still need them.

## Image Build Behavior

Both wrappers auto-check images and can build as needed.

- Base image: `vcai-base:latest`
- App images:
  - `vcai-rtp-server:latest`
  - `vcai-encoder-cli:latest`

Manual builds (optional):

```bash
docker build -f docker/Dockerfile_base -t vcai-base:latest .
docker build -f docker/Dockerfile_rtp_server -t vcai-rtp-server:latest .
docker build -f docker/Dockerfile_encoder_cli -t vcai-encoder-cli:latest .
```

## Wrapper Options (Both Scripts)

These options are handled by the Docker wrappers themselves:

- `--build`
- `--force-rebuild`
- `--force-rebuild-base`
- `--background` / `-d`
- `--env VAR=value` / `-e VAR=value`
- `--help` / `-h`

## RTP Server Wrapper

Script: `docker/run_rtp_server.sh`

Usage:

```bash
./docker/run_rtp_server.sh [OPTIONS] <transport> [host|host:port] [-- <rtp_server_args...>]
./docker/run_rtp_server.sh [OPTIONS] -- --help
```

Forwarded contract (to `rtp_server/run.sh`):

- `<transport>`: `rtp` or `webrtc`
- optional `host` or `host:port`
- pass-through binary args after `--`

Examples:

```bash
# RTP input transport
./docker/run_rtp_server.sh rtp

# RTP input + RTP output forwarding override
./docker/run_rtp_server.sh rtp 192.168.1.100:5006

# WebRTC input + app override
./docker/run_rtp_server.sh webrtc -- --webrtc-signaling-port 8080

# RTP input + custom handshake port
./docker/run_rtp_server.sh rtp -- --server-handshake-port 5004

# Binary help pass-through
./docker/run_rtp_server.sh -- --help
```

## Encoder CLI Wrapper

Script: `docker/run_encoder_cli.sh`

Usage:

```bash
./docker/run_encoder_cli.sh [OPTIONS] <input_mode> <output_mode> [script_args] [-- <encoder_cli_args...>]
./docker/run_encoder_cli.sh [OPTIONS] -- --help
```

Forwarded contract (to `encoder_cli/run.sh`):

- `<input_mode>`: `file`, `camera`, or `udp`
- `<output_mode>`: `rtp` or `webrtc`
- optional script args (`input_arg`, `output_arg`, `-o <output_arg>`)
- pass-through binary args after `--`

Examples:

```bash
# UDP input to RTP output
./docker/run_encoder_cli.sh udp rtp

# File input to RTP output
./docker/run_encoder_cli.sh file rtp /path/to/video_1920x1080_30.yuv

# Camera input to WebRTC output with explicit signaling URL
./docker/run_encoder_cli.sh camera webrtc /dev/video0 -o ws://127.0.0.1:8080

# Pass binary arguments after --
./docker/run_encoder_cli.sh udp webrtc -- --debug-all --communication-timeout-ms 2000

# Binary help pass-through
./docker/run_encoder_cli.sh -- --help
```

Notes:

- In `file` mode, if the file exists on host, the wrapper auto-mounts that file path read-only into the container.
- In `camera` mode, available `/dev/video*` devices are mounted.

## Background Mode

```bash
# Start detached
./docker/run_rtp_server.sh --background rtp
./docker/run_encoder_cli.sh --background udp rtp

# Follow logs
docker logs -f vcai-rtp-server
docker logs -f vcai-encoder-cli

# Stop
docker stop vcai-rtp-server
docker stop vcai-encoder-cli
```

## Current Mounts

RTP server wrapper mounts:

- `~/.VCAI/rtp_server:/opt/vcai/data/rtp_server`
- `<repo>/yolo:/opt/vcai/yolo`
- `<repo>/rtp_server/rtp_server_config.ini:/opt/vcai/rtp_server/rtp_server_config.ini:ro` (if present)
- `/tmp/.X11-unix:/tmp/.X11-unix`
- `/dev/dri` (device)
- `XAUTHORITY` file (if detected)

Encoder wrapper mounts:

- `~/.VCAI/encoder_cli:/opt/vcai/data/encoder_cli`
- `<repo>/yolo:/opt/vcai/yolo`
- `<repo>/encoder_cli/encoder_config.ini:/opt/vcai/encoder_cli/encoder_config.ini:ro` (if present)
- `<repo>/encoder_cli/encoder_config_prod.ini:/opt/vcai/encoder_cli/encoder_config_prod.ini:ro` (if present)
- `<repo>/encoder_cli/static_roi_data.json:/opt/vcai/encoder_cli/static_roi_data.json:ro` (if present)
- file input path mount in `file` mode (if source file exists)
- `/tmp/.X11-unix:/tmp/.X11-unix`
- `${XDG_RUNTIME_DIR}:${XDG_RUNTIME_DIR}`
- `/dev/dri` and optional `/dev/video*` devices
- `XAUTHORITY` file (if detected)

## Troubleshooting

- X11 permission issue:
  - Run `xhost +local:docker`
- Camera permission issue:
  - Ensure your user can access `/dev/video*` (often `video` group membership)
- Force clean rebuild:
  - `./docker/run_rtp_server.sh --force-rebuild-base rtp`
  - `./docker/run_encoder_cli.sh --force-rebuild-base udp rtp`
- Host network port conflicts:
  - Check with `ss -tuln` before starting wrappers
