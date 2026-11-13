# Local Development Environment Setup

This guide covers host setup on Linux. For containerized development, see `APPTAINER_DEV_ENV.md`.

## System Requirements

- Operating system: Linux (tested on Ubuntu 22.04+)
- Architecture: x86_64 or ARM64
- RAM: 16 GB recommended
- GPU: optional — not required for a minimal CPU + GStreamer workflow

## Step 1: Install System Packages

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    wget \
    pkg-config \
    ninja-build \
    python3 \
    python3-pip \
    python3-venv \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgtkmm-3.0-dev \
    autoconf \
    automake \
    libtool \
    yasm
```

## Step 2 (optional): NVIDIA driver, CUDA, and DeepStream 7.1

**Optional.** Use this step for **encoder_cli** with YOLO, `nvh265enc` / `nvvideoconvert`, and related GPU tests. Skip for decoder-focused or CPU/Kvazaar-only work.

### 2a. NVIDIA driver (optional; needed for GPU paths)

Skip if you are not using an NVIDIA GPU on the host (containers may still use `--gpus all`).

Verify the driver:

```bash
nvidia-smi
```

If it fails, install a current driver (Ubuntu example):

```bash
sudo ubuntu-drivers install
# reboot, then nvidia-smi again
```

### 2b. DeepStream 7.1 install (optional)

Download **DeepStream 7.1** from [NVIDIA NGC](https://catalog.ngc.nvidia.com/orgs/nvidia/resources/deepstream) (login required). Use the `.deb` for your architecture (`deepstream-7.1_7.1.0-1_amd64.deb` or `_arm64.deb`).

From the repository root:

```bash
# Option A: put the .deb in ~/Downloads — the installer auto-detects it even under sudo
sudo ./scripts/setup/bin/install_nvidia_deepstream.sh

# Option B: pass the path explicitly (sudo env — plain export is dropped by sudo)
sudo env DEEPSTREAM_DEB="$HOME/Downloads/deepstream-7.1_7.1.0-1_amd64.deb" \
  ./scripts/setup/bin/install_nvidia_deepstream.sh
