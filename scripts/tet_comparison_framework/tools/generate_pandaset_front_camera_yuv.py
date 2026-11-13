#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

"""Generate PandaSet front-camera YUV420p from JPEG frames.

This script is dataset-root parameterized and deterministic:
- Input frames come from --dataset-dir/--camera-dir (default: camera/front_camera).
- Default output name is pandaset_<width>x<height>_<fps>.yuv under dataset root.
- FPS is inferred from timestamps.json when possible, else defaults to 30.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


class YuvGenerationError(RuntimeError):
    """Raised for actionable generation failures."""


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate PandaSet front-camera YUV420p from JPEG frames.")
    parser.add_argument("--dataset-dir", required=True, help="PandaSet dataset root directory")
    parser.add_argument("--camera-dir", default="camera/front_camera", help="Camera directory relative to dataset root")
    parser.add_argument("--output", default="", help="Output .yuv path (optional)")
    parser.add_argument("--fps", type=int, default=0, help="Optional FPS override")
    return parser.parse_args()


def parse_numeric_suffix(path: Path) -> int:
    match = re.search(r"(\d+)$", path.stem)
    if not match:
        raise YuvGenerationError(f"Expected numeric frame stem, got: {path.name}")
    return int(match.group(1))


def infer_fps_from_timestamps(path: Path) -> int:
    if not path.exists():
        return 30
    with path.open("r", encoding="utf-8") as handle:
        raw = json.load(handle)

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


def run_command(command: list[str], *, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=False, text=True, capture_output=capture_output)


def ensure_tool_exists(name: str) -> None:
    if shutil.which(name) is None:
        raise YuvGenerationError(
            f"Required tool '{name}' was not found in PATH. "
            f"Install ffmpeg/ffprobe and retry."
        )


def probe_image_dimensions(path: Path) -> tuple[int, int]:
    command = [
        "ffprobe",
        "-v",
        "error",
        "-select_streams",
        "v:0",
        "-show_entries",
        "stream=width,height",
        "-of",
        "csv=s=x:p=0",
        str(path),
    ]
    result = run_command(command, capture_output=True)
    if result.returncode != 0:
        raise YuvGenerationError(f"ffprobe failed for {path}: {result.stderr.strip()}")
    text = result.stdout.strip()
    match = re.match(r"^(\d+)x(\d+)$", text)
    if not match:
        raise YuvGenerationError(f"Could not parse ffprobe dimensions for {path}: '{text}'")
    return int(match.group(1)), int(match.group(2))


def compute_file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    args = parse_args()
    dataset_dir = Path(args.dataset_dir).expanduser().resolve()
    if not dataset_dir.is_dir():
        raise YuvGenerationError(f"--dataset-dir does not exist or is not a directory: {dataset_dir}")

    ensure_tool_exists("ffmpeg")
    ensure_tool_exists("ffprobe")

    camera_dir = (dataset_dir / args.camera_dir).resolve()
    if not camera_dir.is_dir():
        raise YuvGenerationError(f"Camera directory does not exist: {camera_dir}")

    frame_files = sorted(camera_dir.glob("*.jpg"), key=parse_numeric_suffix)
    if not frame_files:
        raise YuvGenerationError(f"No JPEG frames found in {camera_dir}")

    numeric_indices = [parse_numeric_suffix(path) for path in frame_files]
    if numeric_indices != sorted(numeric_indices):
        raise YuvGenerationError("JPEG frames are not numerically ordered")

    start_index = numeric_indices[0]
    expected = list(range(start_index, start_index + len(numeric_indices)))
    if numeric_indices != expected:
        raise YuvGenerationError(
            "JPEG frame indices must be contiguous for deterministic sequence conversion. "
            f"Got first indices: {numeric_indices[:8]}"
        )

    width, height = probe_image_dimensions(frame_files[0])
    for index, frame_path in enumerate(frame_files):
        current_width, current_height = probe_image_dimensions(frame_path)
        if current_width != width or current_height != height:
            raise YuvGenerationError(
                f"Frame resolution mismatch at {frame_path} "
                f"(expected {width}x{height}, got {current_width}x{current_height})"
            )
        if index % 200 == 0 and index > 0:
            print(f"Validated {index}/{len(frame_files)} frame resolutions...")

    if args.fps > 0:
        fps = args.fps
    else:
        fps = infer_fps_from_timestamps(camera_dir / "timestamps.json")

    default_output = dataset_dir / f"pandaset_{width}x{height}_{fps}.yuv"
    output_path = Path(args.output).expanduser().resolve() if args.output else default_output.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    first_name = frame_files[0].name
    digit_match = re.match(r"^(.*?)(\d+)\.jpg$", first_name, flags=re.IGNORECASE)
    if digit_match is None:
        raise YuvGenerationError(f"Frame name does not match numeric JPEG pattern: {first_name}")
    prefix = digit_match.group(1)
    digits = len(digit_match.group(2))
    pattern = f"{prefix}%0{digits}d.jpg"

    command = [
        "ffmpeg",
        "-y",
        "-loglevel",
        "error",
        "-framerate",
        str(fps),
        "-start_number",
        str(start_index),
        "-i",
        str(camera_dir / pattern),
        "-frames:v",
        str(len(frame_files)),
        "-pix_fmt",
        "yuv420p",
        "-f",
        "rawvideo",
        str(output_path),
    ]

    print("Generating YUV420p from PandaSet JPEG sequence")
    print(f"  Dataset: {dataset_dir}")
    print(f"  Camera dir: {camera_dir}")
    print(f"  Frames: {len(frame_files)}")
    print(f"  Resolution: {width}x{height}")
    print(f"  FPS: {fps}")
    print(f"  Output: {output_path}")
    result = run_command(command, capture_output=True)
    if result.returncode != 0:
        raise YuvGenerationError(
            "ffmpeg failed while generating YUV.\n"
            f"stdout: {result.stdout.strip()}\n"
            f"stderr: {result.stderr.strip()}"
        )

    if not output_path.exists():
        raise YuvGenerationError(f"YUV output was not created: {output_path}")

    expected_size = (len(frame_files) * width * height * 3) // 2
    actual_size = output_path.stat().st_size
    if actual_size != expected_size:
        raise YuvGenerationError(
            f"Generated YUV size mismatch: expected {expected_size} bytes, got {actual_size} bytes ({output_path})"
        )

    digest = compute_file_sha256(output_path)
    print("YUV generation complete")
    print(f"  Output size: {actual_size} bytes")
    print(f"  SHA256: {digest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (YuvGenerationError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
