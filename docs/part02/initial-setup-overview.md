# Initial setup overview

This page summarizes the steps needed to get the project up and running from a fresh clone. Follow the linked chapters and [**INSTALL.md**](../../INSTALL.md) for detail. For containerized setup, use [**APPTAINER_DEV_ENV.md**](../../APPTAINER_DEV_ENV.md) or **Chapter 27** (Apptainer).

## Steps (in order)

**Step 1 — System packages and build deps**

Install system packages, Meson, Kvazaar/Autotools deps, and GStreamer build-deps.

- **Chapter 5** ([Local install — system packages](ch05-system-packages.md))
- [**INSTALL.md**](../../INSTALL.md) Step 1

**Step 2 (optional) — NVIDIA driver, CUDA, DeepStream 7.1**

Only when you need GPU encode, YOLO/DeepStream inference, or full **encoder_cli** build paths (see [**INSTALL.md**](../../INSTALL.md) Step 2). Skip on CPU/decoder-only workflows.

**Step 3 — GStreamer and ONNX Runtime**

From repo root, run (ONNX Runtime always; Yolo only if DeepStream SDK is installed):

```bash
./scripts/setup/bootstrap.sh core
./scripts/setup/bootstrap.sh kvazaar   # optional; no DeepStream required
./scripts/setup/bootstrap.sh openhevc  # optional; no DeepStream required
```

Command behavior and subcommand details are documented in [**Host Setup Bootstrap Guide**](../part07/scripts-setup-readme.md).

For a one-command full setup path, use:

```bash
./scripts/setup/bootstrap.sh all
```

`all` runs `doctor`, `core`, `kvazaar`, `openhevc`, and `ds-env` in sequence (stopping on first failure).

This installs ONNX Runtime into `lib/onnxruntime` and builds the DeepStream-Yolo parser library. The project uses GStreamer; use `./scripts/setup/bootstrap.sh kvazaar` and `./scripts/setup/bootstrap.sh openhevc` when those optional components are needed.

- [**Host Setup Bootstrap Guide**](../part07/scripts-setup-readme.md)
- [**scripts/setup/bootstrap.sh**](../../scripts/setup/bootstrap.sh)
- **Chapter 6** ([GStreamer workflow](ch06-gstreamer-source-workflow.md))
- [**INSTALL.md**](../../INSTALL.md) Step 3

**Step 4 — (Optional) YOLO / DeepStream export env**

Required for encoder_cli detection and for rtp_server when using YOLO/inference features. From repo root:

```bash
./scripts/setup/bootstrap.sh ds-env
```

Skip this step if you already used `./scripts/setup/bootstrap.sh all`.

- **Chapter 12** ([Scripts and YOLO](../part03/ch12-scripts-yolo.md)) / [**Host Setup Bootstrap Guide**](../part07/scripts-setup-readme.md)

**Step 5 — Build applications**

From repo root:

```bash
cd encoder_cli && ./build.sh
cd ../rtp_server && ./build.sh
```

- [**encoder_cli/build.sh**](../../encoder_cli/build.sh), [**rtp_server/build.sh**](../../rtp_server/build.sh)
- **Chapter 4** ([CMake and subprojects](ch04-cmake.md))

**Step 6 — Verify and run tests**

```bash
cd test && ./run.sh
```

- [**test/run.sh**](../../test/run.sh)
- **Chapter 22** ([Test suite layout](../part06/ch22-test-layout.md)), **Chapter 23** ([Build and run tests](../part06/ch23-build-run-tests.md))

**Step 7 — (Optional) Run applications**

Two terminals: start [**rtp_server**](../../rtp_server/run.sh) with `./run.sh rtp`, then [**encoder_cli**](../../encoder_cli/run.sh) with `./run.sh file rtp …` (see CLI for input path).

- **Chapter 10** ([Build, run, config](../part03/ch10-build-run-config.md)) (encoder)
- **Chapter 15** ([Build, config, run](../part04/ch15-build-config-run.md)) (rtp_server)

## Optional / later

- **Installing ONNX Runtime** — Only if you want encoder_cli’s adaptive luma input analyzer to run ONNX inference. See [Installing ONNX Runtime](../part03/install-onnx-runtime.md).
- **Patches (Chapter 7)** — Only if you modify local third-party sources and need to regenerate a project patch. See [Patches and third-party](ch07-patches.md).
- **Static analysis (Chapter 8)** — Part of the contributor workflow after making code changes, with command order, result interpretation, and common failure fixes. See [Static analysis and workflow](ch08-static-analysis.md).

## Quick reference (minimal commands)

After completing **Step 1** (system packages) per [**INSTALL.md**](../../INSTALL.md):

```bash
./scripts/setup/bootstrap.sh core
cd encoder_cli && ./build.sh
cd ../rtp_server && ./build.sh
cd ../test && ./run.sh
```

Then run applications (two terminals) as in Step 6.

## Quick reference (one-command setup)

After completing **Step 1** (system packages), you can run:

```bash
./scripts/setup/bootstrap.sh all
cd encoder_cli && ./build.sh
cd ../rtp_server && ./build.sh
cd ../test && ./run.sh
```
