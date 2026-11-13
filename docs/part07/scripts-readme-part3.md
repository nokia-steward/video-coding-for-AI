# Project scripts (Part 3) — TET evaluation and pandaset tools

This chapter documents TET evaluation helpers, BD-rate tooling, PandaSet conversion
utilities, and semseg support scripts.

## Source and coverage

This page mirrors [**scripts/README.md**](../../scripts/README.md), lines `333–681`.
The excerpt includes script purpose, usage, options, examples, and supporting notes.

## Script index

### TET evaluation entry points

- [run_tet_comparison.sh](../../scripts/run_tet_comparison.sh) - Main TET matrix runner (bbox and semseg, including static and adaptive luma cases).
- [run_tet_bdrate.sh](../../scripts/tet_comparison_framework/run_tet_bdrate.sh) - Generates multi-QP inputs for BD-rate.
- [compute_bdrate.py](../../scripts/tet_comparison_framework/tools/compute_bdrate.py) - Computes BD-rate from generated CSV.
- [populate_vcmrs_excel.py](../../scripts/tet_comparison_framework/tools/populate_vcmrs_excel.py) - Fills VCMRS templates from summary CSVs.

### TET framework helpers

- [lib/run_tet_comparison_main.sh](../../scripts/tet_comparison_framework/lib/run_tet_comparison_main.sh) - Core comparison orchestration.
- [lib/tet_case_helpers.sh](../../scripts/tet_comparison_framework/lib/tet_case_helpers.sh) - Per-case execution utilities.
- [lib/tet_setup_helpers.sh](../../scripts/tet_comparison_framework/lib/tet_setup_helpers.sh) - Setup and environment helpers.

### PandaSet and semseg tools

- [convert_pandaset_to_coco.py](../../scripts/tet_comparison_framework/tools/convert_pandaset_to_coco.py) - PandaSet cuboids to COCO JSON.
- [convert_pandaset_semseg_pkl_to_json.py](../../scripts/tet_comparison_framework/tools/convert_pandaset_semseg_pkl_to_json.py) - Sparse semseg JSON conversion.
- [generate_pandaset_front_camera_yuv.py](../../scripts/tet_comparison_framework/tools/generate_pandaset_front_camera_yuv.py) - Deterministic front-camera YUV generation.
- [yolo/semseg/generate_semseg_config.sh](../../scripts/yolo/semseg/generate_semseg_config.sh) - Generates semseg pipeline YAML configs.
- [yolo/semseg/convert_semseg_model.sh](../../scripts/yolo/semseg/convert_semseg_model.sh) - Fetches/converts semseg model assets.

## Full reference excerpt

The following section is included directly from `scripts/README.md` to keep details
in sync with script behavior and CLI options.

--8<-- "scripts/README.md:333:681"
