#!/usr/bin/env bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -euo pipefail
exec python3 - "$@" <<'PY'
import subprocess
import signal
import sys
import os

# Configuration
UDP_PORT = 12300
PAYLOAD_TYPE = 107
OUTPUT_FILE = "videos/simulation.mkv"
SDP_FILENAME = "temp_stream.sdp"


def create_sdp_file():
    # Defines the input stream as H.265 (HEVC)
    sdp_content = f"""v=0
o=- 0 0 IN IP4 127.0.0.1
s=No Name
c=IN IP4 0.0.0.0
t=0 0
m=video {UDP_PORT} RTP/AVP {PAYLOAD_TYPE}
a=rtpmap:{PAYLOAD_TYPE} H265/90000
"""
    with open(SDP_FILENAME, "w") as f:
        f.write(sdp_content)
    print(f"Generated SDP file: {SDP_FILENAME}")


def capture_stream():
    create_sdp_file()

    command = [
        'ffmpeg',
        '-protocol_whitelist', 'file,udp,rtp',
        '-i', SDP_FILENAME,

        # --- FIX & ENCODE SETTINGS ---
        '-vf', 'fps=30',  # FORCE timestamp correction (30 unique fps)
        '-c:v', 'libx265',  # Encode output as H.265
        '-preset', 'ultrafast',  # Crucial for live H.265 recording (prevents lag)
        '-crf', '28',  # Quality (28 is standard for H.265, lower is better)
        # -----------------------------

        '-y',
        OUTPUT_FILE
    ]

    print(f"Listening for H.265 RTP (Type {PAYLOAD_TYPE}) on port {UDP_PORT}...")
    print(f"Re-encoding live to {OUTPUT_FILE}...")
    print("(Press Ctrl+C to stop)")

    process = subprocess.Popen(
        command,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    def signal_handler(sig, frame):
        print("\nStopping capture...")
        try:
            # Send 'q' to ffmpeg to close the file gracefully
            process.stdin.write(b'q')
            process.stdin.flush()
            process.wait(timeout=5)
        except:
            print("Force killing process...")
            process.kill()

        if os.path.exists(SDP_FILENAME):
            os.remove(SDP_FILENAME)

        print("Capture saved.")
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)

    try:
        while True:
            line = process.stderr.readline()
            if not line:
                if process.poll() is not None:
                    break
                continue
    except KeyboardInterrupt:
        signal_handler(None, None)


if __name__ == "__main__":
    capture_stream()
PY
