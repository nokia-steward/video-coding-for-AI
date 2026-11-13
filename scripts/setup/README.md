# Host dependency setup with `bootstrap.sh`

This page documents the host-side setup entrypoint `scripts/setup/bootstrap.sh` in detail.

For containerized development, keep using `./dev_setup.sh` and [APPTAINER_DEV_ENV.md](../../APPTAINER_DEV_ENV.md).

## What `bootstrap.sh` does

`bootstrap.sh` is the orchestration layer for setup tasks under `scripts/setup/bin`.

- Uses strict shell mode (`set -euo pipefail`) and stops on first failure.
- Resolves repo root automatically and runs sub-scripts from there.
- Provides a stable command interface (`doctor`, `core`, `all`, etc.).
- Uses GStreamer.

## Quick command reference

```bash
./scripts/setup/bootstrap.sh doctor
./scripts/setup/bootstrap.sh nvidia   # sudo: CUDA / TensorRT / DeepStream 7.1 (see INSTALL.md)
./scripts/setup/bootstrap.sh core
./scripts/setup/bootstrap.sh ds-env
./scripts/setup/bootstrap.sh yolo
./scripts/setup/bootstrap.sh kvazaar
./scripts/setup/bootstrap.sh openhevc
./scripts/setup/bootstrap.sh all
```

## Command behavior matrix

| Command | Internal scripts invoked | Main outcome |
|---|---|---|
| `doctor` | `bin/doctor_packages.sh` | Read-only `dpkg` package state report (no installs, no file changes) |
| `nvidia` | `bin/install_nvidia_deepstream.sh` (re-execs with `sudo`) | Apt GPU/DeepStream deps, optional CUDA 12.6, TensorRT 10.3, DeepStream 7.1 SDK; set `DEEPSTREAM_DEB` first |
| `core` | `bin/setup_onnxruntime.sh`, then `bin/setup_yolo.sh` if DeepStream `sources/includes` exists | Always installs ONNX Runtime; builds DeepStream-Yolo parser only when the SDK is present (otherwise prints skip + next steps) |
| `ds-env` | `bin/setup_ds_export_env.sh` with `NON_INTERACTIVE=1` | Recreates `.env/ds_export` Python venv for YOLO model export/conversion |
| `yolo` | `bin/setup_yolo.sh` | Clones/updates `yolo/DeepStream-Yolo` and builds `libnvdsinfer_custom_impl_Yolo.so` |
| `kvazaar` | `bin/install_kvazaar.sh` | Installs kvazaar + gst-kvazaar into `lib/kvazaar` |
| `openhevc` | `bin/install_openhevc.sh` | Installs OpenHEVC + gst-openhevc into `lib/openhevc` |
| `all` | `doctor` + `core` + `kvazaar` + `openhevc` + `ds-env` | One-shot host setup pipeline (stops on first failure) |

## Architecture behavior (`core`)

`core` always runs ONNX Runtime setup. It runs the YOLO parser build **only** when `find_deepstream_includes_dir` detects DeepStream SDK headers (same check as `setup_yolo.sh`). Set `SKIP_YOLO=1` to force skipping the Yolo step.

**Optional codecs** (`kvazaar`, `openhevc`) are separate commands: they do not require DeepStream or TensorRT. Run them anytime after `core` (or on a fresh host with only Step 1 system packages):

```bash
./scripts/setup/bootstrap.sh core
./scripts/setup/bootstrap.sh kvazaar   # optional CPU H.265 encoder
./scripts/setup/bootstrap.sh openhevc  # optional OpenHEVC decoder plugin
```

## Recommended workflows

### Full host setup in one command

Use this on a fresh machine after system packages are installed:

```bash
./scripts/setup/bootstrap.sh all
```

### Minimal runtime setup for build/inference path

```bash
./scripts/setup/bootstrap.sh core
```

### Only refresh export environment

```bash
./scripts/setup/bootstrap.sh ds-env
```

Use this when you only need Python export tooling updates and do not need to rebuild C/C++ components.

## Host NVIDIA + DeepStream (`nvidia`)

