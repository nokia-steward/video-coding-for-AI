#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

"""
Evaluate sparse semseg groundtruth using AH0187 Panoptic FPN model.

This script runs Detectron2 Panoptic FPN R-101-FPN 3x inference on per-frame
PNG images and compares predictions against sparse semseg points in the same
schema used by
`scripts/tet_comparison_framework/tools/convert_pandaset_semseg_pkl_to_json.py`.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

import cv2
import numpy as np

from detectron2 import model_zoo
from detectron2.config import get_cfg
from detectron2.engine import DefaultPredictor

from vcm_mappings import (
    coco_stuff_to_pandaset,
    coco_things_to_pandaset,
    pandaset_to_vcm_category,
    vcm_category,
    vcm_eval_category,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Evaluate sparse semseg points with AH0187 Panoptic FPN."
    )
    parser.add_argument("--frames-dir", required=True, help="Directory with frame_*.png files.")
    parser.add_argument("--groundtruth-json", required=True, help="Sparse semseg groundtruth JSON path.")
    parser.add_argument("--output-json", required=True, help="Output semseg summary JSON path.")
    parser.add_argument("--output-md", required=True, help="Output semseg summary markdown path.")
    parser.add_argument("--port", type=int, default=5000, help="Port value stored in summary files.")
    parser.add_argument(
        "--device",
        default="cuda",
        choices=["cuda", "cpu"],
        help="Detectron2 device to use (default: cuda).",
    )
    return parser.parse_args()


def load_ah0187_mappings() -> dict[str, Any]:
    return {
        "coco_stuff_to_pandaset": coco_stuff_to_pandaset,
        "coco_things_to_pandaset": coco_things_to_pandaset,
        "pandaset_to_vcm_category": pandaset_to_vcm_category,
        "vcm_eval_category": list(vcm_eval_category),
        "vcm_category": vcm_category,
    }


def build_predictor(device: str) -> DefaultPredictor:
    cfg = get_cfg()
    cfg.MODEL.ROI_HEADS.SCORE_THRESH_TEST = 0.0
    cfg.MODEL.DEVICE = device
    cfg.merge_from_file(
        model_zoo.get_config_file("COCO-PanopticSegmentation/panoptic_fpn_R_101_3x.yaml")
    )
    cfg.MODEL.WEIGHTS = model_zoo.get_checkpoint_url(
        "COCO-PanopticSegmentation/panoptic_fpn_R_101_3x.yaml"
    )
    return DefaultPredictor(cfg)


def read_groundtruth(groundtruth_path: Path) -> dict[str, Any]:
    payload = json.loads(groundtruth_path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise RuntimeError("Groundtruth payload root must be an object.")
    frames = payload.get("frames")
    if not isinstance(frames, list):
        raise RuntimeError("Groundtruth payload missing 'frames' array.")
    return payload


def discover_frames(frames_dir: Path) -> tuple[list[Path], dict[int, Path]]:
    frames = sorted(frames_dir.glob("frame_*.png"))
    if not frames:
        raise RuntimeError(f"No frame PNGs found in {frames_dir}")

    by_number: dict[int, Path] = {}
    matcher = re.compile(r"frame_(\d+)\.png$")
    for frame in frames:
        match = matcher.search(frame.name)
        if not match:
            continue
        by_number[int(match.group(1))] = frame
    return frames, by_number


def resolve_frame_path(
    frame_index: int, ordered_frames: list[Path], frames_by_number: dict[int, Path]
) -> Path | None:
    if frame_index in frames_by_number:
        return frames_by_number[frame_index]
    if (frame_index + 1) in frames_by_number:
        return frames_by_number[frame_index + 1]
    if 0 <= frame_index < len(ordered_frames):
        return ordered_frames[frame_index]
    return None


def get_eval_category_ids(payload: dict[str, Any], fallback_ids: list[int]) -> list[int]:
    eval_ids = payload.get("eval_category_ids")
    if isinstance(eval_ids, list) and eval_ids:
        parsed = [int(value) for value in eval_ids if isinstance(value, int)]
        if parsed:
            return parsed
    return list(fallback_ids)


def get_category_name_map(payload: dict[str, Any], fallback: dict[int, str]) -> dict[int, str]:
    names = dict(fallback)
    categories = payload.get("categories")
    if isinstance(categories, list):
        for entry in categories:
            if (
                isinstance(entry, dict)
                and isinstance(entry.get("id"), int)
                and isinstance(entry.get("name"), str)
            ):
                names[entry["id"]] = entry["name"]
    return names


def predict_vcm_for_points(
    predictor: DefaultPredictor,
    frame_path: Path,
    points: list[dict[str, Any]],
    eval_set: set[int],
    mappings: dict[str, Any],
) -> list[tuple[int, int]]:
    image = cv2.imread(str(frame_path))
    if image is None:
        raise RuntimeError(f"Failed to read frame image: {frame_path}")

    outputs = predictor(image)
    panoptic_out = outputs.get("panoptic_seg")
    if not isinstance(panoptic_out, tuple) or len(panoptic_out) != 2:
        raise RuntimeError("Detectron2 output missing panoptic_seg tuple.")

    panoptic_seg, segments_info = panoptic_out
    if segments_info is None:
        raise RuntimeError("segments_info is missing from panoptic output.")

    panoptic_np = panoptic_seg.cpu().numpy()
    height, width = panoptic_np.shape

    segment_to_vcm: dict[int, int] = {}
    coco_stuff_to_pandaset = mappings["coco_stuff_to_pandaset"]
    coco_things_to_pandaset = mappings["coco_things_to_pandaset"]
    pandaset_to_vcm_category = mappings["pandaset_to_vcm_category"]
    for entry in segments_info:
        if not isinstance(entry, dict):
            continue
        segment_id = int(entry.get("id", -1))
        category_id = int(entry.get("category_id", -1))
        isthing = bool(entry.get("isthing", False))
        if segment_id < 0 or category_id < 0:
            continue
        if isthing:
            pandaset_id = int(coco_things_to_pandaset.get(category_id, 0))
        else:
            pandaset_id = int(coco_stuff_to_pandaset.get(category_id, 0))
        vcm_id = int(pandaset_to_vcm_category.get(pandaset_id, 0))
        segment_to_vcm[segment_id] = vcm_id

    results: list[tuple[int, int]] = []
    for point in points:
        if not isinstance(point, dict):
            continue
        x = int(point.get("x", -1))
        y = int(point.get("y", -1))
        gt_class = int(point.get("class_id", -1))
        if gt_class not in eval_set:
            continue
        if x < 0 or y < 0 or x >= width or y >= height:
            continue
        segment_id = int(panoptic_np[y, x])
        pred_class = int(segment_to_vcm.get(segment_id, 0))
        results.append((gt_class, pred_class))
    return results


def safe_divide(numerator: float, denominator: float) -> float:
    if denominator <= 0.0:
        return 0.0
    return numerator / denominator


def write_markdown(
    output_path: Path,
    port: int,
    frames_with_groundtruth: int,
    points_evaluated: int,
    mean_iou: float,
    class_rows: list[dict[str, Any]],
    frame_rows: list[dict[str, Any]],
) -> None:
    lines: list[str] = [
        "# SemSeg Test Mode Summary",
        "",
        f"- Port: {port}",
        f"- Frames with groundtruth: {frames_with_groundtruth}",
        f"- Evaluated points: {points_evaluated}",
        f"- Mean IoU: {mean_iou:.3f}",
        "",
        "## Per-class IoU",
        "",
    ]
    if not class_rows:
        lines.append("- No per-class semseg results captured")
    else:
        for row in class_rows:
            lines.append(
                f"- Class {row['class_id']} ({row['class_name']}): "
                f"intersection={row['intersection']} union={row['union']} iou={row['iou']:.3f}"
            )

    lines.extend(["", "## Frame Results", ""])
    if not frame_rows:
        lines.append("- No semseg frame results captured")
    else:
        for row in frame_rows:
            lines.append(
                f"- Frame {row['frame_index']}: points={row['groundtruth_point_count']} "
                f"active_classes={row['classes_with_union']} mean_iou={row['mean_iou']:.3f}"
            )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    mappings = load_ah0187_mappings()

    predictor = build_predictor(args.device)
    groundtruth_path = Path(args.groundtruth_json).expanduser().resolve()
    frames_dir = Path(args.frames_dir).expanduser().resolve()
    output_json = Path(args.output_json).expanduser().resolve()
    output_md = Path(args.output_md).expanduser().resolve()

    payload = read_groundtruth(groundtruth_path)
    eval_ids = get_eval_category_ids(payload, mappings["vcm_eval_category"])
    eval_set = set(eval_ids)
    vcm_category = mappings["vcm_category"]
    category_names = get_category_name_map(payload, {int(k): str(v) for k, v in vcm_category.items()})

    ordered_frames, frames_by_number = discover_frames(frames_dir)
    metrics: dict[int, dict[str, int]] = {
        class_id: {"intersection": 0, "union": 0} for class_id in eval_ids
    }
    frame_rows: list[dict[str, Any]] = []
    frames_with_groundtruth = 0
    points_evaluated = 0

    frames_payload = payload.get("frames", [])
    for frame_entry in frames_payload:
        if not isinstance(frame_entry, dict):
            continue
        frame_index = int(frame_entry.get("frame_index", -1))
        if frame_index < 0:
            continue
        points = frame_entry.get("points", [])
        if not isinstance(points, list) or not points:
            continue

        frame_path = resolve_frame_path(frame_index, ordered_frames, frames_by_number)
        if frame_path is None:
            continue

        gt_pred_pairs = predict_vcm_for_points(predictor, frame_path, points, eval_set, mappings)
        if not gt_pred_pairs:
            continue

        frames_with_groundtruth += 1
        points_evaluated += len(gt_pred_pairs)

        frame_metrics = {class_id: {"intersection": 0, "union": 0} for class_id in eval_ids}
        for gt_class, pred_class in gt_pred_pairs:
            pred_in_eval = pred_class in eval_set
            for class_id in eval_ids:
                gt_is_class = gt_class == class_id
                pred_is_class = pred_in_eval and (pred_class == class_id)
                if gt_is_class and pred_is_class:
                    metrics[class_id]["intersection"] += 1
                    frame_metrics[class_id]["intersection"] += 1
                if gt_is_class or pred_is_class:
                    metrics[class_id]["union"] += 1
                    frame_metrics[class_id]["union"] += 1

        classes_with_union = 0
        iou_sum = 0.0
        for class_id in eval_ids:
            union_count = frame_metrics[class_id]["union"]
            if union_count <= 0:
                continue
            classes_with_union += 1
            iou_sum += safe_divide(frame_metrics[class_id]["intersection"], union_count)
        frame_mean_iou = safe_divide(iou_sum, classes_with_union)
        frame_rows.append(
            {
                "frame_index": frame_index,
                "groundtruth_point_count": len(gt_pred_pairs),
                "classes_with_union": classes_with_union,
                "mean_iou": frame_mean_iou,
            }
        )

    class_rows: list[dict[str, Any]] = []
    classes_with_union = 0
    iou_sum = 0.0
    for class_id in eval_ids:
        intersection = int(metrics[class_id]["intersection"])
        union_count = int(metrics[class_id]["union"])
        iou_value = safe_divide(intersection, union_count)
        if union_count > 0:
            classes_with_union += 1
            iou_sum += iou_value
        class_rows.append(
            {
                "class_id": class_id,
                "class_name": category_names.get(class_id, f"class_{class_id}"),
                "intersection": intersection,
                "union": union_count,
                "iou": iou_value,
            }
        )
    mean_iou = safe_divide(iou_sum, classes_with_union)

    output_payload = {
        "port": int(args.port),
        "has_groundtruth": frames_with_groundtruth > 0,
        "frames_with_groundtruth": frames_with_groundtruth,
        "points_evaluated": points_evaluated,
        "mean_iou": mean_iou,
        "classes": class_rows,
        "frames": frame_rows,
        "model_backend": "detectron2_panoptic_fpn_r101_3x_ah0187",
    }
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(output_payload, indent=2) + "\n", encoding="utf-8")

    write_markdown(
        output_md,
        args.port,
        frames_with_groundtruth,
        points_evaluated,
        mean_iou,
        class_rows,
        frame_rows,
    )
    print(f"Wrote semseg summary JSON: {output_json}")
    print(f"Wrote semseg summary markdown: {output_md}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
