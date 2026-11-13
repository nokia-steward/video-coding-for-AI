# Chapter 15 — rtp_server build, config, run

## Building and configuration (from rtp_server README)

Excerpt: [**build.sh**](../../rtp_server/build.sh) variants and [**rtp_server_config.ini**](../../rtp_server/rtp_server_config.ini) section list.

--8<-- "rtp_server/README.md:52:100"

## Run commands

From [**rtp_server/**](../../rtp_server/): [**run.sh**](../../rtp_server/run.sh)

```bash
./run.sh rtp
./run.sh webrtc
./run.sh rtp 192.168.1.100:5006
./run.sh rtp -- --debug-draw-bitrate --debug-draw-fps
```

## Config reference (manual)

- [rtp_server_config — Server and Common](../config/rtp_server_config-overview.md)
- [WebRTC and Monitoring](../config/rtp_server_config-webrtc-monitoring.md)
- [RTPOutput and Debug](../config/rtp_server_config-rtpoutput-debug.md)
- [TET, Decoder, Inference, TestMode, …](../config/rtp_server_config-tet-rest.md)

Full option tables and test mode narrative: **Chapter 14** ([**rtp_server/README.md**](../../rtp_server/README.md)).
