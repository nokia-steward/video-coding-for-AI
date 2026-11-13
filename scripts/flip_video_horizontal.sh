#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -o pipefail

# Script to flip a video file horizontally
# Uses ffmpeg for reliable video processing

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <input_video_file> [output_file]"
    echo ""
    echo "Flips the input video horizontally (mirror effect)."
    echo "If output_file is not specified, creates a new file with '_flipped' suffix."
    echo ""
    echo "Example:"
    echo "  $0 video.mp4"
    echo "  $0 video.mp4 output_flipped.mp4"
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
    output_file="${input_dir}/${input_name}_flipped.${input_ext}"
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

echo "Flipping video horizontally: $input_file"
echo "Output file: $output_file"
echo ""

# Use ffmpeg to flip the video horizontally
# -i: input file
# -vf hflip: horizontal flip filter
# -c:v libx264: encode video with H.264 codec
# -preset medium: encoding speed/quality balance
# -crf 23: constant rate factor for quality (lower is better, 23 is default)
# -c:a copy: copy audio stream if present (or skip if no audio)
# -y: overwrite output file if it exists (though we check above)
ffmpeg -i "${input_file}" \
    -vf hflip \
    -c:v libx264 \
    -preset medium \
    -crf 23 \
    -c:a copy \
    -y \
    "${output_file}" 2>&1

if [[ $? -eq 0 ]]; then
    echo ""
    echo "Successfully flipped video horizontally."
    echo "Output saved to: $output_file"
else
    echo ""
    echo "Error: Failed to flip video." >&2
    # Clean up partial output file if it exists
    if [[ -f "$output_file" ]]; then
        rm -f "$output_file"
        echo "Removed incomplete output file." >&2
    fi
    exit 1
fi
