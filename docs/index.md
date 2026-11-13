# VCAI Manual

Welcome to the **Video Coding for AI Tasks (VCAI)** HTML documentation manual. This site is generated from the same Markdown sources committed in the repository (single source of truth via includes).

## What is VCAI?

Video Coding for AI Tasks (VCAI) is a GStreamer-based video pipeline for **ROI-aware H.265** workflows. It embeds and consumes H.274-style metadata (Packed Regions Information) across encode, transport, and decode/display.

## Quick links

| Component        | Description                                      |
|-----------------|--------------------------------------------------|
| [**encoder_cli**](part03/ch09-encoder-overview.md) | Ingest, detection, ROI metadata in H.265, RTP/WebRTC out |
| [**rtp_server**](part04/ch14-receiver-overview.md) | Receive, decode, PRI/EOI extraction, overlays   |
| [**libvcai**](part05/ch19-lib-overview.md) | Shared C++20 core (SEI, ROI, communication) |

## How to build this manual locally

```bash
cd /path/to/VCAI
pip install -r requirements-docs.txt   # see repo root
./docs/build_manual.sh
```

Open [**manual/index.html**](../manual/index.html) in a browser (file:// from repo root). Do not use `mkdocs serve` — links to repo files only work when viewing the built site from the filesystem.

## Reading order

Use the navigation tabs: **Part I** (foundation) through **Part X** (appendices), or jump to **encoder_cli** / **rtp_server** / **libvcai** as needed.
