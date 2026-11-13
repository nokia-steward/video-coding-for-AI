# Chapter 26 — Docker

Usage wrappers and host directories: **docker/DOCKER_USAGE.md** (included below).

**Docker script and doc links:** [DOCKER_USAGE.md](../../docker/DOCKER_USAGE.md) · [run_rtp_server.sh](../../docker/run_rtp_server.sh) · [run_encoder_cli.sh](../../docker/run_encoder_cli.sh) · [init_directories.sh](../../docker/init_directories.sh)

## Image layout (committed Dockerfiles)

| Image | Dockerfile | Purpose |
|-------|------------|--------|
| **vcai-base** | [`docker/Dockerfile_base`](../../docker/Dockerfile_base) | DeepStream 7.1 triton-devel + build deps; shared base for apps |
| **vcai-rtp-server** | [`docker/Dockerfile_rtp_server`](../../docker/Dockerfile_rtp_server) | Multi-stage: builder runs dependency bootstrap and `rtp_server/build.sh`; runtime copies artifacts |
| **vcai-encoder-cli** | [`docker/Dockerfile_encoder_cli`](../../docker/Dockerfile_encoder_cli) | Multi-stage: builder runs `install_kvazaar.sh`, `bootstrap.sh ds-env`, `setup_yolo.sh`, `encoder_cli/build.sh`; stages kvazaar .so into runtime |

**RTP server builder** runs project dependency bootstrap inside the image. **Encoder builder** installs Kvazaar into
`lib/kvazaar` and YOLO env before building encoder_cli.
For host-side setup command behavior, see [**Host Setup Bootstrap Guide**](../part07/scripts-setup-readme.md).

## Dockerfile_base (excerpt)

--8<-- "docker/Dockerfile_base:1:35"

Full files: [**docker/Dockerfile_base**](../../docker/Dockerfile_base), [**docker/Dockerfile_rtp_server**](../../docker/Dockerfile_rtp_server), [**docker/Dockerfile_encoder_cli**](../../docker/Dockerfile_encoder_cli).

--8<-- "docker/DOCKER_USAGE.md"
