# Installing ONNX Runtime for encoder_cli (adaptive luma)

Installing ONNX Runtime is part of the [Initial setup overview](part02/initial-setup-overview.md). You only need to install it if you want the encoder's **adaptive luma input analyzer** to run ONNX inference. Otherwise the encoder builds and runs with a stub and adaptive luma will not perform inference. For official install options and compatibility, see [ONNX Runtime Install](https://onnxruntime.ai/docs/install/) and [Compatibility](https://onnxruntime.ai/docs/reference/compatibility).

## Automated install (Linux x64 and aarch64)

From the repository root, run:

```bash
./scripts/setup/bootstrap.sh core
```

For full `bootstrap.sh` command coverage, see the [**Host Setup Bootstrap Guide**](../part07/scripts-setup-readme.md).

(or `./scripts/setup/bootstrap.sh all`). This downloads and extracts the ONNX Runtime Linux CPU prebuilt for your architecture (x86_64 or aarch64) into `lib/onnxruntime`. No manual steps are needed on supported Linux arches. `bootstrap.sh core` also runs the DeepStream-Yolo parser setup; if you only want ONNX Runtime, run `scripts/setup/bin/setup_onnxruntime.sh` directly. Then build encoder_cli (e.g. `cd encoder_cli && ./build.sh`); the build script adds `lib/onnxruntime` to `CMAKE_PREFIX_PATH` when present, so CMake will find ONNX Runtime and the encoder will use it for adaptive luma inference.

## Requirements

