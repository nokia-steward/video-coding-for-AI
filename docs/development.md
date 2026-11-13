# Development guide

Repository layout, CMake build toggles, documentation map, and common maintenance tasks for contributors working from a full clone.

## Repository layout

| Path | Purpose |
|------|---------|
| [encoder_cli/](../encoder_cli/README.md) | Encoder application, model integration, output branches |
| [rtp_server/](../rtp_server/README.md) | Receiver/server application, transport/session/pipeline handling |
| [libvcai/](../libvcai/README.md) | Shared C++20 core used by both applications |
| [test/](../test/README.md) | Unified GoogleTest runner and test sources |
| [scripts/](../scripts/README.md) | Project-level setup/utilities (GStreamer, YOLO, evaluation helpers) |
| [patches/](../patches/) | Patch artifacts for custom third-party components |
| [docker/](../docker/DOCKER_USAGE.md) | Container build/run support files |

## Build model

Root CMake exposes subproject toggles (all `OFF` by default):

- `BUILD_ENCODER_CLI`
- `BUILD_RTP_SERVER`
- `BUILD_VCAI_DECODER`
- `BUILD_TESTS`

`libvcai` sources are reused by each consumer target via
[`libvcai/libvcai_sources.cmake`](../libvcai/libvcai_sources.cmake).

## Key documentation

- **HTML manual** — book-like site (from repo root): `pip install -r requirements-docs.txt && ./docs/build_manual.sh` → open `manual/index.html` (file://)
- [INSTALL.md](../INSTALL.md) — local Linux setup and GStreamer workflow
- [APPTAINER_DEV_ENV.md](../APPTAINER_DEV_ENV.md) — Apptainer container workflow
- [encoder_cli/README.md](../encoder_cli/README.md) — encoder usage, script reference, full CLI option reference
- [rtp_server/README.md](../rtp_server/README.md) — receiver usage, test mode, transport/config details
- [libvcai/README.md](../libvcai/README.md) — shared library API/build model overview
- [test/README.md](../test/README.md) — current test targets/runner behavior
- [scripts/README.md](../scripts/README.md) — project-level script reference
- [encoder_cli/scripts/README.md](../encoder_cli/scripts/README.md) — encoder script wrappers/libraries
- [rtp_server/scripts/README.md](../rtp_server/scripts/README.md) and [rtp_server/SCRIPTS_README.md](../rtp_server/SCRIPTS_README.md) — rtp_server script references
- [FEATURE_OPTIONS_INSTRUCTIONS.md](../FEATURE_OPTIONS_INSTRUCTIONS.md) — feature/options matrix and compatibility
- [Host Setup Bootstrap Guide](part07/scripts-setup-readme.md) — `bootstrap.sh` setup commands

## Common developer tasks

### Refresh local codec plugins

```bash
./scripts/setup/bootstrap.sh kvazaar
./scripts/setup/bootstrap.sh openhevc
```

([scripts/setup/bootstrap.sh](../scripts/setup/bootstrap.sh))

### Static analysis

- root: `./run_static_analysis.sh`
- component-specific scripts under each subdirectory (for example `encoder_cli/`, `rtp_server/`, `libvcai/`, `test/`)

## Generated local directories

Created during setup, build, or runtime (typically gitignored):

- `build/`
- `lib/`
- `yolo/`
- `.env/`
- `test/test_build/`
- `test/test_output/`
- `static_analysis_results/`
