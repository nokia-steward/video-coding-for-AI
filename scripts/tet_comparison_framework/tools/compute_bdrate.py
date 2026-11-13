#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

"""Compute Bjontegaard Delta Bitrate (BD-rate) between TET-off and TET-on curves.

BD-rate measures the average bitrate difference between two rate-distortion
curves at equal quality.  A *negative* BD-rate means the test curve (TET-on)
needs fewer bits to achieve the same quality as the anchor (TET-off) — i.e.
TET helps.  A positive value means TET hurts.

Algorithm
---------
1. Take two curves — anchor (TET-off) and test (TET-on) — each a set of
   (bitrate, metric) points.
2. Convert bitrates to log10 scale.
3. Fit a polynomial (up to 3rd order) to each curve: log_rate = f(metric).
4. Integrate both polynomials over the overlapping metric range.
5. BD-rate = (10^(avg_diff) - 1) * 100 %

Required CSV format
-------------------
The input CSV must have at least these columns::

    qp,tet,bitrate_bps,<metric>

where ``tet`` is ``off`` (anchor) or ``on`` (test), and ``<metric>`` is one
of ``precision``, ``recall``, ``f1_score``, ``mean_iou``.

Usage
-----
::

    python3 scripts/tet_comparison_framework/tools/compute_bdrate.py results/bdrate_input.csv --metric f1_score
    python3 scripts/tet_comparison_framework/tools/compute_bdrate.py results/bdrate_input.csv --metric mean_iou
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
from pathlib import Path

try:
    import numpy as np
except ImportError:
    print(
        "ERROR: numpy is required.  Install it with:  pip install numpy",
        file=sys.stderr,
    )
    sys.exit(1)


def bd_rate(
    rate_anchor: list[float],
    metric_anchor: list[float],
    rate_test: list[float],
    metric_test: list[float],
) -> float:
    """Compute BD-rate (%) between anchor and test curves.

    Parameters
    ----------
    rate_anchor:
        Bitrates for the anchor curve (at least 4 points recommended).
    metric_anchor:
        Quality metric values for the anchor curve.
    rate_test:
        Bitrates for the test curve.
    metric_test:
        Quality metric values for the test curve.

    Returns
    -------
    float
        BD-rate in percent.  Negative means the test curve is better (needs
        fewer bits for the same quality).
    """
    log_rate_a = np.log10(np.array(rate_anchor, dtype=float))
    log_rate_t = np.log10(np.array(rate_test, dtype=float))
    metric_a = np.array(metric_anchor, dtype=float)
    metric_t = np.array(metric_test, dtype=float)

    # Sort both curves by metric value (ascending)
    order_a = np.argsort(metric_a)
    order_t = np.argsort(metric_t)
    metric_a, log_rate_a = metric_a[order_a], log_rate_a[order_a]
    metric_t, log_rate_t = metric_t[order_t], log_rate_t[order_t]

    # Overlapping metric range
    min_metric = max(metric_a.min(), metric_t.min())
    max_metric = min(metric_a.max(), metric_t.max())
    if max_metric <= min_metric:
        return float("nan")

    # Fit polynomials: log_rate = f(metric)
    # Use up to 3rd order, but clamp to (num_points - 1) to avoid overfitting
    poly_degree_a = min(3, len(metric_a) - 1)
    poly_degree_t = min(3, len(metric_t) - 1)
    poly_a = np.polyfit(metric_a, log_rate_a, poly_degree_a)
    poly_t = np.polyfit(metric_t, log_rate_t, poly_degree_t)

    # Integrate over the common metric range
    p_int_a = np.polyint(poly_a)
    p_int_t = np.polyint(poly_t)
    integral_a = np.polyval(p_int_a, max_metric) - np.polyval(p_int_a, min_metric)
    integral_t = np.polyval(p_int_t, max_metric) - np.polyval(p_int_t, min_metric)

    avg_diff = (integral_t - integral_a) / (max_metric - min_metric)
    return (10.0**avg_diff - 1.0) * 100.0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Compute Bjontegaard Delta Bitrate (BD-rate) from TET test results.\n\n"
            "A negative BD-rate means TET-on needs fewer bits for the same quality\n"
            "(i.e. TET helps). A positive value means TET costs more bits."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Examples:\n"
            "  python3 scripts/tet_comparison_framework/tools/compute_bdrate.py results/bdrate_input.csv --metric f1_score\n"
            "  python3 scripts/tet_comparison_framework/tools/compute_bdrate.py results/bdrate_input.csv --metric mean_iou\n"
        ),
    )
    parser.add_argument(
        "csv_file",
        type=Path,
        help="Path to results CSV (must contain columns: qp, tet, bitrate_bps, <metric>)",
    )
    parser.add_argument(
        "--metric",
        default="f1_score",
        choices=["precision", "recall", "f1_score", "mean_iou"],
        help=(
            "Quality metric to use for BD-rate computation. "
            "precision: detection precision, "
            "recall: detection recall, "
            "f1_score: harmonic mean of precision and recall (default), "
            "mean_iou: mean intersection-over-union"
        ),
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print per-QP data points",
    )
    args = parser.parse_args()

    if not args.csv_file.exists():
        print(f"ERROR: CSV file not found: {args.csv_file}", file=sys.stderr)
        return 1

    rows = list(csv.DictReader(args.csv_file.open()))
    if not rows:
        print("ERROR: CSV file is empty.", file=sys.stderr)
        return 1

    # Separate anchor (TET-off) and test (TET-on) data
    anchor_rates: list[float] = []
    anchor_metrics: list[float] = []
    test_rates: list[float] = []
    test_metrics: list[float] = []

    for row in rows:
        try:
            bitrate = float(row["bitrate_bps"])
            metric_val = float(row[args.metric])
        except (KeyError, ValueError) as exc:
            print(f"WARNING: Skipping row due to error: {exc}", file=sys.stderr)
            continue

        if bitrate <= 0 or math.isnan(metric_val):
            qp = row.get("qp", "?")
            tet = row.get("tet", "?")
            print(
                f"WARNING: Skipping QP={qp} TET={tet} "
                f"(bitrate={bitrate}, {args.metric}={metric_val})",
                file=sys.stderr,
            )
            continue

        if row["tet"] == "off":
            anchor_rates.append(bitrate)
            anchor_metrics.append(metric_val)
        else:
            test_rates.append(bitrate)
            test_metrics.append(metric_val)

    if args.verbose:
        print(f"\nAnchor (TET-off) data points ({args.metric}):")
        for rate, metric_val in sorted(zip(anchor_rates, anchor_metrics)):
            print(f"  bitrate={rate:.0f} bps  {args.metric}={metric_val:.4f}")
        print(f"\nTest (TET-on) data points ({args.metric}):")
        for rate, metric_val in sorted(zip(test_rates, test_metrics)):
            print(f"  bitrate={rate:.0f} bps  {args.metric}={metric_val:.4f}")
        print()

    min_points = 4
    if len(anchor_rates) < min_points or len(test_rates) < min_points:
        print(
            f"Need at least {min_points} valid points per curve for reliable BD-rate. "
            f"Got {len(anchor_rates)} anchor (TET-off), {len(test_rates)} test (TET-on)."
        )
        # Still try to compute if we have at least 2 points
        if len(anchor_rates) < 2 or len(test_rates) < 2:
            print("ERROR: Need at least 2 points per curve. Cannot compute BD-rate.")
            return 1
        print("WARNING: Computing BD-rate with fewer than 4 points (results may be unreliable).")

    result = bd_rate(anchor_rates, anchor_metrics, test_rates, test_metrics)

    if math.isnan(result):
        print(f"BD-rate ({args.metric}): NaN (no overlapping metric range)")
        return 1

    print(f"BD-rate ({args.metric}): {result:+.2f}%")
    if result < 0:
        print(f"  -> TET saves {-result:.2f}% bitrate at equal {args.metric}")
    elif result > 0:
        print(f"  -> TET costs {result:.2f}% MORE bitrate at equal {args.metric}")
    else:
        print("  -> No difference")

    return 0


if __name__ == "__main__":
    sys.exit(main())