For GPU development on bare metal, run **before** `core` when DeepStream is not already installed (full detail in [INSTALL.md](../../INSTALL.md) Step 2):

```bash
export DEEPSTREAM_DEB="$HOME/Downloads/deepstream-7.1_7.1.0-1_amd64.deb"
./scripts/setup/bootstrap.sh nvidia
```

Direct script (equivalent):

```bash
sudo ./scripts/setup/bin/install_nvidia_deepstream.sh --help
```

## DeepStream-Yolo build (`yolo` / `core`)

Building `libnvdsinfer_custom_impl_Yolo.so` needs the **full DeepStream SDK** with development headers, not a runtime-only install:

```bash
test -f /opt/nvidia/deepstream/deepstream-7.1/sources/includes/nvdsinfer_custom_impl.h
```

If that file is missing, install DeepStream from NVIDIA's SDK (tarball or devel packages). Many setups also use:

```bash
sudo ln -sfn /opt/nvidia/deepstream/deepstream-7.1 /opt/nvidia/deepstream/deepstream
```

`scripts/yolo/setup_yolo.sh` auto-detects the `sources/includes` directory and patches the cloned Makefile `-I` path when possible.

When the system `libnvinfer-headers-dev` package is TensorRT **11.x** but DeepStream uses **libnvinfer10**, setup caches matching **TRT 10** headers under `lib/tensorrt-headers/` (via `apt-get download`) and pins the Yolo `Makefile` includes/link line to `libnvinfer.so.10`.

## Files and directories created

Depending on command, the setup writes to:

- `lib/onnxruntime` (ONNX Runtime prebuilt)
- `yolo/DeepStream-Yolo` and
  `yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo/libnvdsinfer_custom_impl_Yolo.so`
- `.env/ds_export` (Python export venv)
- `lib/kvazaar` (kvazaar + gst-kvazaar local install)
- `lib/openhevc` (OpenHEVC + gst-openhevc local install)

## Re-run and idempotency notes

- `doctor` is read-only.
- `setup_onnxruntime.sh` skips if `lib/onnxruntime/include` and `lib/onnxruntime/lib` already exist.
- `ds-env` always recreates `.env/ds_export` (existing env is removed first).
- `yolo` updates DeepStream-Yolo (`git pull` when repo exists) and rebuilds the parser library.
- `kvazaar` and `openhevc` commands rebuild their local prefixes under `lib/kvazaar` and `lib/openhevc`.

## Failure handling and troubleshooting

Because `bootstrap.sh` uses strict mode and does not swallow errors, a failing sub-step exits immediately.

Common recovery pattern:

1. Re-run the exact failing command to isolate the issue.
2. If needed, run the underlying bin script directly for focused logs.
3. Fix missing system prerequisites, then re-run the command.

Helpful direct calls:

```bash
./scripts/setup/bin/doctor_packages.sh
./scripts/setup/bin/setup_onnxruntime.sh
./scripts/setup/bin/setup_yolo.sh
./scripts/setup/bin/setup_ds_export_env.sh
```

For deeper environment setup details, see:

- [INSTALL.md](../../INSTALL.md)
- [docs/part02/initial-setup-overview.md](../../docs/part02/initial-setup-overview.md)
- [docs/part03/ch12-scripts-yolo.md](../../docs/part03/ch12-scripts-yolo.md)

## Layout and related files

- `scripts/setup/bootstrap.sh`: command dispatcher and orchestration
- `scripts/setup/bin/`: executable setup implementations
- `scripts/setup/lib/`: sourced helper libraries (for example `yolo_setup_lib.sh`)

Related indices:

- [scripts/README.md](../README.md) (full scripts index)
- [encoder_cli/scripts/README.md](../../encoder_cli/scripts/README.md)
- [rtp_server/scripts/README.md](../../rtp_server/scripts/README.md)

## Container note

Container build flows may call `scripts/setup/bin/*` scripts directly in Dockerfile or definition files.
That does not change the host recommendation: use `bootstrap.sh` as the canonical local entrypoint.
