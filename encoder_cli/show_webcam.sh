#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# This script launches a GStreamer pipeline to display a specific webcam.

echo "Starting webcam feed... Press Ctrl+C in the terminal to stop."

# Specify the device for the webcam source element.
# Replace /dev/videoX with your external webcam's path (e.g., /dev/video2).
gst-launch-1.0 v4l2src device=/dev/video2 ! 'video/x-raw,width=1280,height=960' ! videoconvert ! autovideosink
