#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

"""
Convert PandaSet semantic-segmentation .pkl data to sparse JSON points.

The output JSON stores per-frame image-space points with VCM class IDs so
rtp_server test mode can compute semantic-segmentation mIoU.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
from pathlib import Path
from typing import Dict, List


# VCM category names and evaluation classes, aligned with JVET PandaSet scripts.
VCM_CATEGORY: Dict[int, str] = {
    0: "unlabeled",
    1: "vegetation",
    2: "ground",
    3: "road",
    4: "sidewalk",
    5: "car",
    6: "truck",
    7: "motorcycle",
    8: "bicycle",
    9: "other_vehicle",
    10: "bus",
    11: "train",
    12: "pedestrian",
    13: "animals_bird",
    14: "animals_other",
    15: "signs",
    16: "road_barriers",
    17: "building",
    18: "other_static_object",
}

VCM_EVAL_CATEGORY_IDS: List[int] = [1, 3, 4, 5, 6, 7, 8, 10, 12, 17]

PANDASET_TO_VCM_CATEGORY: Dict[int, int] = {
    0: 0,
    1: 0,
    2: 5,
    3: 0,
    4: 0,
    5: 1,
    6: 2,
    7: 3,
    8: 3,
    9: 3,
    10: 3,
    11: 4,
    12: 4,
    13: 5,
    14: 6,
    15: 6,
    16: 6,
    17: 5,
    18: 7,
    19: 9,
    20: 9,
    21: 9,
    22: 9,
    23: 10,
    24: 7,
    25: 7,
    26: 8,
    27: 11,
    28: 0,
    29: 11,
    30: 12,
    31: 12,
    32: 13,
    33: 14,
    34: 16,
    35: 16,
    36: 15,
    37: 16,
    38: 15,
    39: 16,
    40: 18,
    41: 17,
    42: 18,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert PandaSet semseg .pkl to sparse JSON points for rtp_server semseg test mode."
    )
    parser.add_argument(
        "--dataset-dir",
        required=True,
        help="Path to a PandaSet sequence directory (e.g. .../PandaSet/057).",
    )
    parser.add_argument(
        "--output",
        required=True,
        help="Output JSON file path.",
    )
    parser.add_argument(
        "--camera",
        default="front_camera",
        help="Camera name used for projection (default: front_camera).",
    )
    parser.add_argument(
        "--image-width",
        type=int,
        default=1920,
        help="Output image width for bounds checks (default: 1920).",
    )
    parser.add_argument(
        "--image-height",
        type=int,
        default=1080,
        help="Output image height for bounds checks (default: 1080).",
    )
    return parser.parse_args()


def validate_sequence_dir(dataset_dir: Path) -> None:
    required_dirs = [
        dataset_dir / "camera" / "front_camera",
        dataset_dir / "annotations" / "semseg",
        dataset_dir / "lidar",
    ]
    for required in required_dirs:
        if not required.exists():
            raise RuntimeError(f"Missing required PandaSet directory: {required}")


def resolve_dataset_root_and_sequence_id(dataset_dir: Path) -> tuple[Path, str]:
    sequence_id = dataset_dir.name
    dataset_root = dataset_dir.parent
    if not sequence_id:
        raise RuntimeError("Could not determine PandaSet sequence ID from --dataset-dir")
    if not dataset_root.exists():
        raise RuntimeError(f"Dataset root does not exist: {dataset_root}")
    return dataset_root, sequence_id


def patch_raw_pickle_data_structure(sequence) -> None:
    """Fallback for datasets storing raw .pkl instead of .pkl.gz expected by pandaset-devkit."""
    if sequence.lidar is not None and not sequence.lidar._data_structure:  # pylint: disable=protected-access
        lidar_raw = sorted(glob.glob(os.path.join(sequence.lidar._directory, "*.pkl")))  # pylint: disable=protected-access
        if lidar_raw:
            sequence.lidar._data_structure = lidar_raw  # pylint: disable=protected-access

    if sequence.semseg is not None and not sequence.semseg._data_structure:  # pylint: disable=protected-access
        semseg_raw = sorted(glob.glob(os.path.join(sequence.semseg._directory, "*.pkl")))  # pylint: disable=protected-access
        if semseg_raw:
            sequence.semseg._data_structure = semseg_raw  # pylint: disable=protected-access


def convert(args: argparse.Namespace) -> None:
    dataset_dir = Path(args.dataset_dir).expanduser().resolve()
    validate_sequence_dir(dataset_dir)
    dataset_root, sequence_id = resolve_dataset_root_and_sequence_id(dataset_dir)

    try:
        import numpy as np
        import pandaset
        from pandaset import geometry
    except Exception as exc:
        raise RuntimeError(
            "Failed to import required modules (pandaset, numpy). "
            "Ensure the PandaSet environment is installed."
        ) from exc

    dataset = pandaset.DataSet(str(dataset_root))
    sequence = dataset[sequence_id]
    patch_raw_pickle_data_structure(sequence)
    sequence.load()

    lidar = sequence.lidar
    if args.camera not in sequence.camera:
        raise RuntimeError(f"Camera '{args.camera}' is not available in sequence {sequence_id}")
    chosen_camera = sequence.camera[args.camera]

    eval_set = set(VCM_EVAL_CATEGORY_IDS)
    frames_output: List[dict] = []
    total_points = 0
    camera_frame_count = len(chosen_camera.timestamps)
    lidar_frame_count = len(lidar.data)
    semseg_frame_count = len(sequence.semseg.data)
    camera_pose_count = len(chosen_camera.poses)
    frame_count = min(camera_frame_count, lidar_frame_count, semseg_frame_count, camera_pose_count)
    if frame_count <= 0:
        raise RuntimeError("No overlapping frames found across camera/lidar/semseg data.")
    if frame_count != camera_frame_count or frame_count != lidar_frame_count or frame_count != semseg_frame_count:
        print(
            "Warning: frame-count mismatch detected. "
            f"camera={camera_frame_count}, lidar={lidar_frame_count}, semseg={semseg_frame_count}, poses={camera_pose_count}. "
            f"Using {frame_count} overlapping frames."
        )

    for frame_index in range(0, frame_count):
        points3d_lidar_xyz = lidar.data[frame_index].to_numpy()[:, :3]
        if points3d_lidar_xyz.size == 0:
            frames_output.append({"frame_index": frame_index, "points": []})
            continue
        projected_points2d, _, inner_indices = geometry.projection(
            lidar_points=points3d_lidar_xyz,
            camera_data=chosen_camera[frame_index],
            camera_pose=chosen_camera.poses[frame_index],
            camera_intrinsics=chosen_camera.intrinsics,
            filter_outliers=True,
        )

        projected_points2d = projected_points2d.astype(int)
        semseg = sequence.semseg[frame_index].to_numpy().flatten()
        if semseg.size == 0:
            frames_output.append({"frame_index": frame_index, "points": []})
            continue

        inner_indices = np.asarray(inner_indices).astype(np.int64, copy=False)
        valid_index_mask = (inner_indices >= 0) & (inner_indices < semseg.size)
        if not np.any(valid_index_mask):
            frames_output.append({"frame_index": frame_index, "points": []})
            continue
        inner_indices = inner_indices[valid_index_mask]
        projected_points2d = projected_points2d[valid_index_mask]
        semseg_on_image = semseg[inner_indices]

        frame_points: List[dict] = []
        for (x, y), pandaset_class_id in zip(projected_points2d, semseg_on_image):
            if x < 0 or y < 0 or x >= args.image_width or y >= args.image_height:
                continue
            vcm_class_id = PANDASET_TO_VCM_CATEGORY.get(int(pandaset_class_id), 0)
            if vcm_class_id not in eval_set:
                continue
            frame_points.append(
                {
                    "x": int(x),
                    "y": int(y),
                    "class_id": int(vcm_class_id),
                }
            )

        total_points += len(frame_points)
        frames_output.append({"frame_index": frame_index, "points": frame_points})

    output_payload = {
        "groundtruth_type": "semseg_sparse_points_v1",
        "dataset": "PandaSet",
        "sequence_id": sequence_id,
        "camera": args.camera,
        "image_width": int(args.image_width),
        "image_height": int(args.image_height),
        "categories": [{"id": class_id, "name": VCM_CATEGORY[class_id]} for class_id in VCM_EVAL_CATEGORY_IDS],
        "eval_category_ids": VCM_EVAL_CATEGORY_IDS,
        "frames": frames_output,
    }

    output_path = Path(args.output).expanduser().resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", encoding="utf-8") as output_file:
        json.dump(output_payload, output_file, indent=2)

    print(f"Converted PandaSet semseg to sparse JSON: {output_path}")
    print(f"Sequence: {sequence_id}, frames: {len(frames_output)}, kept_eval_points: {total_points}")


def main() -> int:
    args = parse_args()
    try:
        convert(args)
    except Exception as exc:
        print(f"ERROR: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
