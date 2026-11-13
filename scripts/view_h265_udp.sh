#!/usr/bin/env bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -euo pipefail
exec python3 - "$@" <<'PY'
import argparse
import subprocess
import signal
import sys
import os
import time

# Configuration
DEFAULT_UDP_PORT = 12300
PAYLOAD_TYPE = 107
SDP_FILENAME = "viewer.sdp"


def create_sdp_file(udp_port: int):
    """Create SDP file for RTP stream.
    
    Args:
        udp_port: UDP port to listen on for RTP stream.
    """
    # We must describe the stream for ffplay to recognize it
    sdp_content = f"""v=0
o=- 0 0 IN IP4 127.0.0.1
s=RTP Video
c=IN IP4 0.0.0.0
t=0 0
m=video {udp_port} RTP/AVP {PAYLOAD_TYPE}
a=rtpmap:{PAYLOAD_TYPE} H265/90000
"""
    with open(SDP_FILENAME, "w") as f:
        f.write(sdp_content)
    # print(f"Created temporary SDP file: {SDP_FILENAME}")


def play_stream(udp_port: int, framerate: float | None = None):
    """Play RTP stream from specified UDP port.
    
    Args:
        udp_port: UDP port to listen on for RTP stream.
        framerate: Optional framerate to set for playback. If None, uses stream's native framerate.
    """
    create_sdp_file(udp_port)

    # ffplay command arguments:
    # -protocol_whitelist: Allow file (SDP) and rtp/udp network
    # -fflags nobuffer: Reduce latency (try to play immediately)
    # -flags low_delay: Tell the decoder to output frames ASAP
    # -framedrop: Drop frames if the computer is too slow (keeps sync)
    # -vf fps=fps=X: Set framerate for playback using video filter (if specified)
    # -autoexit: Close window if the stream stops (optional, removed here to keep window open)
    command = [
        'ffplay',
        '-protocol_whitelist', 'file,udp,rtp',
        '-fflags', 'nobuffer',
        '-flags', 'low_delay',
        '-framedrop',
    ]
    
    if framerate is not None:
        command.extend(['-vf', f'fps=fps={framerate}'])
    
    command.append('-i')
    command.append(SDP_FILENAME)

    print(f"Opening video window listening on port {udp_port}...")
    print("Press 'q' or 'Esc' in the video window to quit.")

    try:
        process = subprocess.Popen(command)
        process.wait()  # Wait for the window to close
    except KeyboardInterrupt:
        print("\nClosing viewer...")
        process.terminate()
    finally:
        # Cleanup the temp file
        if os.path.exists(SDP_FILENAME):
            os.remove(SDP_FILENAME)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="View H.265 RTP stream from UDP port using ffplay."
    )
    parser.add_argument(
        "--port",
        "-p",
        type=int,
        default=DEFAULT_UDP_PORT,
        help=f"UDP port to listen on for RTP stream (default: {DEFAULT_UDP_PORT}).",
    )
    parser.add_argument(
        "--framerate",
        "-f",
        type=float,
        default=None,
        help="Framerate for playback (frames per second). If not specified, uses stream's native framerate.",
    )
    args = parser.parse_args()

    play_stream(args.port, args.framerate)
PY
