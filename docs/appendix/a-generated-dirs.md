# Appendix A — Generated directories

From the root [**README.md**](../../README.md) (generated/local, typically gitignored):

- `build/`
- `lib/`
- `yolo/`
- `.env/`
- `test/test_build/`
- `test/test_output/`
- `static_analysis_results/`

Manual build output: `manual/` (from `mkdocs build`).

Config and scripts expansion lives under `docs/config/` and extra `docs/part07/` pages; regenerate inventories with [**./docs/generate_inventories.sh**](../../docs/generate_inventories.sh) when [**patches/**](../../patches/) or [**scripts/**](../../scripts/) change.

Manual HTML uses the **readthedocs** MkDocs theme (see [**mkdocs.yml**](../../mkdocs.yml)); output layout is `manual/css/`, `manual/js/`, etc.

## Key docs and scripts at repo root

| File or path | Purpose |
|--------------|---------|
| [**README.md**](../../README.md) | Project overview and quick start |
| [**docs/development.md**](../development.md) | Repository layout, build model, docs index, dev tasks |
| [**CONTRIBUTING.md**](../../CONTRIBUTING.md) | Contribution workflow |
| [**SECURITY.md**](../../SECURITY.md) | Vulnerability reporting |
| [**INSTALL.md**](../../INSTALL.md) | Local Linux setup and GStreamer workflow |
| [**APPTAINER_DEV_ENV.md**](../../APPTAINER_DEV_ENV.md) | Apptainer container workflow (Ch 27) |
| [**FEATURE_OPTIONS_INSTRUCTIONS.md**](../../FEATURE_OPTIONS_INSTRUCTIONS.md) | Feature/options matrix (Part IX) |
| [**dev.def**](../../dev.def) | Apptainer definition file for dev image |
| [**dev_setup.sh**](../../dev_setup.sh) | Entrypoint to enter Apptainer dev environment |
| [**requirements-docs.txt**](../../requirements-docs.txt) | Python deps for MkDocs (manual build) |
| [**run_static_analysis.sh**](../../run_static_analysis.sh) | Root static analysis driver (Ch 8) |
| [**docs/build_manual.sh**](../../docs/build_manual.sh) | Build this manual → `manual/` |
| Config reference (encoder) | Manual: [encoder_config overview](../config/encoder_config-overview.md) |
| Config reference (rtp_server) | Manual: [rtp_server_config overview](../config/rtp_server_config-overview.md) |
| [**encoder_cli/encoder_config.ini**](../../encoder_cli/encoder_config.ini) | Committed reference; see Config reference (encoder) in manual |
| [**rtp_server/rtp_server_config.ini**](../../rtp_server/rtp_server_config.ini) | Committed reference; see Config reference (rtp_server) in manual |
