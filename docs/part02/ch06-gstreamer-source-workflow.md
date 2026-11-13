# Chapter 6 — GStreamer workflow

The project uses GStreamer together with the `vcai_decoder` plugin chain:

- `sfh265seiextract`
- `sfprireconstruct`
- `sfeoilumabackscale`
- `sftetapply`

Standard setup:

```bash
./scripts/setup/bootstrap.sh core
```
