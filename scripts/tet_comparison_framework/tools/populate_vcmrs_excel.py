#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

"""Populate VCMRS Excel template from TET comparison summary results.

This script supports two result layouts:

1) Single-sequence layout (legacy):
   <results-dir>/summary.csv

2) Multi-sequence layout (PandaSet sequence mode):
   <results-dir>/<sequence>/summary.csv

Output behavior remains unchanged: one .xlsm is created per non-anchor test case.
Each generated workbook is populated for all matched sequences in the template.
"""

from __future__ import annotations

import argparse
import csv
import re
import shutil
import sys
from pathlib import Path
from typing import NoReturn

from openpyxl import load_workbook


ANCHOR_CASE = "baseline_all_off"
DEFAULT_SHEET = "SFU-HW_RA_Inner"
SUMMARY_SHEET = "Summary"
QP_MAP_CASES = (
    "mode_qp_map",
)

# Column mapping in *_RA_Inner sheets:
# D:E:F => Reference: Anchor (kbps, mono-mAP, mAP)
# N:O:P => Test: Test       (kbps, mono-mAP, mAP)
ANCHOR_COL_KBPS = 4
ANCHOR_COL_MONO_MAP = 5
ANCHOR_COL_MAP = 6
TEST_COL_KBPS = 14
TEST_COL_MONO_MAP = 15
TEST_COL_MAP = 16


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Create one .xlsm per test case from TET summary results. "
            "Supports both single-sequence and multi-sequence result directories."
        )
    )
    parser.add_argument(
        "--results-dir",
        required=True,
        type=Path,
        help=(
            "Directory containing either summary.csv (single-sequence mode) or "
            "sequence subdirectories each with summary.csv (multi-sequence mode)."
        ),
    )
    parser.add_argument(
        "--excel-template",
        required=True,
        type=Path,
        help="Path to the source .xlsm template workbook.",
    )
    parser.add_argument(
        "--sheet",
        default=DEFAULT_SHEET,
        help=f"Target worksheet name (default: {DEFAULT_SHEET}).",
    )
    parser.add_argument(
        "--sequence",
        default="",
        help=(
            "Optional sequence filter. Sequence matching is numeric-normalized "
            "(e.g. 057 == 57). If omitted, all discovered sequences are filled."
        ),
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Directory for generated files (default: results-dir).",
    )
    return parser.parse_args()


def fail(message: str) -> NoReturn:
    raise SystemExit(f"ERROR: {message}")


def infer_sequence_name(results_dir: Path) -> str:
    match = re.match(r"tet_test_results_(.+)_\d{8}_\d{6}$", results_dir.name)
    if match:
        return match.group(1)
    return results_dir.name


def normalize_cell_text(value: object) -> str:
    if value is None:
        return ""
    return str(value).strip()


def normalize_sequence_id(value: object) -> str:
    text = normalize_cell_text(value)
    if not text:
        return ""
    if re.fullmatch(r"[+-]?\d+", text):
        return str(int(text))
    return text


def sort_sequence_keys(keys: set[str]) -> list[str]:
    def key_func(seq: str) -> tuple[int, int | str]:
        if re.fullmatch(r"[+-]?\d+", seq):
            return (0, int(seq))
        return (1, seq)

    return sorted(keys, key=key_func)


def parse_required_float(row: dict[str, str], key: str, context: str) -> float:
    value = row.get(key)
    if value is None or value == "":
        fail(f"Missing '{key}' in summary row ({context}).")
    try:
        return float(value)
    except ValueError as error:
        fail(f"Invalid '{key}' value '{value}' in summary row ({context}): {error}")


def parse_required_int(row: dict[str, str], key: str, context: str) -> int:
    value = row.get(key)
    if value is None or value == "":
        fail(f"Missing '{key}' in summary row ({context}).")
    try:
        return int(value)
    except ValueError as error:
        fail(f"Invalid '{key}' value '{value}' in summary row ({context}): {error}")


