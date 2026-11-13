#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

"""Convert PandaSet cuboids into a COCO 5-class JSON dataset.

Contract implemented by this converter:
- Input dataset root is provided via --dataset-dir.
- Uses cuboids from annotations/cuboids/*.pkl only.
- Uses camera intrinsics/poses from camera/<camera>/intrinsics.json and poses.json.
- Emits strict 5-class COCO categories:
  person=1, car=3, motorcycle=4, bus=6, truck=8.
- Emits synthetic deterministic image names:
  pandaset_<width>x<height>_<fps>_<frame:03d>.jpg
- Emits bbox_mode=1 and class=<display-name> for compatibility with existing style.
- Drops unknown labels and invalid/tiny projected boxes.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

import numpy as np
import pandas as pd


CATEGORY_ID_BY_NAME = {
    "person": 1,
    "car": 3,
    "motorcycle": 4,
    "bus": 6,
    "truck": 8,
}

CATEGORIES = [
    {"id": 1, "name": "person"},
    {"id": 3, "name": "car"},
    {"id": 4, "name": "motorcycle"},
    {"id": 6, "name": "bus"},
    {"id": 8, "name": "truck"},
]

SOURCE_TO_TARGET = {
    "pedestrian": "person",
    "pedestrian with object": "person",
    "car": "car",
    "pickup truck": "truck",
    "medium-sized truck": "truck",
    "semi-truck": "truck",
    "other vehicle - construction vehicle": "truck",
    "emergency vehicle": "truck",
    "motorcycle": "motorcycle",
    "bus": "bus",
}

# Cuboid edges as corner-index pairs.
CUBOID_EDGES = (
    (0, 1),
    (1, 2),
    (2, 3),
    (3, 0),
    (4, 5),
    (5, 6),
    (6, 7),
    (7, 4),
    (0, 4),
    (1, 5),
    (2, 6),
    (3, 7),
)


class ConverterError(RuntimeError):
    """Raised for actionable converter errors."""


@dataclass(frozen=True)
class Intrinsics:
    fx: float
    fy: float
    cx: float
    cy: float


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert PandaSet cuboids to COCO 5-class JSON.")
    parser.add_argument("--dataset-dir", required=True, help="PandaSet dataset root directory")
    parser.add_argument("--camera", default="front_camera", help="Camera name under dataset_dir/camera/")
    parser.add_argument(
        "--yuv",
        default="",
        help="Optional explicit YUV path for frame-count validation. "
        "If omitted, the converter tries to infer a YUV file under dataset root.",
    )
    parser.add_argument(
        "--output",
        default="",
        help="Output COCO JSON path. Defaults to dataset_dir/annotations/"
        "pandaset_<width>x<height>_<fps>_coco5.json",
    )
    parser.add_argument("--min-bbox-width", type=float, default=2.0)
    parser.add_argument("--min-bbox-height", type=float, default=2.0)
    parser.add_argument("--min-bbox-area", type=float, default=16.0)
    parser.add_argument("--overwrite", action="store_true", help="Overwrite output file if it already exists")
    parser.add_argument("--report-json", default="", help="Optional diagnostics report JSON output path")
    return parser.parse_args()


def load_json(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except FileNotFoundError as exc:
        raise ConverterError(f"Required JSON file not found: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ConverterError(f"Invalid JSON in {path}: {exc}") from exc


def parse_numeric_suffix(path: Path) -> int:
    match = re.search(r"(\d+)$", path.stem)
    if not match:
        raise ConverterError(f"Expected numeric frame stem, got: {path.name}")
    return int(match.group(1))


def get_sorted_files_with_suffix(directory: Path, pattern: str) -> list[Path]:
    files = sorted(directory.glob(pattern), key=parse_numeric_suffix)
    if not files:
        raise ConverterError(f"No files matched {pattern} in {directory}")
    return files


def read_jpeg_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as handle:
        data = handle.read()
    if len(data) < 4 or data[0:2] != b"\xff\xd8":
        raise ConverterError(f"Not a valid JPEG file: {path}")

    index = 2
    sof_markers = {
        0xC0,
        0xC1,
        0xC2,
        0xC3,
        0xC5,
        0xC6,
        0xC7,
        0xC9,
        0xCA,
        0xCB,
        0xCD,
        0xCE,
        0xCF,
    }
    while index + 9 < len(data):
        if data[index] != 0xFF:
            index += 1
            continue
        marker = data[index + 1]
        index += 2
        if marker in (0xD8, 0xD9):
            continue
        if index + 2 > len(data):
            break
        segment_len = int.from_bytes(data[index:index + 2], byteorder="big")
        if segment_len < 2 or index + segment_len > len(data):
            break
        if marker in sof_markers:
            if index + 7 > len(data):
                break
            height = int.from_bytes(data[index + 3:index + 5], byteorder="big")
            width = int.from_bytes(data[index + 5:index + 7], byteorder="big")
            if width <= 0 or height <= 0:
                break
            return width, height
        index += segment_len

    raise ConverterError(f"Could not read JPEG dimensions from {path}")


def find_intrinsics_candidate(node: Any) -> dict[str, float] | None:
    if isinstance(node, dict):
        if {"fx", "fy", "cx", "cy"}.issubset(node.keys()):
            try:
                return {
                    "fx": float(node["fx"]),
                    "fy": float(node["fy"]),
                    "cx": float(node["cx"]),
                    "cy": float(node["cy"]),
                }
            except (TypeError, ValueError):
                return None
        for value in node.values():
            found = find_intrinsics_candidate(value)
            if found is not None:
                return found
    elif isinstance(node, list):
        for item in node:
            found = find_intrinsics_candidate(item)
            if found is not None:
                return found
    return None


def load_intrinsics(path: Path) -> Intrinsics:
    raw = load_json(path)
    found = find_intrinsics_candidate(raw)
    if found is None:
        raise ConverterError(f"Could not find fx/fy/cx/cy in {path}")
    return Intrinsics(fx=found["fx"], fy=found["fy"], cx=found["cx"], cy=found["cy"])


def normalize_pose_entry(entry: Any) -> dict[str, Any] | None:
    if not isinstance(entry, dict):
        return None
    heading = entry.get("heading")
    position = entry.get("position")
    if not isinstance(heading, dict) or not isinstance(position, dict):
        return None
    if not {"w", "x", "y", "z"}.issubset(heading.keys()):
        return None
    if not {"x", "y", "z"}.issubset(position.keys()):
        return None
    return entry


def load_pose_entries(path: Path) -> dict[int, dict[str, Any]]:
    raw = load_json(path)

    if isinstance(raw, list):
        pose_list = [normalize_pose_entry(item) for item in raw]
        if any(item is None for item in pose_list):
            raise ConverterError(f"Invalid pose entry structure in {path}")
        return {index: pose_list[index] for index in range(len(pose_list))}

    if isinstance(raw, dict):
        if "poses" in raw and isinstance(raw["poses"], list):
            pose_list = [normalize_pose_entry(item) for item in raw["poses"]]
            if any(item is None for item in pose_list):
                raise ConverterError(f"Invalid pose entry structure in {path}")
            return {index: pose_list[index] for index in range(len(pose_list))}

        numeric_items: dict[int, dict[str, Any]] = {}
        for key, value in raw.items():
            try:
                index = int(key)
            except (TypeError, ValueError):
                continue
            normalized = normalize_pose_entry(value)
            if normalized is not None:
                numeric_items[index] = normalized
        if numeric_items:
            return numeric_items

    raise ConverterError(f"Unsupported poses JSON structure in {path}")


def rotation_matrix_from_quaternion(w: float, x: float, y: float, z: float) -> np.ndarray:
    norm = math.sqrt(w * w + x * x + y * y + z * z)
    if norm <= 0.0:
        raise ConverterError("Invalid zero-norm quaternion in poses.json")
    w /= norm
    x /= norm
    y /= norm
    z /= norm
    return np.array(
        [
            [1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w)],
            [2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w)],
            [2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y)],
        ],
        dtype=np.float64,
    )


def extract_value(row: pd.Series, keys: Iterable[str]) -> float | str | None:
    for key in keys:
        if key in row and pd.notna(row[key]):
            return row[key]
    return None


def build_cuboid_corners_world(row: pd.Series) -> np.ndarray:
    position_x = extract_value(row, ("position.x", "position_x", "x", "pos_x"))
    position_y = extract_value(row, ("position.y", "position_y", "y", "pos_y"))
    position_z = extract_value(row, ("position.z", "position_z", "z", "pos_z"))
    dimension_x = extract_value(row, ("dimensions.x", "dimension.x", "length", "size.x"))
    dimension_y = extract_value(row, ("dimensions.y", "dimension.y", "width", "size.y"))
    dimension_z = extract_value(row, ("dimensions.z", "dimension.z", "height", "size.z"))
    yaw_value = extract_value(row, ("yaw", "rotation_yaw", "heading", "rotation_z"))

    required = [position_x, position_y, position_z, dimension_x, dimension_y, dimension_z, yaw_value]
    if any(value is None for value in required):
        raise ConverterError("Cuboid row missing required fields for geometry projection")

    px = float(position_x)
    py = float(position_y)
    pz = float(position_z)
    dx = float(dimension_x)
    dy = float(dimension_y)
    dz = float(dimension_z)
    yaw = float(yaw_value)
    if dx <= 0.0 or dy <= 0.0 or dz <= 0.0:
        raise ConverterError("Cuboid dimensions must be positive")

    half_x = dx / 2.0
    half_y = dy / 2.0
    half_z = dz / 2.0

    local_corners = np.array(
        [
            [-half_x, -half_y, -half_z],
            [half_x, -half_y, -half_z],
            [half_x, half_y, -half_z],
            [-half_x, half_y, -half_z],
            [-half_x, -half_y, half_z],
            [half_x, -half_y, half_z],
            [half_x, half_y, half_z],
            [-half_x, half_y, half_z],
        ],
        dtype=np.float64,
    )

    cos_yaw = math.cos(yaw)
    sin_yaw = math.sin(yaw)
    rotation_world = np.array(
        [
            [cos_yaw, -sin_yaw, 0.0],
            [sin_yaw, cos_yaw, 0.0],
            [0.0, 0.0, 1.0],
        ],
        dtype=np.float64,
    )
    center = np.array([px, py, pz], dtype=np.float64)
    return (rotation_world @ local_corners.T).T + center


def infer_fps_from_timestamps(path: Path) -> int:
    if not path.exists():
        return 30
    raw = load_json(path)
    values: list[float] = []
    if isinstance(raw, list):
        for item in raw:
            if isinstance(item, (int, float)):
                values.append(float(item))
            elif isinstance(item, dict):
                value = item.get("timestamp")
                if isinstance(value, (int, float)):
                    values.append(float(value))
    elif isinstance(raw, dict):
        candidates = raw.get("timestamps")
        if isinstance(candidates, list):
            for item in candidates:
                if isinstance(item, (int, float)):
                    values.append(float(item))
                elif isinstance(item, dict):
                    value = item.get("timestamp")
                    if isinstance(value, (int, float)):
                        values.append(float(value))
    if len(values) < 2:
        return 30

    values = sorted(values)
    deltas = [values[index + 1] - values[index] for index in range(len(values) - 1)]
    deltas = [delta for delta in deltas if delta > 0.0]
    if not deltas:
        return 30
    median_delta = deltas[len(deltas) // 2]

    fps_candidates = []
    for scale in (1.0, 1e3, 1e6, 1e9):
        fps = scale / median_delta
        if 1.0 <= fps <= 240.0:
            rounded = max(1, int(round(fps)))
            fps_candidates.append((abs(fps - rounded), rounded))
    if not fps_candidates:
        return 30
    fps_candidates.sort(key=lambda item: item[0])
    return fps_candidates[0][1]


def infer_yuv_path(dataset_dir: Path) -> Path | None:
    candidates = sorted(dataset_dir.glob("*.yuv"))
    if not candidates:
        return None
    return candidates[0]


def validate_yuv_frame_count(yuv_path: Path, width: int, height: int, expected_frames: int) -> None:
    frame_bytes = (width * height * 3) // 2
    if frame_bytes <= 0:
        raise ConverterError(f"Invalid frame dimensions for YUV validation: {width}x{height}")
    size_bytes = yuv_path.stat().st_size
    if size_bytes % frame_bytes != 0:
        raise ConverterError(
            f"YUV file size is not divisible by frame size: {yuv_path} "
            f"(size={size_bytes}, frame_bytes={frame_bytes})"
        )
    frame_count = size_bytes // frame_bytes
    if frame_count != expected_frames:
        raise ConverterError(
            f"YUV frame count mismatch: {yuv_path} has {frame_count} frames, "
            f"but dataset has {expected_frames} images/cuboid files"
        )


def map_source_label(label: str) -> str | None:
    normalized = label.strip().lower()
    return SOURCE_TO_TARGET.get(normalized)


def project_cuboid_to_bbox(
    corners_world: np.ndarray,
    intrinsics: Intrinsics,
    rotation_camera_world: np.ndarray,
    translation_world_camera: np.ndarray,
    width: int,
    height: int,
    z_near: float,
) -> tuple[float, float, float, float] | None:
    corners_camera = (rotation_camera_world @ (corners_world - translation_world_camera).T).T
    z_values = corners_camera[:, 2]
    in_front_mask = z_values > z_near
    if not np.any(in_front_mask):
        return None

    points_to_project: list[np.ndarray] = [point for point in corners_camera if point[2] > z_near]
    for edge_start, edge_end in CUBOID_EDGES:
        start = corners_camera[edge_start]
        end = corners_camera[edge_end]
        start_front = start[2] > z_near
        end_front = end[2] > z_near
        if start_front == end_front:
            continue
        denominator = end[2] - start[2]
        if abs(denominator) < 1e-12:
            continue
        ratio = (z_near - start[2]) / denominator
        if ratio < 0.0 or ratio > 1.0:
            continue
        intersection = start + ratio * (end - start)
        if intersection[2] > z_near:
            points_to_project.append(intersection)

    if not points_to_project:
        return None

    projected_u: list[float] = []
    projected_v: list[float] = []
    for point in points_to_project:
        x_value, y_value, z_value = float(point[0]), float(point[1]), float(point[2])
        if z_value <= z_near:
            continue
        u_value = intrinsics.fx * x_value / z_value + intrinsics.cx
        v_value = intrinsics.fy * y_value / z_value + intrinsics.cy
        if not (math.isfinite(u_value) and math.isfinite(v_value)):
            continue
        projected_u.append(u_value)
        projected_v.append(v_value)

    if not projected_u:
        return None

    min_u = max(0.0, min(projected_u))
    min_v = max(0.0, min(projected_v))
    max_u = min(float(width), max(projected_u))
    max_v = min(float(height), max(projected_v))

    bbox_width = max_u - min_u
    bbox_height = max_v - min_v
    if bbox_width <= 0.0 or bbox_height <= 0.0:
        return None

    return min_u, min_v, bbox_width, bbox_height


def main() -> int:
    args = parse_args()
    dataset_dir = Path(args.dataset_dir).expanduser().resolve()
    if not dataset_dir.is_dir():
        raise ConverterError(f"--dataset-dir does not exist or is not a directory: {dataset_dir}")

    camera_dir = dataset_dir / "camera" / args.camera
    cuboids_dir = dataset_dir / "annotations" / "cuboids"
    intrinsics_path = camera_dir / "intrinsics.json"
    poses_path = camera_dir / "poses.json"
    timestamps_path = camera_dir / "timestamps.json"

    if not camera_dir.is_dir():
        raise ConverterError(f"Camera directory not found: {camera_dir}")
    if not cuboids_dir.is_dir():
        raise ConverterError(f"Cuboids directory not found: {cuboids_dir}")

    image_files = get_sorted_files_with_suffix(camera_dir, "*.jpg")
    cuboid_files = get_sorted_files_with_suffix(cuboids_dir, "*.pkl")
    if len(image_files) != len(cuboid_files):
        raise ConverterError(
            f"Frame count mismatch: {len(image_files)} images vs {len(cuboid_files)} cuboid files "
            f"({camera_dir} vs {cuboids_dir})"
        )

    width, height = read_jpeg_size(image_files[0])
    fps = infer_fps_from_timestamps(timestamps_path)

    explicit_yuv = Path(args.yuv).expanduser().resolve() if args.yuv else None
    inferred_yuv = infer_yuv_path(dataset_dir)
    yuv_path = explicit_yuv if explicit_yuv is not None else inferred_yuv
    if yuv_path is not None and yuv_path.exists():
        validate_yuv_frame_count(yuv_path, width, height, len(image_files))

    intrinsics = load_intrinsics(intrinsics_path)
    pose_entries = load_pose_entries(poses_path)
    if len(pose_entries) < len(image_files):
        raise ConverterError(
            f"Not enough pose entries in {poses_path}: {len(pose_entries)} for {len(image_files)} frames"
        )

    output_path = Path(args.output).expanduser().resolve() if args.output else (
        dataset_dir / "annotations" / f"pandaset_{width}x{height}_{fps}_coco5.json"
    )
    if output_path.exists() and not args.overwrite:
        raise ConverterError(f"Output already exists (use --overwrite): {output_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    images_json: list[dict[str, Any]] = []
    annotations_json: list[dict[str, Any]] = []
    annotation_id = 1
    dropped_unknown = 0
    dropped_invalid_geometry = 0
    dropped_tiny = 0
    class_counts: Counter[str] = Counter()

    for frame_index in range(len(image_files)):
        image_id = frame_index
        synthetic_file_name = f"pandaset_{width}x{height}_{fps}_{frame_index:03d}.jpg"
        images_json.append(
            {
                "id": image_id,
                "file_name": synthetic_file_name,
                "width": width,
                "height": height,
            }
        )

        pose = pose_entries.get(frame_index)
        if pose is None:
            raise ConverterError(f"Missing pose entry for frame {frame_index}")

        heading = pose["heading"]
        position = pose["position"]
        rotation_world_camera = rotation_matrix_from_quaternion(
            float(heading["w"]),
            float(heading["x"]),
            float(heading["y"]),
            float(heading["z"]),
        )
        rotation_camera_world = rotation_world_camera.T
        translation_world_camera = np.array(
            [float(position["x"]), float(position["y"]), float(position["z"])],
            dtype=np.float64,
        )

        cuboids_df = pd.read_pickle(cuboid_files[frame_index])
        if not isinstance(cuboids_df, pd.DataFrame):
            cuboids_df = pd.DataFrame(cuboids_df)

        for _, row in cuboids_df.iterrows():
            label_value = extract_value(row, ("label", "category", "name"))
            if label_value is None:
                dropped_unknown += 1
                continue

            target_label = map_source_label(str(label_value))
            if target_label is None:
                dropped_unknown += 1
                continue

            try:
                corners_world = build_cuboid_corners_world(row)
            except (ConverterError, TypeError, ValueError):
                dropped_invalid_geometry += 1
                continue

            bbox = project_cuboid_to_bbox(
                corners_world=corners_world,
                intrinsics=intrinsics,
                rotation_camera_world=rotation_camera_world,
                translation_world_camera=translation_world_camera,
                width=width,
                height=height,
                z_near=1e-3,
            )
            if bbox is None:
                dropped_invalid_geometry += 1
                continue

            bbox_x, bbox_y, bbox_width, bbox_height = bbox
            bbox_area = bbox_width * bbox_height
            if (
                bbox_width < args.min_bbox_width
                or bbox_height < args.min_bbox_height
                or bbox_area < args.min_bbox_area
            ):
                dropped_tiny += 1
                continue

            category_id = CATEGORY_ID_BY_NAME[target_label]
            annotations_json.append(
                {
                    "id": annotation_id,
                    "image_id": image_id,
                    "category_id": category_id,
                    "bbox": [
                        round(float(bbox_x), 6),
                        round(float(bbox_y), 6),
                        round(float(bbox_width), 6),
                        round(float(bbox_height), 6),
                    ],
                    "area": round(float(bbox_area), 6),
                    "iscrowd": 0,
                    "bbox_mode": 1,
                    "class": target_label,
                }
            )
            annotation_id += 1
            class_counts[target_label] += 1

    output_payload = {
        "images": images_json,
        "annotations": annotations_json,
        "categories": CATEGORIES,
    }
    with output_path.open("w", encoding="utf-8") as handle:
        json.dump(output_payload, handle, indent=2)
        handle.write("\n")

    diagnostics = {
        "dataset_dir": str(dataset_dir),
        "camera": args.camera,
        "output": str(output_path),
        "frames_processed": len(image_files),
        "accepted_annotations": len(annotations_json),
        "dropped_unknown_label_annotations": dropped_unknown,
        "dropped_invalid_geometry_annotations": dropped_invalid_geometry,
        "dropped_tiny_box_annotations": dropped_tiny,
        "per_class_counts": dict(sorted(class_counts.items())),
        "image_width": width,
        "image_height": height,
        "fps": fps,
        "validated_yuv": str(yuv_path) if yuv_path and yuv_path.exists() else "",
    }

    print("PandaSet conversion complete")
    print(f"  Frames processed: {diagnostics['frames_processed']}")
    print(f"  Accepted annotations: {diagnostics['accepted_annotations']}")
    print(f"  Dropped unknown labels: {diagnostics['dropped_unknown_label_annotations']}")
    print(f"  Dropped invalid geometry: {diagnostics['dropped_invalid_geometry_annotations']}")
    print(f"  Dropped tiny boxes: {diagnostics['dropped_tiny_box_annotations']}")
    print(f"  Per-class counts: {diagnostics['per_class_counts']}")
    print(f"  Output: {output_path}")

    if args.report_json:
        report_path = Path(args.report_json).expanduser().resolve()
        report_path.parent.mkdir(parents=True, exist_ok=True)
        with report_path.open("w", encoding="utf-8") as handle:
            json.dump(diagnostics, handle, indent=2, sort_keys=True)
            handle.write("\n")
        print(f"  Report JSON: {report_path}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ConverterError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
