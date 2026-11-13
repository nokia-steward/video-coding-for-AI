#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -o pipefail

# Script to remove audio tracks from video files
# Supports MP4 with H.265 encoded video and other formats

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <input_video_file> [output_file]"
    echo ""
    echo "Removes all audio tracks from the input video file."
    echo "If output_file is not specified, creates a new file with '_no_audio' suffix."
    echo ""
    echo "Example:"
    echo "  $0 video.mp4"
    echo "  $0 video.mp4 output_no_audio.mp4"
    exit 1
fi

input_file="$1"
output_file="${2:-}"

# Check if input file exists
if [[ ! -f "$input_file" ]]; then
    echo "Error: Input file '$input_file' does not exist." >&2
    exit 1
fi

# Generate output filename if not provided
if [[ -z "$output_file" ]]; then
    input_dir=$(dirname "$input_file")
    input_basename=$(basename "$input_file")
    input_name="${input_basename%.*}"
    input_ext="${input_basename##*.}"
    output_file="${input_dir}/${input_name}_no_audio.${input_ext}"
fi

# Check if output file already exists
if [[ -f "$output_file" ]]; then
    echo "Error: Output file '$output_file' already exists." >&2
    echo "Please specify a different output file or remove the existing one." >&2
    exit 1
fi

# Check if ffmpeg is available
if ! command -v ffmpeg &> /dev/null; then
    echo "Error: ffmpeg is not installed or not in PATH." >&2
    echo "Please install ffmpeg to use this script." >&2
    exit 1
fi

echo "Removing audio from: $input_file"
echo "Output file: $output_file"
echo ""

# Use ffmpeg to copy video stream and skip audio streams
# -i: input file
# -c:v copy: copy video codec (no re-encoding, preserves quality)
# -an: disable audio (remove all audio tracks)
# -y: overwrite output file if it exists (though we check above)
ffmpeg -i "$input_file" -c:v copy -an -y "$output_file"

if [[ $? -eq 0 ]]; then
    echo ""
    echo "Successfully removed audio from video."
    echo "Output saved to: $output_file"
else
    echo ""
    echo "Error: Failed to remove audio from video." >&2
    exit 1
fi
