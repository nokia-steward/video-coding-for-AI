#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

"""Find ROI overlap frames and border-vs-copy mismatches from JSONL dumps.

Supported input formats:
1) display border dump lines with "rectangles"
2) packing trace lines where stage == "final_output" and "packed_rectangles"
3) compare mode across display borders vs copy-rectangles stage
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Rectangle:
    x: int
    y: int
    w: int
    h: int

    @property
    def area(self) -> int:
        if self.w <= 0 or self.h <= 0:
            return 0
        return self.w * self.h


@dataclass(frozen=True)
class OverlapPair:
    first_index: int
    second_index: int
    overlap_area: int
    iou: float


@dataclass(frozen=True)
class FrameOverlap:
    frame_number: int
    rectangle_count: int
    pairs: tuple[OverlapPair, ...]

    @property
    def max_iou(self) -> float:
        if not self.pairs:
            return 0.0
        return max(pair.iou for pair in self.pairs)

    @property
    def total_overlap_area(self) -> int:
        return sum(pair.overlap_area for pair in self.pairs)


@dataclass
class FrameMismatch:
    frame_number: int
    border_count: int
    copy_count: int
    only_in_copy: Counter[Rectangle]
    only_in_border: Counter[Rectangle]


@dataclass(frozen=True)
class CopyRectanglePair:
    source: Rectangle
    destination: Rectangle


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Scan ROI JSONL dumps and report frames with overlaps."
    )
    parser.add_argument(
        "--mode",
        choices=("auto", "display", "packing", "compare"),
        default="auto",
        help=(
            "Input mode: display (rectangles), "
            "packing (final_output packed_rectangles), "
            "auto (overlap modes), or compare (border vs copy rectangles)."
        ),
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("encoder_cli/tracker_dump/display_border_rectangles.jsonl"),
        help=(
            "Path to JSONL dump file for overlap modes. Default: "
            "encoder_cli/tracker_dump/display_border_rectangles.jsonl"
        ),
    )
    parser.add_argument(
        "--display-input",
        type=Path,
        default=Path("encoder_cli/tracker_dump/display_border_rectangles.jsonl"),
        help=(
            "Display border JSONL for compare mode. Default: "
            "encoder_cli/tracker_dump/display_border_rectangles.jsonl"
        ),
    )
    parser.add_argument(
        "--copy-input",
        type=Path,
        default=Path("encoder_cli/tracker_dump/roi_packing_trace.jsonl"),
        help=(
            "Packing trace JSONL for compare mode. Default: "
            "encoder_cli/tracker_dump/roi_packing_trace.jsonl"
        ),
    )
    parser.add_argument(
        "--ignore-timestamp",
        dest="ignore_timestamp",
        action="store_true",
        default=True,
        help="Ignore RTP timestamp ROI in copy rectangles for compare mode (default).",
    )
    parser.add_argument(
        "--include-timestamp",
        dest="ignore_timestamp",
        action="store_false",
        help="Include RTP timestamp ROI in compare mode.",
    )
    parser.add_argument(
        "--compare-raw-destination",
        action="store_true",
        default=False,
        help=(
            "Compare against raw destination_rectangles instead of effective "
            "post-clamp copied rectangles (compare mode only)."
        ),
    )
    parser.add_argument(
        "--min-overlap-area",
        type=int,
        default=1,
        help="Minimum pixel area to count as overlap (overlap modes only, default: 1).",
    )
    parser.add_argument(
        "--max-report",
        type=int,
        default=50,
        help="Max number of frames to print in detail (default: 50).",
    )
    parser.add_argument(
        "--write-frames",
        type=Path,
        default=None,
        help="Optional output text file with one overlapping frame number per line.",
    )
    parser.add_argument(
        "--write-ranges",
        type=Path,
        default=None,
        help="Optional output text file with contiguous overlap ranges: start,end,count.",
    )
    return parser.parse_args()


def intersect_area(a: Rectangle, b: Rectangle) -> int:
    left = max(a.x, b.x)
    top = max(a.y, b.y)
    right = min(a.x + a.w, b.x + b.w)
    bottom = min(a.y + a.h, b.y + b.h)
    overlap_w = right - left
    overlap_h = bottom - top
    if overlap_w <= 0 or overlap_h <= 0:
        return 0
    return overlap_w * overlap_h


def compute_iou(a: Rectangle, b: Rectangle, overlap_area: int) -> float:
    union = a.area + b.area - overlap_area
    if union <= 0:
        return 0.0
    return float(overlap_area) / float(union)


def parse_rectangle(raw: object) -> Rectangle | None:
    if not isinstance(raw, dict):
        return None
    keys = {"x", "y", "w", "h"}
    if not keys.issubset(raw.keys()):
        return None
    try:
        return Rectangle(
            x=int(raw["x"]),
            y=int(raw["y"]),
            w=int(raw["w"]),
            h=int(raw["h"]),
        )
    except (ValueError, TypeError):
        return None


def extract_line_payload(parsed: dict[str, object], mode: str) -> tuple[int, list[Rectangle]] | None:
    frame_raw = parsed.get("frame_number")
    if not isinstance(frame_raw, int):
        return None
    frame_number = int(frame_raw)

    if mode in ("display", "auto"):
        rectangles_raw = parsed.get("rectangles")
        if isinstance(rectangles_raw, list):
            rectangles = [rect for item in rectangles_raw if (rect := parse_rectangle(item)) is not None]
            return frame_number, rectangles

    if mode in ("packing", "auto"):
        if parsed.get("stage") == "final_output" and isinstance(parsed.get("packed_rectangles"), list):
            packed_raw = parsed["packed_rectangles"]
            rectangles = [rect for item in packed_raw if (rect := parse_rectangle(item)) is not None]
            return frame_number, rectangles

    return None


def find_overlaps(rectangles: list[Rectangle], min_overlap_area: int) -> tuple[OverlapPair, ...]:
    pairs: list[OverlapPair] = []
    for first in range(len(rectangles)):
        for second in range(first + 1, len(rectangles)):
            overlap_area = intersect_area(rectangles[first], rectangles[second])
            if overlap_area < min_overlap_area:
                continue
            iou = compute_iou(rectangles[first], rectangles[second], overlap_area)
            pairs.append(
                OverlapPair(
                    first_index=first,
                    second_index=second,
                    overlap_area=overlap_area,
                    iou=iou,
                )
            )
    return tuple(pairs)


def contiguous_ranges(frames: Iterable[int]) -> list[tuple[int, int, int]]:
    sorted_frames = sorted(set(frames))
    if not sorted_frames:
        return []
    ranges: list[tuple[int, int, int]] = []
    start = sorted_frames[0]
    prev = sorted_frames[0]
    for frame in sorted_frames[1:]:
        if frame == prev + 1:
            prev = frame
            continue
        ranges.append((start, prev, prev - start + 1))
        start = frame
        prev = frame
    ranges.append((start, prev, prev - start + 1))
    return ranges


def build_counter(rectangles: list[Rectangle]) -> Counter[Rectangle]:
    return Counter(rectangles)


def timestamp_rectangle(frame_width: int, frame_height: int) -> Rectangle:
    x = 10
    y = max(0, frame_height - 12 - 10)
    w = max(0, min(107, frame_width - x))
    h = max(0, min(12, frame_height - y))
    return Rectangle(x=x, y=y, w=w, h=h)


def parse_display_file_for_compare(
    path: Path,
) -> tuple[dict[int, Counter[Rectangle]], dict[int, tuple[int, int]], int, int]:
    frames: dict[int, Counter[Rectangle]] = {}
    dimensions: dict[int, tuple[int, int]] = {}
    parsed_lines = 0
    skipped_lines = 0
    with path.open("r", encoding="utf-8") as file:
        for line in file:
            line = line.strip()
            if not line:
                continue
            parsed_lines += 1
            try:
                payload = json.loads(line)
            except json.JSONDecodeError:
                skipped_lines += 1
                continue
            if not isinstance(payload, dict):
                skipped_lines += 1
                continue
            frame_number = payload.get("frame_number")
            rectangles_raw = payload.get("rectangles")
            frame_width = payload.get("frame_width")
            frame_height = payload.get("frame_height")
            if (
                not isinstance(frame_number, int)
                or not isinstance(rectangles_raw, list)
                or not isinstance(frame_width, int)
                or not isinstance(frame_height, int)
            ):
                skipped_lines += 1
                continue
            rectangles = [rect for item in rectangles_raw if (rect := parse_rectangle(item)) is not None]
            frames[int(frame_number)] = build_counter(rectangles)
            dimensions[int(frame_number)] = (int(frame_width), int(frame_height))
    return frames, dimensions, parsed_lines, skipped_lines


def parse_copy_file_for_compare(
    path: Path,
) -> tuple[dict[int, Counter[Rectangle]], dict[int, list[CopyRectanglePair]], int, int]:
    raw_destinations_by_frame: dict[int, Counter[Rectangle]] = {}
    pairs_by_frame: dict[int, list[CopyRectanglePair]] = {}
    parsed_lines = 0
    skipped_lines = 0
    with path.open("r", encoding="utf-8") as file:
        for line in file:
            line = line.strip()
            if not line:
                continue
            parsed_lines += 1
            try:
                payload = json.loads(line)
            except json.JSONDecodeError:
                skipped_lines += 1
                continue
            if not isinstance(payload, dict):
                skipped_lines += 1
                continue
            if payload.get("stage") != "copy_rectangles":
                continue
            frame_number = payload.get("frame_number")
            destination_raw = payload.get("destination_rectangles")
            pairs_raw = payload.get("pairs")
            if (
                not isinstance(frame_number, int)
                or not isinstance(destination_raw, list)
                or not isinstance(pairs_raw, list)
            ):
                skipped_lines += 1
                continue
            frame = int(frame_number)
            destinations = [rect for item in destination_raw if (rect := parse_rectangle(item)) is not None]
            raw_destinations_by_frame[frame] = build_counter(destinations)

            parsed_pairs: list[CopyRectanglePair] = []
            for pair_raw in pairs_raw:
                if not isinstance(pair_raw, dict):
                    continue
                source = parse_rectangle(pair_raw.get("source"))
                destination = parse_rectangle(pair_raw.get("destination"))
                if source is None or destination is None:
                    continue
                parsed_pairs.append(CopyRectanglePair(source=source, destination=destination))
            pairs_by_frame[frame] = parsed_pairs
    return raw_destinations_by_frame, pairs_by_frame, parsed_lines, skipped_lines


def effective_destination_rectangle(
    source: Rectangle, destination: Rectangle, frame_width: int, frame_height: int
) -> Rectangle | None:
    if frame_width <= 0 or frame_height <= 0:
        return None

    src_x = max(0, source.x)
    src_y = max(0, source.y)
    src_w = min(source.w, frame_width - src_x)
    src_h = min(source.h, frame_height - src_y)

    dst_x = max(0, destination.x)
    dst_y = max(0, destination.y)
    dst_w = min(destination.w, frame_width - dst_x)
    dst_h = min(destination.h, frame_height - dst_y)

    copy_w = min(src_w, dst_w)
    copy_h = min(src_h, dst_h)
    if copy_w <= 0 or copy_h <= 0:
        return None
    return Rectangle(x=dst_x, y=dst_y, w=copy_w, h=copy_h)


def build_effective_copy_counter(
    pairs: list[CopyRectanglePair], frame_width: int, frame_height: int
) -> Counter[Rectangle]:
    effective_rectangles: list[Rectangle] = []
    for pair in pairs:
        effective = effective_destination_rectangle(pair.source, pair.destination, frame_width, frame_height)
        if effective is not None:
            effective_rectangles.append(effective)
    return build_counter(effective_rectangles)


def sorted_counter_items(counter: Counter[Rectangle]) -> list[tuple[Rectangle, int]]:
    return sorted(counter.items(), key=lambda item: (item[0].x, item[0].y, item[0].w, item[0].h, item[1]))


def run_compare_mode(args: argparse.Namespace) -> int:
    if not args.display_input.is_file():
        raise SystemExit(f"ERROR: Display input file not found: {args.display_input}")
    if not args.copy_input.is_file():
        raise SystemExit(f"ERROR: Copy input file not found: {args.copy_input}")
    if args.max_report < 1:
        raise SystemExit("ERROR: --max-report must be >= 1")

    (
        border_by_frame,
        dimensions_by_frame,
        display_parsed_lines,
        display_skipped_lines,
    ) = parse_display_file_for_compare(args.display_input)
    (
        raw_copy_destinations_by_frame,
        copy_pairs_by_frame,
        copy_parsed_lines,
        copy_skipped_lines,
    ) = parse_copy_file_for_compare(args.copy_input)

    border_frames = set(border_by_frame.keys())
    copy_frames = set(raw_copy_destinations_by_frame.keys())
    common_frames = sorted(border_frames & copy_frames)
    missing_in_border = sorted(copy_frames - border_frames)
    missing_in_copy = sorted(border_frames - copy_frames)

    mismatches: list[FrameMismatch] = []
    timestamp_removed_count = 0
    for frame_number in common_frames:
        border_counter = border_by_frame[frame_number]
        if frame_number not in dimensions_by_frame:
            continue
        frame_width, frame_height = dimensions_by_frame[frame_number]

        if args.compare_raw_destination:
            copy_counter = raw_copy_destinations_by_frame[frame_number].copy()
        else:
            copy_counter = build_effective_copy_counter(
                copy_pairs_by_frame.get(frame_number, []), frame_width, frame_height
            )

        if args.ignore_timestamp:
            timestamp_rect = timestamp_rectangle(frame_width, frame_height)
            if timestamp_rect.area > 0 and copy_counter[timestamp_rect] > 0:
                copy_counter[timestamp_rect] -= 1
                if copy_counter[timestamp_rect] <= 0:
                    del copy_counter[timestamp_rect]
                timestamp_removed_count += 1

        only_in_copy = copy_counter - border_counter
        only_in_border = border_counter - copy_counter
        if only_in_copy or only_in_border:
            mismatches.append(
                FrameMismatch(
                    frame_number=frame_number,
                    border_count=sum(border_counter.values()),
                    copy_count=sum(copy_counter.values()),
                    only_in_copy=only_in_copy,
                    only_in_border=only_in_border,
                )
            )

    mismatch_frames = [item.frame_number for item in mismatches]
    mismatch_ranges = contiguous_ranges(mismatch_frames)

    print(f"Mode: compare")
    print(f"Display input: {args.display_input}")
    print(f"Copy input: {args.copy_input}")
    print(f"Display parsed lines: {display_parsed_lines}")
    print(f"Display skipped lines: {display_skipped_lines}")
    print(f"Copy parsed lines: {copy_parsed_lines}")
    print(f"Copy skipped lines: {copy_skipped_lines}")
    if args.compare_raw_destination:
        print("Compare copy mode: raw_destination")
    else:
        print("Compare copy mode: effective_copy")
    print(f"Common frames considered: {len(common_frames)}")
    print(f"Frames missing in border dump: {len(missing_in_border)}")
    print(f"Frames missing in copy dump: {len(missing_in_copy)}")
    print(f"Ignore timestamp: {args.ignore_timestamp}")
    if args.ignore_timestamp:
        print(f"Frames with timestamp rectangle removed from copy set: {timestamp_removed_count}")
    print(f"Mismatch frames: {len(mismatches)}")
    if mismatches:
        print(f"First mismatch frame: {mismatches[0].frame_number}")
        print(f"Last mismatch frame: {mismatches[-1].frame_number}")
    else:
        print("No border-vs-copy mismatches found.")

    print(f"Contiguous mismatch ranges: {len(mismatch_ranges)}")
    for start, end, count in mismatch_ranges:
        print(f"  - {start}..{end} ({count} frames)")

    if mismatches:
        print("\nDetailed mismatch frames:")
    for mismatch in mismatches[: args.max_report]:
        copy_only_total = sum(mismatch.only_in_copy.values())
        border_only_total = sum(mismatch.only_in_border.values())
        print(
            f"  frame={mismatch.frame_number} "
            f"border_count={mismatch.border_count} "
            f"copy_count={mismatch.copy_count} "
            f"copy_only={copy_only_total} "
            f"border_only={border_only_total}"
        )
        for rect, count in sorted_counter_items(mismatch.only_in_copy):
            print(f"    copy_only count={count} rect=({rect.x},{rect.y},{rect.w},{rect.h})")
        for rect, count in sorted_counter_items(mismatch.only_in_border):
            print(f"    border_only count={count} rect=({rect.x},{rect.y},{rect.w},{rect.h})")

    if args.write_frames is not None:
        args.write_frames.parent.mkdir(parents=True, exist_ok=True)
        with args.write_frames.open("w", encoding="utf-8") as file:
            for frame in mismatch_frames:
                file.write(f"{frame}\n")
        print(f"\nWrote mismatch frame list: {args.write_frames}")

    if args.write_ranges is not None:
        args.write_ranges.parent.mkdir(parents=True, exist_ok=True)
        with args.write_ranges.open("w", encoding="utf-8") as file:
            for start, end, count in mismatch_ranges:
                file.write(f"{start},{end},{count}\n")
        print(f"Wrote mismatch ranges: {args.write_ranges}")

    return 0


def main() -> int:
    args = parse_args()

    if args.mode == "compare":
        return run_compare_mode(args)

    if not args.input.is_file():
        raise SystemExit(f"ERROR: Input file not found: {args.input}")
    if args.min_overlap_area < 1:
        raise SystemExit("ERROR: --min-overlap-area must be >= 1")
    if args.max_report < 1:
        raise SystemExit("ERROR: --max-report must be >= 1")

    overlap_frames: list[FrameOverlap] = []
    parsed_lines = 0
    considered_frames = 0
    skipped_lines = 0

    with args.input.open("r", encoding="utf-8") as file:
        for line_number, line in enumerate(file, start=1):
            line = line.strip()
            if not line:
                continue
            parsed_lines += 1
            try:
                parsed = json.loads(line)
            except json.JSONDecodeError:
                skipped_lines += 1
                continue
            if not isinstance(parsed, dict):
                skipped_lines += 1
                continue

            extracted = extract_line_payload(parsed, args.mode)
            if extracted is None:
                skipped_lines += 1
                continue
            frame_number, rectangles = extracted
            considered_frames += 1

            pairs = find_overlaps(rectangles, args.min_overlap_area)
            if not pairs:
                continue
            overlap_frames.append(
                FrameOverlap(
                    frame_number=frame_number,
                    rectangle_count=len(rectangles),
                    pairs=pairs,
                )
            )

            if len(overlap_frames) == 1 and line_number > 1:
                # Keep lint tools happy about line_number use while preserving useful context in future edits.
                _ = line_number

    overlap_frames.sort(key=lambda item: item.frame_number)
    overlap_only_numbers = [item.frame_number for item in overlap_frames]
    ranges = contiguous_ranges(overlap_only_numbers)

    print(f"Input file: {args.input}")
    print(f"Mode: {args.mode}")
    print(f"Parsed lines: {parsed_lines}")
    print(f"Frames considered: {considered_frames}")
    print(f"Skipped lines: {skipped_lines}")
    print(f"Overlap frames: {len(overlap_frames)}")

    if overlap_frames:
        print(f"First overlap frame: {overlap_frames[0].frame_number}")
        print(f"Last overlap frame: {overlap_frames[-1].frame_number}")
    else:
        print("No overlap frames found.")

    print(f"Contiguous overlap ranges: {len(ranges)}")
    for start, end, count in ranges:
        print(f"  - {start}..{end} ({count} frames)")

    if overlap_frames:
        print("\nDetailed overlap frames:")
    for frame in overlap_frames[: args.max_report]:
        print(
            f"  frame={frame.frame_number} "
            f"rectangles={frame.rectangle_count} "
            f"pairs={len(frame.pairs)} "
            f"total_overlap_area={frame.total_overlap_area} "
            f"max_iou={frame.max_iou:.6f}"
        )
        for pair in frame.pairs:
            print(
                f"    pair=({pair.first_index},{pair.second_index}) "
                f"overlap_area={pair.overlap_area} "
                f"iou={pair.iou:.6f}"
            )

    if args.write_frames is not None:
        args.write_frames.parent.mkdir(parents=True, exist_ok=True)
        with args.write_frames.open("w", encoding="utf-8") as file:
            for frame in overlap_only_numbers:
                file.write(f"{frame}\n")
        print(f"\nWrote overlap frame list: {args.write_frames}")

    if args.write_ranges is not None:
        args.write_ranges.parent.mkdir(parents=True, exist_ok=True)
        with args.write_ranges.open("w", encoding="utf-8") as file:
            for start, end, count in ranges:
                file.write(f"{start},{end},{count}\n")
        print(f"Wrote overlap ranges: {args.write_ranges}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
