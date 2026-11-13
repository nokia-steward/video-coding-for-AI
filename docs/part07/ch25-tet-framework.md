# Chapter 25 — TET comparison framework

Committed under [**scripts/tet_comparison_framework/**](../../scripts/tet_comparison_framework/):

| Path | Role |
|------|------|
| [`run_tet_bdrate.sh`](../../scripts/tet_comparison_framework/run_tet_bdrate.sh) | Multi-QP BD-rate style runs (encoder QP sweep, TET on/off) |
| [`lib/run_tet_comparison_main.sh`](../../scripts/tet_comparison_framework/lib/run_tet_comparison_main.sh) | Main comparison driver |
| [`lib/tet_case_helpers.sh`](../../scripts/tet_comparison_framework/lib/tet_case_helpers.sh) | Case helpers |
| [`lib/tet_setup_helpers.sh`](../../scripts/tet_comparison_framework/lib/tet_setup_helpers.sh) | Setup helpers |
| [`tools/compute_bdrate.py`](../../scripts/tet_comparison_framework/tools/compute_bdrate.py) | BD-rate computation |
| [`ah0187/`](../../scripts/tet_comparison_framework/ah0187/) | Semseg evaluation backend pieces |

Usage pointers are in [**rtp_server/README.md**](../../rtp_server/README.md) (BD-rate section) and [**scripts/README.md**](../../scripts/README.md). See **Appendix E** for the full file list.

## Default comparison matrix

The default `run_tet_comparison.sh` matrix now runs 7 single-mode cases per QP
for bbox and 7 for semseg:

- `baseline_all_off`
- `mode_roi_packing`
- `mode_background_blur`
- `mode_qp_map`
- `mode_tet`
- `mode_luma_scaling` (static luma: `--enable-adaptive-luma-adaptation false`)
- `mode_adaptive_luma_scaling` (analyzer-driven luma: `--enable-adaptive-luma-adaptation true`)

Semseg runs use the corresponding `semseg_*` case names, including
`semseg_mode_adaptive_luma_scaling`.

## qp_map backend contract

`run_tet_comparison_main.sh` validates `mode_qp_map` summary inclusion using a
stable encoder contract line:

- `encoder_cli` emits one stdout line prefixed with `CONTRACT_JSON:`
- contract schema is `encoder_backend_contract/v1`
- required fields: `schema`, `requested_background_mode`,
  `effective_background_mode`, `direct_qp_map_backend`

For `mode_qp_map`, summary rows are included only when the contract confirms
requested/effective mode is `qp_map_delta` and `direct_qp_map_backend=true`.
Malformed contract payloads are treated as contract drift and fail the gate.
If no contract line is present (older logs), the framework falls back to legacy
marker heuristics and records that fallback in `summary.md`.
