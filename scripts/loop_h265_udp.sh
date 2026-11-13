#!/usr/bin/env bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -euo pipefail
exec python3 - "$@" <<'PY'
import argparse
import os
import re
import subprocess
import sys

# Configuration
INPUT_FILE = "videos/simulation.mkv"
DEST_IP = "127.0.0.1"  # Change to target IP (e.g., 192.168.1.X)
DEST_PORT = 12300
PAYLOAD_TYPE = 107
YUV_PIXEL_FORMAT = "yuv420p"
SHORT_GOP_KEYINT_FRAMES = 30


def parse_yuv_metadata_from_filename(input_file: str) -> tuple[int, int, float]:
    """Extract width, height, and fps from a raw YUV filename.

    Expected filename pattern contains '<width>x<height>_<fps>', for example:
    'Traffic_2560x1600_30.yuv'
    """
    filename = os.path.basename(input_file)
    match = re.search(r"(?P<width>\d+)x(?P<height>\d+)[_-](?P<fps>\d+(?:\.\d+)?)", filename)
    if not match:
        raise ValueError(
            "Could not parse resolution/fps from YUV filename. "
            "Expected pattern like '<name>_2560x1600_30.yuv'."
        )

    width = int(match.group("width"))
    height = int(match.group("height"))
    fps = float(match.group("fps"))
    return width, height, fps


def parse_host_port(host_spec: str, default_port: int) -> tuple[str, int]:
    """Parse host specification which can be either 'IP' or 'IP:PORT'."""
    if ":" in host_spec:
        parts = host_spec.rsplit(":", 1)
        if len(parts) == 2:
            host = parts[0]
            try:
                port = int(parts[1])
                return host, port
            except ValueError:
                raise ValueError(f"Invalid port number in '{host_spec}': '{parts[1]}' is not a valid integer")
    return host_spec, default_port


def stream_high_fidelity(
    dest_hosts: list[str],
    default_port: int = DEST_PORT,
    fps: float | None = None,
    input_file: str = INPUT_FILE,
):
    """Stream a looped video file to one or more hosts via RTP.
    
    Args:
        dest_hosts: List of host specifications. Each can be either 'IP' or 'IP:PORT'.
                    If port is not specified, default_port is used.
        default_port: Default port to use when not specified in host_spec.
        fps: Output frame rate in frames per second. If None, uses input file's native frame rate.
        input_file: Input video path. Supports encoded files and raw .yuv files.
                    Raw .yuv filenames must contain '<width>x<height>_<fps>'.
    Notes:
        Uses libx265 with a short GOP to emit more frequent random-access points.
        The encoder is configured to:
            - emit an IRAP roughly every SHORT_GOP_KEYINT_FRAMES frames,
            - repeat VPS/SPS/PPS on keyframes,
            - include AUD NAL units,
            - produce Annex-B bitstream format.
    """
    # 1. ?pkt_size=1316: Aligns with standard MPEG-TS packet chunks (188 bytes * 7)
    #    This prevents IP fragmentation which causes "glitches" in video.
    if not dest_hosts:
        raise ValueError("dest_hosts must contain at least one host")

    parsed_destinations = [parse_host_port(host_spec, default_port) for host_spec in dest_hosts]
    
    tee_outputs = [
        f"[select=v:f=rtp:ssrc=12345678:payload_type={PAYLOAD_TYPE}]"
        f"rtp://{host}:{port}?pkt_size=1316"
        for host, port in parsed_destinations
    ]
    tee_output_arg = "|".join(tee_outputs)

    is_raw_yuv = input_file.lower().endswith(".yuv")

    command = [
        "ffmpeg",
        "-re",  # Read at native frame rate (simulate live camera)
        "-stream_loop",
        "-1",  # Loop forever
    ]

    if is_raw_yuv:
        width, height, inferred_fps = parse_yuv_metadata_from_filename(input_file)
        command.extend([
            "-f",
            "rawvideo",
            "-pix_fmt",
            YUV_PIXEL_FORMAT,
            "-video_size",
            f"{width}x{height}",
            "-framerate",
            str(inferred_fps),
        ])

    command.extend([
        "-i",
        input_file,
        "-map",
        "0:v:0",
    ])
    
    # Add output frame rate if specified
    if fps is not None:
        command.extend(["-r", str(fps)])
    
    x265_params = (
        f"keyint={SHORT_GOP_KEYINT_FRAMES}:"
        f"min-keyint={SHORT_GOP_KEYINT_FRAMES}:"
        "scenecut=0:"
        "repeat-headers=1:"
        "aud=1:"
        "annexb=1"
    )

    # Always transcode with explicit x265 params so stream cadence/signaling
    # stays predictable regardless of the input file's original encoding.
    command.extend([
        "-c:v",
        "libx265",
        "-preset",
        "ultrafast",
        "-tune",
        "zerolatency",
        "-x265-params",
        x265_params,
    ])

    command.extend([
        "-an",  # No audio (unless you want it, then remove this)
        "-f",
        "tee",  # Duplicate the output to multiple RTP destinations
        tee_output_arg,
    ])

    destinations_label = ", ".join(f"{host}:{port}" for host, port in parsed_destinations)
    fps_info = f" @ {fps} fps" if fps is not None else ""
    print(f"Streaming high-fidelity replica to {destinations_label}{fps_info}...")
    print(
        f"Mode: RTP Type {PAYLOAD_TYPE} | Encode: libx265 "
        f"(short GOP={SHORT_GOP_KEYINT_FRAMES} frames, repeat headers, AUD, Annex-B) | mux=tee"
    )
    if is_raw_yuv:
        print(f"Input: raw YUV ({YUV_PIXEL_FORMAT})")
    print("Press Ctrl+C to stop.")

    try:
        process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # Keep process alive and print errors if it crashes
        while True:
            output = process.stderr.readline()
            if process.poll() is not None:
                print("\n[ERROR] FFmpeg crashed:")
                print(process.stderr.read().decode("utf-8"))
                break
            # Optional: print(output.decode('utf-8'), end='')

    except KeyboardInterrupt:
        print("\nStopping stream...")
        process.kill()
        sys.exit(0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Loop a video file over RTP to one or more target hosts."
    )
    parser.add_argument(
        "--host",
        "-H",
        action="append",
        dest="hosts",
        help=(
            f"Destination host/IP for RTP stream. "
            f"Can be specified as 'IP' or 'IP:PORT' (e.g., '192.168.1.1:12300'). "
            f"If port is not specified, default port {DEST_PORT} is used. "
            f"Repeat flag to send to multiple hosts (default: {DEST_IP}:{DEST_PORT})."
        ),
    )
    parser.add_argument(
        "--port",
        "-p",
        type=int,
        default=DEST_PORT,
        help=f"Default port to use when not specified in host argument (default: {DEST_PORT}).",
    )
    parser.add_argument(
        "--fps",
        "-f",
        type=float,
        default=None,
        help="Output frame rate in frames per second. If not specified, uses input file's native frame rate.",
    )
    parser.add_argument(
        "--input",
        "-i",
        default=INPUT_FILE,
        help=(
            f"Input video file path to stream (default: {INPUT_FILE}). "
            f"For raw .yuv files, filename must include '<width>x<height>_<fps>' "
            f"(example: Traffic_2560x1600_30.yuv), assumed pixel format: {YUV_PIXEL_FORMAT}."
        ),
    )
    args = parser.parse_args()

    stream_high_fidelity(
        dest_hosts=args.hosts or [f"{DEST_IP}:{args.port}"],
        default_port=args.port,
        fps=args.fps,
        input_file=args.input,
    )
PY
