#!/usr/bin/env python3
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

import argparse
import pickle
import struct
import sys
from pathlib import Path

import numpy as np


SUPPORTED_QPS = [22, 27, 32, 37, 42, 47]
NORMALIZED = 0
RAW_DIFF = 1
CENTERED = 2


def load_pickle(path: Path):
    with path.open("rb") as handle:
        try:
            return pickle.load(handle)
        except pickle.UnpicklingError:
            try:
                import torch  # type: ignore
            except Exception as exc:
                raise RuntimeError(
                    "Pickle requires torch persistent loader; install torch to load this file."
                ) from exc
            handle.seek(0)
            return torch.load(handle, map_location="cpu")


def to_numpy(value):
    try:
        import torch  # type: ignore

        if isinstance(value, torch.Tensor):
            return value.detach().cpu().numpy()
    except Exception:
        pass
    if isinstance(value, np.ndarray):
        return value
    return np.array(value)


def normalize_tensor(tensor: np.ndarray) -> np.ndarray:
    if tensor.ndim == 2:
        return tensor[np.newaxis, :, :]
    if tensor.ndim == 3 and tensor.shape[0] in (1, 3):
        return tensor
    if tensor.ndim == 3 and tensor.shape[2] in (1, 3):
        return np.transpose(tensor, (2, 0, 1))
    raise ValueError(f"Unsupported tensor shape: {tensor.shape}")


def detect_normalization(data_min: float, data_max: float) -> int:
    if 0.0 <= data_min and data_max <= 1.0:
        return NORMALIZED
    if -1.0 <= data_min and data_max <= 1.0:
        return CENTERED
    return RAW_DIFF


def extract_tiles(raw_data):
    if isinstance(raw_data, dict):
        if "add_checkerboard" in raw_data:
            data = to_numpy(raw_data["add_checkerboard"]).astype(np.float32)
            if data.ndim == 4 and data.shape[0] == len(SUPPORTED_QPS):
                return [data[index] for index in range(data.shape[0])]
            if data.ndim == 3 and data.shape[0] == len(SUPPORTED_QPS):
                return [data[index] for index in range(data.shape[0])]
            raise ValueError(f"Unsupported checkerboard tensor shape: {data.shape}")

        missing = [qp for qp in SUPPORTED_QPS if qp not in raw_data]
        if missing:
            raise ValueError(f"Missing QP entries in pickle: {missing}")
        return [raw_data[qp] for qp in SUPPORTED_QPS]
    if isinstance(raw_data, (list, tuple)):
        if len(raw_data) != len(SUPPORTED_QPS):
            raise ValueError("Pickle list must contain 6 entries (one per QP)")
        return list(raw_data)
    raise ValueError("Unsupported pickle format; expected dict or list of tiles")


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert PyTorch pickle tiles to .tet format")
    parser.add_argument("input_pickle", type=Path, help="Input pickle file (.pkl)")
    parser.add_argument("output_file", type=Path, help="Output .tet file")
    args = parser.parse_args()

    tiles_raw = extract_tiles(load_pickle(args.input_pickle))
    tiles = [normalize_tensor(to_numpy(tile).astype(np.float32)) for tile in tiles_raw]

    num_qps = len(tiles)
    num_channels, height, width = tiles[0].shape
    if num_channels not in (1, 3):
        raise ValueError(f"Unsupported channel count: {num_channels}")

    for index, tile in enumerate(tiles):
        if tile.shape != (num_channels, height, width):
            raise ValueError(f"Tile at index {index} has inconsistent shape: {tile.shape}")

    all_values = np.concatenate([tile.reshape(-1) for tile in tiles])
    data_min = float(np.min(all_values))
    data_max = float(np.max(all_values))
    normalization_type = detect_normalization(data_min, data_max)
    scale_factor = 1.0

    header = struct.pack(
        "<4s6I3f6I",
        b"TET\0",
        1,
        num_qps,
        num_channels,
        width,
        height,
        normalization_type,
        scale_factor,
        data_min,
        data_max,
        *SUPPORTED_QPS,
    )

    args.output_file.parent.mkdir(parents=True, exist_ok=True)
    with args.output_file.open("wb") as output:
        output.write(header)
        for tile in tiles:
            output.write(tile.astype(np.float32).tobytes(order="C"))

    print(f"Wrote {args.output_file} ({num_channels}ch {width}x{height}, min={data_min:.6f}, max={data_max:.6f})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