def load_summary_rows(summary_csv: Path) -> dict[tuple[str, int], dict[str, str]]:
    if not summary_csv.is_file():
        fail(f"summary.csv not found: {summary_csv}")

    with summary_csv.open(newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        required_columns = {"qp", "case", "bitrate_bps", "map_50"}
        missing_columns = required_columns.difference(set(reader.fieldnames or []))
        if missing_columns:
            fail(f"summary.csv missing required columns: {sorted(missing_columns)}")

        by_case_qp: dict[tuple[str, int], dict[str, str]] = {}
        for row in reader:
            case_name = row.get("case", "")
            qp_text = row.get("qp", "")
            if not case_name or not qp_text:
                continue
            try:
                qp = int(qp_text)
            except ValueError:
                fail(f"Invalid qp value in summary.csv: '{qp_text}'")
            by_case_qp[(case_name, qp)] = row

    if not by_case_qp:
        fail(f"No valid rows found in {summary_csv}")
    return by_case_qp


def discover_multi_sequence_summaries(results_dir: Path) -> dict[str, Path]:
    summaries: dict[str, Path] = {}
    for child in sorted(results_dir.iterdir()):
        if not child.is_dir():
            continue
        summary_csv = child / "summary.csv"
        if not summary_csv.is_file():
            continue
        sequence_key = normalize_sequence_id(child.name)
        if not sequence_key:
            fail(f"Could not normalize sequence id from directory name: {child.name}")
        if sequence_key in summaries:
            fail(
                "Duplicate normalized sequence id detected under results directory: "
                f"{sequence_key} (from {child.name} and {summaries[sequence_key].parent.name})"
            )
        summaries[sequence_key] = summary_csv
    return summaries


def load_sequence_results(
    results_dir: Path, explicit_sequence_raw: str
) -> tuple[dict[str, dict[tuple[str, int], dict[str, str]]], dict[str, Path], str]:
    explicit_sequence_key = normalize_sequence_id(explicit_sequence_raw)
    if explicit_sequence_raw and not explicit_sequence_key:
        fail(f"Could not normalize --sequence value: '{explicit_sequence_raw}'")

    root_summary = results_dir / "summary.csv"
    if root_summary.is_file():
        mode = "single-sequence"
        inferred_sequence_raw = explicit_sequence_raw or infer_sequence_name(results_dir)
        sequence_key = normalize_sequence_id(inferred_sequence_raw)
        if not sequence_key:
            fail(
                "Could not infer sequence id from --results-dir name. "
                "Provide --sequence explicitly."
            )
        sequence_rows = {sequence_key: load_summary_rows(root_summary)}
        summary_paths = {sequence_key: root_summary}
        return sequence_rows, summary_paths, mode

    mode = "multi-sequence"
    summary_paths = discover_multi_sequence_summaries(results_dir)
    if not summary_paths:
        fail(
            f"Could not find summary.csv in {results_dir}. Expected either "
            "results-dir/summary.csv or results-dir/<sequence>/summary.csv."
        )

    if explicit_sequence_key:
        if explicit_sequence_key not in summary_paths:
            available = ", ".join(sort_sequence_keys(set(summary_paths.keys()))[:30])
            suffix = " ..." if len(summary_paths) > 30 else ""
            fail(
                f"--sequence '{explicit_sequence_raw}' (normalized '{explicit_sequence_key}') "
                f"not found in results. Available: {available}{suffix}"
            )
        summary_paths = {explicit_sequence_key: summary_paths[explicit_sequence_key]}

    sequence_rows = {sequence_key: load_summary_rows(path) for sequence_key, path in summary_paths.items()}
    return sequence_rows, summary_paths, mode


def resolve_sheet_name(sheetnames: list[str], requested_sheet: str) -> str:
    if requested_sheet in sheetnames:
        return requested_sheet

    # Keep backward compatibility with SFU defaults while allowing PandaSet templates.
    if requested_sheet == DEFAULT_SHEET:
        fallback_priority = [
            "Pandaset_RA_Inner",
            "SFU-HW_RA_Inner",
        ]
        for candidate in fallback_priority:
            if candidate in sheetnames:
                print(f"INFO: Sheet '{requested_sheet}' not found; using '{candidate}'.")
                return candidate

        ra_inner_sheets = sorted(name for name in sheetnames if name.endswith("_RA_Inner"))
        if len(ra_inner_sheets) == 1:
            print(f"INFO: Sheet '{requested_sheet}' not found; using '{ra_inner_sheets[0]}'.")
            return ra_inner_sheets[0]

    available = ", ".join(sheetnames)
    fail(f"Sheet '{requested_sheet}' not found in template. Available sheets: {available}")


def find_sequence_start_rows(worksheet) -> dict[str, int]:
    start_rows: dict[str, int] = {}
    for row_index in range(4, worksheet.max_row + 1):
        qpislice_cell = normalize_cell_text(worksheet.cell(row=row_index, column=3).value).lower()
        if qpislice_cell != "r0":
            continue
        sequence_key = normalize_sequence_id(worksheet.cell(row=row_index, column=2).value)
        if not sequence_key:
            continue
        if sequence_key in start_rows:
            fail(
                f"Duplicate sequence '{sequence_key}' r0 rows in sheet '{worksheet.title}': "
                f"rows {start_rows[sequence_key]} and {row_index}"
            )
        start_rows[sequence_key] = row_index
    if not start_rows:
        fail(f"Sheet '{worksheet.title}' has no sequence rows (column B + column C='r0').")
    return start_rows


def validate_sequences_in_sheet(
    sequence_summaries: dict[str, dict[tuple[str, int], dict[str, str]]],
    sheet_sequence_rows: dict[str, int],
    sheet_name: str,
) -> dict[str, int]:
    requested_keys = set(sequence_summaries.keys())
    available_keys = set(sheet_sequence_rows.keys())
    missing = requested_keys.difference(available_keys)
    if missing:
        missing_list = ", ".join(sort_sequence_keys(missing))
        available_preview = ", ".join(sort_sequence_keys(available_keys)[:30])
        suffix = " ..." if len(available_keys) > 30 else ""
        fail(
            f"Worksheet '{sheet_name}' missing sequence rows for: {missing_list}. "
            f"Available worksheet sequences: {available_preview}{suffix}"
        )
    return {key: sheet_sequence_rows[key] for key in requested_keys}


def ordered_test_cases(all_cases: set[str]) -> list[str]:
    # Keep legacy non-anchor cases first to preserve established output ordering
    # and downstream workbook naming expectations.
    preferred_order = [
        "mode_roi_packing",
        "mode_background_blur",
        "mode_tet",
        "mode_luma_scaling",
        "mode_adaptive_luma_scaling",
        *QP_MAP_CASES,
    ]
    cases = [case_name for case_name in preferred_order if case_name in all_cases and case_name != ANCHOR_CASE]
    remaining = sorted(case_name for case_name in all_cases if case_name not in cases and case_name != ANCHOR_CASE)
    return cases + remaining


def collect_cases_and_qps(
    sequence_summaries: dict[str, dict[tuple[str, int], dict[str, str]]]
) -> tuple[list[str], list[int]]:
    common_cases: set[str] | None = None
    common_qps: set[int] | None = None
    per_sequence_cases: dict[str, set[str]] = {}
    per_sequence_qps: dict[str, set[int]] = {}

    for sequence_key, by_case_qp in sequence_summaries.items():
        sequence_cases = {case_name for case_name, _ in by_case_qp.keys()}
        sequence_qps = {qp for _, qp in by_case_qp.keys()}
        if ANCHOR_CASE not in sequence_cases:
            fail(f"Anchor case '{ANCHOR_CASE}' not found in summary rows for sequence '{sequence_key}'.")
        per_sequence_cases[sequence_key] = sequence_cases
        per_sequence_qps[sequence_key] = sequence_qps
        if common_cases is None:
            common_cases = set(sequence_cases)
            common_qps = set(sequence_qps)
        else:
            common_cases.intersection_update(sequence_cases)
            common_qps.intersection_update(sequence_qps)

    if not common_qps:
        fail("No QP values found in loaded summary rows.")
    qps = sorted(common_qps)

    # Keep workbook generation stable when some sequences miss optional modes
    # (for example qp_map_delta rows excluded by run-time gating).
    common_non_anchor_cases = {case_name for case_name in (common_cases or set()) if case_name != ANCHOR_CASE}
    if len(sequence_summaries) > 1:
        union_non_anchor_cases = set().union(
            *(sequence_cases.difference({ANCHOR_CASE}) for sequence_cases in per_sequence_cases.values())
        )
        skipped_cases = union_non_anchor_cases.difference(common_non_anchor_cases)
        if skipped_cases:
            print(
                "INFO: Skipping non-common cases across sequences: "
                + ", ".join(sorted(skipped_cases))
            )

        union_qps = set().union(*per_sequence_qps.values())
        skipped_qps = union_qps.difference(common_qps)
        if skipped_qps:
            print(
                "INFO: Skipping non-common QPs across sequences: "
                + ", ".join(str(qp) for qp in sorted(skipped_qps))
            )

    test_cases = ordered_test_cases(common_cases or set())
    if not test_cases:
        fail("No non-anchor test cases found in loaded summary rows.")
    return test_cases, qps


def write_case_workbook(
    template_path: Path,
    output_path: Path,
    sheet_name: str,
    sequence_start_rows: dict[str, int],
    sequence_summaries: dict[str, dict[tuple[str, int], dict[str, str]]],
    test_case: str,
    qps: list[int],
) -> None:
    shutil.copy2(template_path, output_path)
    workbook = load_workbook(output_path, keep_vba=True)

    if sheet_name not in workbook.sheetnames:
        fail(f"Sheet '{sheet_name}' not found in {output_path}")
    worksheet = workbook[sheet_name]

    ordered_sequences = sorted(sequence_start_rows.items(), key=lambda item: item[1])
    for sequence_key, start_row in ordered_sequences:
        by_case_qp = sequence_summaries[sequence_key]

        for index, qp in enumerate(qps):
            row_index = start_row + index
            expected_r = f"r{index}"
            actual_r = normalize_cell_text(worksheet.cell(row=row_index, column=3).value).lower()
            if actual_r != expected_r:
                fail(
                    f"Unexpected QP row layout for sequence '{sequence_key}' in sheet '{worksheet.title}': "
                    f"row {row_index} has '{actual_r}', expected '{expected_r}'."
                )

            anchor_row = by_case_qp.get((ANCHOR_CASE, qp))
            test_row = by_case_qp.get((test_case, qp))
            if anchor_row is None or test_row is None:
                fail(
                    f"Missing summary row for sequence={sequence_key}, qp={qp} "
                    f"(anchor={ANCHOR_CASE}, test={test_case})."
                )

            anchor_context = f"sequence={sequence_key}, case={ANCHOR_CASE}, qp={qp}"
            test_context = f"sequence={sequence_key}, case={test_case}, qp={qp}"

            anchor_bitrate_bps = parse_required_int(anchor_row, "bitrate_bps", anchor_context)
            test_bitrate_bps = parse_required_int(test_row, "bitrate_bps", test_context)

            anchor_map_50 = parse_required_float(anchor_row, "map_50", anchor_context)
            test_map_50 = parse_required_float(test_row, "map_50", test_context)

            # Excel template expects percentage-style mAP values.
            anchor_map_percent = anchor_map_50 * 100.0
            test_map_percent = test_map_50 * 100.0

            worksheet.cell(row=row_index, column=ANCHOR_COL_KBPS).value = anchor_bitrate_bps / 1000.0
            worksheet.cell(row=row_index, column=ANCHOR_COL_MONO_MAP).value = anchor_map_percent
            worksheet.cell(row=row_index, column=ANCHOR_COL_MAP).value = anchor_map_percent

            worksheet.cell(row=row_index, column=TEST_COL_KBPS).value = test_bitrate_bps / 1000.0
            worksheet.cell(row=row_index, column=TEST_COL_MONO_MAP).value = test_map_percent
            worksheet.cell(row=row_index, column=TEST_COL_MAP).value = test_map_percent

    if SUMMARY_SHEET in workbook.sheetnames:
        summary_sheet = workbook[SUMMARY_SHEET]
        summary_sheet["Y5"] = test_case

    workbook.save(output_path)
    workbook.close()


def main() -> int:
    args = parse_args()

    results_dir = args.results_dir.resolve()
    template_path = args.excel_template.resolve()

    if not results_dir.is_dir():
        fail(f"Results directory not found: {results_dir}")
    if not template_path.is_file():
        fail(f"Excel template not found: {template_path}")

    output_dir = Path(args.output_dir).resolve() if args.output_dir else results_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    sequence_summaries, summary_paths, mode = load_sequence_results(results_dir, args.sequence.strip())
    test_cases, qps = collect_cases_and_qps(sequence_summaries)

    probe_workbook = load_workbook(template_path, keep_vba=True, data_only=True)
    resolved_sheet = resolve_sheet_name(probe_workbook.sheetnames, args.sheet)
    sheet_sequence_rows = find_sequence_start_rows(probe_workbook[resolved_sheet])
    matched_sequence_rows = validate_sequences_in_sheet(sequence_summaries, sheet_sequence_rows, resolved_sheet)
    probe_workbook.close()

    created_files: list[Path] = []
    for test_case in test_cases:
        output_path = output_dir / f"{template_path.stem}_{test_case}{template_path.suffix}"
        write_case_workbook(
            template_path=template_path,
            output_path=output_path,
            sheet_name=resolved_sheet,
            sequence_start_rows=matched_sequence_rows,
            sequence_summaries=sequence_summaries,
            test_case=test_case,
            qps=qps,
        )
        created_files.append(output_path)

    sequence_keys = sort_sequence_keys(set(sequence_summaries.keys()))
    print(f"Mode: {mode}")
    print(f"Sheet: {resolved_sheet}")
    print(f"Sequences filled ({len(sequence_keys)}): {', '.join(sequence_keys)}")
    print(f"QP values: {' '.join(str(qp) for qp in qps)}")
    print("Summary sources:")
    for sequence_key in sequence_keys:
        print(f"  {sequence_key}: {summary_paths[sequence_key]}")
    print("Created files:")
    for created_file in created_files:
        print(created_file)
    return 0


if __name__ == "__main__":
    sys.exit(main())
