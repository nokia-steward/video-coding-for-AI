#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -o pipefail

# Script to play raw YUV video using ffplay
# Assumes YUV 4:2:0 planar (yuv420p) pixel format

if [[ $# -ne 1 && $# -ne 3 ]]; then
    echo "Usage: $0 <input_yuv_file> [<width>x<height> <framerate>]"
    echo ""
    echo "Plays a raw YUV file on screen using ffplay."
    echo "Pixel format is assumed to be yuv420p."
    echo ""
    echo "If only the input path is provided, the script will parse"
    echo "resolution and framerate from the filename in the format:"
    echo "  <name>_<width>x<height>_<framerate>.yuv"
    echo ""
    echo "Example:"
    echo "  $0 BQMall_832x480_60.yuv"
    echo "  $0 video.yuv 1920x1080 30"
    exit 1
fi

input_file="$1"
resolution="${2:-}"
framerate="${3:-}"

if [[ ! -f "$input_file" ]]; then
    echo "Error: Input file '$input_file' does not exist." >&2
    exit 1
fi

if ! command -v ffplay &> /dev/null; then
    echo "Error: ffplay is not installed or not in PATH." >&2
    echo "Please install ffmpeg to use this script." >&2
    exit 1
fi

if [[ -z "$resolution" && -z "$framerate" ]]; then
    file_name=$(basename "$input_file")
    if [[ "$file_name" =~ _([0-9]+)x([0-9]+)_([0-9]+([.][0-9]+)?)\.yuv$ ]]; then
        resolution="${BASH_REMATCH[1]}x${BASH_REMATCH[2]}"
        framerate="${BASH_REMATCH[3]}"
    else
        echo "Error: Could not parse resolution and framerate from filename." >&2
        echo "Expected format: <name>_<width>x<height>_<framerate>.yuv" >&2
        exit 1
    fi
fi

if [[ ! "$resolution" =~ ^[0-9]+x[0-9]+$ ]]; then
    echo "Error: Resolution must be in the format <width>x<height>." >&2
    exit 1
fi

width="${resolution%x*}"
height="${resolution#*x}"

if [[ "$width" -le 0 || "$height" -le 0 ]]; then
    echo "Error: Resolution values must be positive integers." >&2
    exit 1
fi

if [[ ! "$framerate" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "Error: Framerate must be a positive number." >&2
    exit 1
fi

echo "Playing YUV file: $input_file"
echo "Resolution: ${width}x${height}"
echo "Framerate: $framerate"
echo "Pixel format: yuv420p"
echo ""

ffplay \
    -f rawvideo \
    -pixel_format yuv420p \
    -video_size "${width}x${height}" \
    -framerate "$framerate" \
    -i "$input_file"