```

The TensorRT **local-repo** `.deb` in `~/Downloads` is optional; if it is missing, the script continues and installs DeepStream from the NGC `.deb`. If `apt` reports TensorRT conflicts, add `nv-tensorrt-local-repo-ubuntu2204-10.3.0-cuda-12.5_1.0-1_amd64.deb` to `~/Downloads` and re-run, or install DeepStream with:

```bash
cd ~/Downloads
sudo apt install ./deepstream-7.1_7.1.0-1_amd64.deb
```

Confirm the SDK registered with the system package manager (required for a real install):

```bash
dpkg -l 'deepstream*'
ls /opt/nvidia/deepstream
./scripts/setup/bin/diagnose_deepstream.sh
```

If `dpkg` shows no `deepstream*` packages and `/opt/nvidia/deepstream` is missing, the `.deb` was not installed (for example only downloaded or opened without `apt install`). Install with:

```bash
cd ~/Downloads
sudo apt install ./deepstream-7.1_7.1.0-1_amd64.deb
```

The script:

- Installs GStreamer and DeepStream **apt** dependencies (good/bad/ugly plugins, RTSP, JSON/YAML libs, and related packages)
- Installs **CUDA toolkit 12.6** (`cuda-toolkit-12-6`) when `nvcc` is not already available (x86_64)
- Installs **TensorRT 10.3.0.26** when you provide `TENSORRT_LOCAL_DEB` (see script output if the pin fails)
- Installs the **DeepStream 7.1 SDK** from `DEEPSTREAM_DEB` or `DEEPSTREAM_TARBALL`
- Creates `/opt/nvidia/deepstream/deepstream` → `deepstream-7.1` when needed
- Verifies `nvdsinfer_custom_impl.h`, `nvvideoconvert`, `nvh265enc`, and `nvh265dec`

Optional environment variables:

| Variable | Purpose |
|----------|---------|
| `NGC_API_KEY` | Download the DeepStream `.deb` from NGC when `DEEPSTREAM_DEB` is unset |
| `DEEPSTREAM_TARBALL` | Use `deepstream_sdk_v7.1.0_x86_64.tbz2` instead of the `.deb` |
| `TENSORRT_LOCAL_DEB` | `nv-tensorrt-local-repo-ubuntu2204-10.3.0-cuda-12.5_1.0-1_amd64.deb` from NVIDIA TensorRT |
| `SKIP_CUDA=1` / `SKIP_TENSORRT=1` | Skip those phases if you manage them separately |
| `--deps-only` | Apt dependencies only (no CUDA/TensorRT/DeepStream install) |

Shortcut (re-invokes this script with sudo):

```bash
export DEEPSTREAM_DEB="$HOME/Downloads/deepstream-7.1_7.1.0-1_amd64.deb"
./scripts/setup/bootstrap.sh nvidia
```

After DeepStream is installed, run **`bootstrap.sh core`** (Step 3) so the DeepStream-Yolo parser can build.

For behavior and troubleshooting of the Yolo parser build, see `scripts/setup/README.md`.

### 2c. Docker / Apptainer alternative (optional DeepStream on host)

If you need DeepStream features but not a host SDK install, use the DeepStream 7.1 development image in `APPTAINER_DEV_ENV.md` or `docs/part08/ch26-docker.md` (`nvcr.io/nvidia/deepstream:7.1-gc-triton-devel`).

## Step 3: Bootstrap Project Dependencies

From repository root:

```bash
./scripts/setup/bootstrap.sh core
```

This **always** installs ONNX Runtime into `lib/onnxruntime`. If the DeepStream SDK dev headers are present (Step 2 or equivalent), it also builds the DeepStream-Yolo parser; otherwise it **skips Yolo** and exits successfully. Set `SKIP_YOLO=1` to skip Yolo even when DeepStream is installed.

After DeepStream is installed later:

```bash
./scripts/setup/bootstrap.sh yolo
```

**Optional codecs** (independent of DeepStream, TensorRT, and Yolo — only need Step 1 build deps):

```bash
./scripts/setup/bootstrap.sh kvazaar   # CPU H.265 encoder → lib/kvazaar
./scripts/setup/bootstrap.sh openhevc # OpenHEVC decoder plugin → lib/openhevc
```

Order does not matter relative to `core`; many workflows use `core` then one or both codec commands. **`encoder_cli/build.sh` still requires the Yolo `.so`** for the default detection path until you run `yolo` with DeepStream available.

Optional export environment (**requires DeepStream-related YOLO tooling** for model conversion):

```bash
./scripts/setup/bootstrap.sh ds-env
```

For full command behavior, see `docs/part07/scripts-setup-readme.md`.

## Step 4: Build Applications

```bash
cd encoder_cli && ./build.sh   # requires DeepStream-Yolo lib from bootstrap core
cd ../rtp_server && ./build.sh # builds without DeepStream; GPU/inference features need Step 2 at runtime
```

## Step 5: Verify Plugin Discovery

```bash
gst-inspect-1.0 sfprireconstruct
gst-inspect-1.0 sfh265seiextract
gst-inspect-1.0 kvazaarenc   # if kvazaar was installed
gst-inspect-1.0 openhevcdec  # if openhevc was installed
gst-inspect-1.0 nvvideoconvert   # optional: after Step 2 (DeepStream / driver)
gst-inspect-1.0 nvh265enc        # optional: after Step 2 (DeepStream / driver)
```

## Step 6: Run Tests

```bash
cd test
./build.sh
./run.sh
```

`./test/build.sh` does not require DeepStream. Without Step 2, tests that use nvds headers or NVIDIA GStreamer elements are **skipped or fail**; **rtp_server** and **vcai_decoder** suites can still pass (encoder GPU cases may not).

## Application Notes

- `encoder_cli` and `rtp_server` both use GStreamer.
- Setup wrappers configure `GST_PLUGIN_PATH` and `LD_LIBRARY_PATH` as needed for local project plugins.
- See:
  - `encoder_cli/README.md`
  - `rtp_server/README.md`

## Troubleshooting

### `nvidia-smi` or NVIDIA GStreamer plugins missing

1. Confirm driver: `nvidia-smi`
2. Re-run Step 2 or install only dependencies: `sudo ./scripts/setup/bin/install_nvidia_deepstream.sh --deps-only`
3. Confirm DeepStream plugins: `gst-inspect-1.0 nvvideoconvert`

### `nvdsinfer_custom_impl.h` missing (DeepStream-Yolo build)

Diagnose what is on disk:

```bash
./scripts/setup/bin/diagnose_deepstream.sh
```

If the `.deb` installed libraries but not `sources/includes`, add the **SDK tarball** from the same NGC DeepStream 7.1 page (`deepstream_sdk_v7.1.0_x86_64.tbz2` on dGPU):

```bash
sudo DEEPSTREAM_TARBALL="$HOME/Downloads/deepstream_sdk_v7.1.0_x86_64.tbz2" \
  ./scripts/setup/bin/install_deepstream_sdk_sources.sh
./scripts/setup/bootstrap.sh core
```

Verify:

```bash
test -f /opt/nvidia/deepstream/deepstream-7.1/sources/includes/nvdsinfer_custom_impl.h
```

Create the conventional symlink if needed:

```bash
sudo ln -sfn /opt/nvidia/deepstream/deepstream-7.1 /opt/nvidia/deepstream/deepstream
```

### Missing GStreamer headers or pkg-config entries

```bash
pkg-config --modversion gstreamer-1.0
pkg-config --modversion gstreamer-video-1.0
```

Install missing development packages if either command fails.

### Plugin not found at runtime

Run with higher debug output:

```bash
GST_DEBUG=3 gst-inspect-1.0 sfprireconstruct
```

If needed, re-run setup and rebuild:

```bash
./scripts/setup/bootstrap.sh core
cd encoder_cli && ./build.sh
cd ../rtp_server && ./build.sh
```

### TensorRT version conflicts during Step 2

DeepStream 7.1 expects TensorRT **10.3.0.26** built for CUDA **12.5**. If apt reports version conflicts, install from NVIDIA’s **local repo** `.deb` and set `TENSORRT_LOCAL_DEB`, or use `SKIP_TENSORRT=1` after installing TensorRT manually. Details are printed by `install_nvidia_deepstream.sh`.