- **Linux**: The [official install guide](https://onnxruntime.ai/docs/install/) recommends the English language package with `en_US.UTF-8` locale. For example:
  ```bash
  sudo locale-gen en_US.UTF-8
  sudo update-locale LANG=en_US.UTF-8
  ```
- **Windows**: Install the [Visual C++ 2019 runtime](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170) (latest version recommended).

## Manual install

For Windows, Linux GPU, other architectures, or a custom version, install manually as follows.

### Option A — Prebuilt (recommended)

Prebuilt packages are available on [GitHub Releases](https://github.com/microsoft/onnxruntime/releases) (e.g. latest v1.24.x). The examples below use the repository **`lib`** folder as the extraction and install location (from the repo root).

### Linux x64 (CPU)

If you prefer not to use bootstrap, you can install manually as below.

1. From the repository root, download `onnxruntime-linux-x64-<version>.tgz` from the [releases](https://github.com/microsoft/onnxruntime/releases) page.
2. Extract into `lib/onnxruntime`:
   ```bash
   mkdir -p lib/onnxruntime
   tar -xzf onnxruntime-linux-x64-1.24.3.tgz -C lib/onnxruntime --strip-components=1
   ```
3. When building encoder_cli, set `CMAKE_PREFIX_PATH` to that directory:
   ```bash
   cd encoder_cli
   export CMAKE_PREFIX_PATH="$(pwd)/../lib/onnxruntime:$CMAKE_PREFIX_PATH"
   ./build.sh
   ```

### Linux x64 (GPU, optional)

For GPU inference, use `onnxruntime-linux-x64-gpu-<version>.tgz` or `onnxruntime-linux-x64-gpu_cuda13-<version>.tgz`. You must meet [CUDA execution provider requirements](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html#requirements) (CUDA and cuDNN versions, `LD_LIBRARY_PATH`, etc.). Extract into `lib/onnxruntime` and set `CMAKE_PREFIX_PATH` the same way as the CPU package.

### Windows x64

1. Download `onnxruntime-win-x64-<version>.zip` from [releases](https://github.com/microsoft/onnxruntime/releases).
2. From the repository root, extract the archive into `lib\onnxruntime` (e.g. create `lib\onnxruntime` and extract there).
3. Set `CMAKE_PREFIX_PATH` to the full path to `lib\onnxruntime` when configuring the encoder_cli build.

### Prebuilt layout

Prebuilt archives typically contain `include/`, `lib/`, and (for recent releases) CMake config files so that `find_package(onnxruntime)` works when `CMAKE_PREFIX_PATH` points at the extracted directory. If your release does not provide a CMake config, you may need to install or copy the `include` and `lib` directories into a layout that your build can use (see the [ONNX Runtime CMake guideline](https://github.com/microsoft/onnxruntime/blob/main/docs/cmake_guideline.md)).

### Option B — Build from source

To build ONNX Runtime yourself, follow the [Build for inferencing](https://onnxruntime.ai/docs/build/inferencing) instructions. Prerequisites include Python 3.10+ and CMake 3.28+. The example below uses **`lib`** as the clone and build directory and installs into **`lib/install`** so that ONNX Runtime lives alongside other local installs (e.g. GStreamer).

**Linux example (from repository root):**

```bash
git clone --recursive https://github.com/microsoft/onnxruntime.git lib/onnxruntime-src
cd lib/onnxruntime-src
./build.sh --config RelWithDebInfo --build_shared_lib --parallel \
  --compile_no_warning_as_error --skip_submodule_sync \
  --cmake_extra_defines CMAKE_INSTALL_PREFIX="$(pwd)/../install"
# Then run the install target (e.g. cmake --install build/Linux/RelWithDebInfo
# or equivalent from the build tree). That installs into lib/install.
cd ../..
```

Then build encoder_cli with `CMAKE_PREFIX_PATH` including the install prefix so that `find_package(onnxruntime)` finds it:

```bash
cd encoder_cli
export CMAKE_PREFIX_PATH="$(pwd)/../lib/install:$CMAKE_PREFIX_PATH"
./build.sh
```

## Converting a .pt model to ONNX (optional)

If you set `input_analyzer_model_path` in `[ROI]` to a PyTorch checkpoint (`.pt`), `encoder_cli/run.sh` will convert it to ONNX when the `.onnx` file is missing or older than the `.pt`. That conversion requires Python with **torch** and **torchvision**. Prefer a single environment (e.g. a virtualenv) so torch and numpy come from the same source; mixing pip and apt packages can cause NumPy sanity-check failures:

```bash
python3 -m venv .env/onnx
source .env/onnx/bin/activate
pip install torch torchvision
# then run encoder_cli from the same shell, or run the export script manually
```

To export manually: `python3 encoder_cli/scripts/export_input_analyzer_onnx.py <path/to/model_weights.pt> [path/to/output.onnx]`. The script expects a checkpoint that contains `model_state_dict` (see the script docstring and `aidoc/input_analyzer_example.py`).

## Using with encoder_cli

1. Build encoder_cli with `CMAKE_PREFIX_PATH` set to your ONNX Runtime location (e.g. `lib/onnxruntime` for prebuilt or `lib/install` if you installed from source into `lib/install`).
2. Confirm in the CMake output: **"ONNX Runtime found - adaptive luma analyzer will use ONNX inference."**
3. If ONNX Runtime is not found, you will see: **"ONNX Runtime not found - adaptive luma analyzer will use stub"**. The encoder still builds; adaptive luma will not run inference. To disable the lookup explicitly, pass `-DENABLE_ONNXRUNTIME=OFF` when configuring.

### If the encoder exits with "built without ONNX Runtime"

If you enable adaptive luma (`enable_adaptive_luma_adaptation = true`) and the encoder exits with an error like *"Adaptive luma is enabled but encoder_cli was built without ONNX Runtime"*, the binary was built with the stub. Install ONNX Runtime using one of the options above, set `CMAKE_PREFIX_PATH` to the install location, and rebuild encoder_cli.

## Package summary

| Platform      | Use case | Asset name pattern                                      |
|---------------|----------|---------------------------------------------------------|
| Linux x64     | CPU      | `onnxruntime-linux-x64-<version>.tgz`                   |
| Linux x64     | GPU      | `onnxruntime-linux-x64-gpu-<version>.tgz` or `*_cuda13*` |
| Windows x64   | CPU      | `onnxruntime-win-x64-<version>.zip`                     |
| Windows x64   | GPU      | `onnxruntime-win-x64-gpu-<version>.zip`                 |

For more variants and compatibility, see the [official install page](https://onnxruntime.ai/docs/install/) and the [inference install table](https://onnxruntime.ai/docs/install/#inference-install-table-for-all-languages).
