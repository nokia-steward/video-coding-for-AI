# Apptainer Development Environment Guide

This document explains how to use the Apptainer (formerly Singularity) development environment for the VCAI project.

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Initial Setup](#initial-setup)
- [How It Works](#how-it-works)
- [Usage](#usage)
- [Available Commands](#available-commands)
- [Troubleshooting](#troubleshooting)

## Overview

The VCAI project uses an Apptainer containerized development environment to provide a consistent, reproducible build and runtime environment. This container includes:

- **Base Image**: NVIDIA DeepStream 7.1 development image (`nvcr.io/nvidia/deepstream:7.1-gc-triton-devel`)
- **Pre-installed Dependencies**:
  - CUDA Toolkit 13.0
  - GStreamer 1.0 with plugins
  - Build tools (CMake, GCC-12, Meson, etc.)
  - Kvazaar HEVC encoder (compiled during container build)
  - DeepStream SDK libraries
  - Various multimedia and development libraries

The container isolates all dependencies and ensures consistent builds across different host systems.

## Prerequisites

### Required Software

1. **Apptainer** (version 1.1.0 or later)
   - Check installation: `apptainer --version`
   - Installation guide: https://apptainer.org/docs/user/main/quick_start.html

2. **NVIDIA GPU with CUDA drivers** (for GPU acceleration)
   - Verify GPU access: `nvidia-smi`
   - The container uses `--nv` flag to enable GPU support

3. **Git** (for cloning the repository)
   - Check installation: `git --version`

### System Requirements

- **Operating System**: Linux (Apptainer is Linux-only)
- **Architecture**: x86_64
- **Storage**: At least 10GB free space (for container image)
- **Network**: Internet connection for initial build (downloads base image)

## Initial Setup

### Step 1: Build the Container

From the project root directory, run:

```bash
./dev_setup.sh
```

This script will:
1. Check if Apptainer is installed
2. Build the container image (`dev.sif`) if it doesn't exist or if `dev.def` is newer
3. Enter the container automatically after building

**What happens during build:**

The build process (`dev.def`):
- Pulls the NVIDIA DeepStream base image from NVIDIA's container registry (`nvcr.io`) via Apptainer's Docker bootstrap
- Installs system packages (GStreamer plugins, build tools, libraries)
- Installs CUDA Toolkit 13.0
- Compiles and installs Kvazaar encoder
- Copies project scripts and patches into the container
- Sets up environment variables for DeepStream and CUDA

**Build time**: Typically 30-60 minutes depending on network speed and system resources.

### Step 2: Verify Container

After the build completes, you should see:

```
? Development container built
? Development environment setup complete!
Entering the development container...
```

Your prompt should change to show `[Apptainer]` prefix, indicating you're inside the container.

## How It Works

### Container Architecture

```
???????????????????????????????????????????
?  Host System (your Linux machine)     ?
?                                         ?
?  ????????????????????????????????????? ?
?  ?  dev.sif (Container Image)        ? ?
?  ?  ????????????????????????????????? ? ?
?  ?  ?  DeepStream 7.1 Base Image   ? ? ?
?  ?  ?  + CUDA 13.0                 ? ? ?
?  ?  ?  + GStreamer + Plugins      ? ? ?
?  ?  ?  + Kvazaar Encoder          ? ? ?
?  ?  ?  + Build Tools               ? ? ?
?  ?  ????????????????????????????????? ? ?
?  ?                                     ? ?
?  ?  ????????????????????????????????? ? ?
?  ?  ?  Project Files (bind-mounted)? ? ?
?  ?  ?  /opt/sf_dev/scripts         ? ? ?
?  ?  ?  /opt/sf_dev/patches         ? ? ?
?  ?  ????????????????????????????????? ? ?
?  ????????????????????????????????????? ?
???????????????????????????????????????????
```

### Script Flow

When you run `./dev_setup.sh`, the following happens:

1. **`dev_setup.sh`**:
   - Checks for Apptainer installation
   - Builds `dev.sif` if needed
   - Executes `apptainer exec --nv dev.sif ./scripts/enter_dev.sh`

2. **`scripts/enter_dev.sh`**:
   - Launches an interactive bash shell with `scripts/lib/dev_wrapper.sh` as the rcfile

3. **`scripts/lib/dev_wrapper.sh`**:
   - Sources your host `.bashrc` (preserves your shell configuration)
   - Sources `scripts/setup/lib/dev_functions_lib.sh` (loads development helper functions)
   - Sets prompt to `[Apptainer]` prefix

4. **`scripts/setup/lib/dev_functions_lib.sh`**:
   - Provides convenience functions for building and running components
   - Functions are available inside the container

### File Mapping

The container automatically bind-mounts:
- **Project root**: Your entire project directory is accessible inside the container
- **Home directory**: Your `$HOME` is accessible (for config files, etc.)
- **GPU devices**: NVIDIA GPU devices are accessible via `--nv` flag

### Environment Variables

Inside the container, these environment variables are set:

```bash
# NVIDIA/CUDA
NVIDIA_DRIVER_CAPABILITIES=video,compute,graphics,utility
LD_LIBRARY_PATH includes CUDA 13.0 libraries

# DeepStream
DEEPSTREAM_PATH=/opt/nvidia/deepstream/deepstream-7.1
GST_PLUGIN_PATH includes DeepStream plugins

# User paths
PATH includes $HOME/.local/bin
LD_LIBRARY_PATH includes $HOME/.local/lib
PKG_CONFIG_PATH includes $HOME/.local/lib/pkgconfig
```

## Usage

### Entering the Container

**First time (builds container):**
```bash
./dev_setup.sh
```

**Subsequent times (container already exists):**
```bash
apptainer exec --nv dev.sif ./scripts/enter_dev.sh
```

Or use the convenience wrapper:
```bash
./dev_setup.sh
```
(The script will skip building if `dev.sif` exists and is up-to-date)

### Exiting the Container

Simply type `exit` or press `Ctrl+D` to exit the container shell.

### Rebuilding the Container

If you modify `dev.def` or want to rebuild from scratch:

```bash
# Remove old container
rm dev.sif

# Rebuild
./dev_setup.sh
```

Or force rebuild:
```bash
apptainer build --force dev.sif dev.def
```

### Running Commands Inside Container

**Non-interactive command:**
```bash
apptainer exec --nv dev.sif bash -c "cd encoder_cli && ./build.sh"
```

**Interactive shell:**
```bash
apptainer shell --nv dev.sif
```
(Note: `shell` provides a basic shell without the custom wrapper)

## Available Commands

Once inside the container (with `[Apptainer]` prompt), you have access to helper functions from `scripts/setup/lib/dev_functions_lib.sh`:

### Setup Commands

- **`setup_dependencies`** - Set up all dependencies (first time only)
  - Runs bootstrap setup
  - Sets up DeepStream export environment
  - Sets up YOLO

- **`build_all`** - Build all components (encoder_cli, rtp_server)

### Runtime Commands

- **`run_encoder [args]`** - Run encoder via `encoder_cli/run.sh`
- **`run_rtp_server [args]`** - Run RTP server via `rtp_server/run.sh`
- **`show_webcam`** - Show available webcam devices

### RTP Server Commands

- **`run_rtp_server rtp [-- <rtp_server_args...>]`** - Start server in RTP transport mode
- **`run_rtp_server webrtc [-- <rtp_server_args...>]`** - Start server in WebRTC transport mode

Examples:

```bash
# Start server (RTP input transport)
run_rtp_server rtp

# Start server (WebRTC input transport)
run_rtp_server webrtc

# Override handshake port
run_rtp_server rtp -- --server-handshake-port 5004
```

### Encoder Commands

- **`run_encoder <input_mode> <output_mode> ...`** - Consolidated encoder runner

Examples:

  ```bash
# File input, RTP output
run_encoder file rtp /path/to/video_1920x1080_30.yuv

# Camera input, RTP output
run_encoder camera rtp /dev/video0

# UDP input, WebRTC output
run_encoder udp webrtc
  ```

### Help Commands

- **`show_commands`** - Show all available commands with descriptions
- **`show_encoder_help`** - Show encoder-specific help
- **`show_rtp_help`** - Show RTP server-specific help

### Wrapper Note

Some helper functions in `dev_functions_lib.sh` (`run_encoder_camera`, `run_encoder_camera_rtp`, `run_encoder_rtp`)
are compatibility wrappers. Prefer `run_encoder`.

### Example Workflow

```bash
# Enter container
./dev_setup.sh

# Inside container - show available commands
show_commands

# Build all components
build_all

# Start server
run_rtp_server rtp

# In another shell, run encoder
run_encoder file rtp /path/to/video_1920x1080_30.yuv

# Exit container
exit
```

## Troubleshooting

### Container Build Fails

**Issue**: `Error: Failed to build development container`

**Solutions**:
- Check internet connection (needs to download base image)
- Ensure sufficient disk space (at least 10GB free)
- Check Apptainer version: `apptainer --version`
- Review build logs for specific error messages

**Forced rebuild**:
```bash
rm dev.sif
./dev_setup.sh
```

### GPU Not Available

**Issue**: CUDA/GPU not accessible inside container

**Symptoms**:
- `nvidia-smi` fails inside container
- GPU-based operations fail
- `cudaErrorInsufficientDriver` errors
- `Failed to parse group property` in nvinfer configuration

**Solutions**:

**For WSL (Windows Subsystem for Linux) users:**
1. **Install NVIDIA drivers on Windows** (not in WSL):
   - Download and install NVIDIA drivers from NVIDIA's website for Windows
   - WSL uses Windows drivers, not Linux drivers
   - Restart Windows after installation

2. **Verify GPU access in WSL** (outside container):
   ```bash
   # In WSL (not in container)
   nvidia-smi
   ```
   If this fails, GPU is not accessible in WSL.

3. **Install CUDA on WSL**:
   - Follow NVIDIA's guide: https://docs.nvidia.com/cuda/wsl-user-guide/
   - CUDA Toolkit for WSL bridges to Windows drivers

4. **Try additional Apptainer flags**:
   ```bash
   apptainer exec --nv --writable-tmpfs dev.sif ./scripts/enter_dev.sh
   ```

5. **Check WSL version**:
   ```bash
   wsl --version
   ```
   WSL2 is required for GPU support. Upgrade if needed:
   ```bash
   wsl --set-version <distro-name> 2
   ```

**For native Linux users:**
- Verify GPU on host: `nvidia-smi` (outside container)
- Ensure `--nv` flag is used: `apptainer exec --nv dev.sif ...`
- Check NVIDIA driver compatibility
- Some systems may need `--userns` flag: `apptainer exec --nv --userns dev.sif ...`

**General troubleshooting:**
- Check if GPU is accessible: `nvidia-smi` should work outside container
- Verify CUDA version compatibility between host and container
- Check container logs for specific CUDA errors

### Permission Issues

**Issue**: Permission denied errors

**Solutions**:
- Files created inside container belong to your user (bind mounts preserve ownership)
- If issues persist, check `apptainer.conf` for user namespace settings
- Some systems may require: `apptainer exec --nv --userns dev.sif ...`

### Container File Not Found

**Issue**: `Container file 'dev.sif' does not exist`

**Solution**:
```bash
# Build the container
./dev_setup.sh
```

### Scripts Not Found Inside Container

**Issue**: Helper functions not available

**Solution**:
- Ensure you entered via `./dev_setup.sh` or `./scripts/enter_dev.sh`
- The wrapper script (`scripts/lib/dev_wrapper.sh`) sources `scripts/setup/lib/dev_functions_lib.sh`
- If using `apptainer shell`, functions won't be available (use `exec` with `enter_dev.sh`)

### Environment Variables Not Set

**Issue**: GStreamer plugins or CUDA libraries not found

**Solution**:
- These are set in `dev.def` `%environment` section
- Rebuild container if you modified `dev.def`
- Check variables: `echo $GST_PLUGIN_PATH`

### Large Container Size

**Issue**: `dev.sif` file is very large (>10GB)

**Explanation**:
- This is normal - contains DeepStream SDK, CUDA toolkit, and all dependencies
- The container is a single file image (SIF format)
- Consider cleanup if disk space is limited: `apptainer cache clean`

### Updating Container Definition

**After modifying `dev.def`**:
```bash
# Remove old container
rm dev.sif

# Rebuild
./dev_setup.sh
```

The script automatically detects if `dev.def` is newer than `dev.sif` and rebuilds.

## Additional Notes

### Container Persistence

- The container image (`dev.sif`) persists between sessions
- Your project files are bind-mounted (changes persist)
- User-installed packages in `$HOME/.local` persist
- Container modifications are ephemeral unless you rebuild

### Performance

- Container has minimal performance overhead
- GPU access is native (no virtualization)
- File I/O is native (bind mounts)

### Security

- Apptainer runs containers as your user (no root privileges inside)
- Container cannot modify host system (except bind-mounted directories)
- Safe for shared systems

### Integration with IDE

- You can build inside container while editing on host
- Changes to source files are immediately visible inside container
- Use container for building/testing, host for editing

## See Also

- [INSTALL.md](INSTALL.md) - Manual installation guide (alternative to container)
- [README.md](README.md) - Project overview
