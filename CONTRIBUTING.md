# Contributing to VCAI

Thank you for your interest in improving **Video Coding for AI Tasks (VCAI)**.

External contributions are accepted **only through pull requests** (opened from your fork). Do not push commits directly to branches on the upstream repository unless a maintainer has granted you that access.

## Before you start

1. Read [README.md](README.md) for project scope and quick start.
2. Follow [INSTALL.md](INSTALL.md) (or [APPTAINER_DEV_ENV.md](APPTAINER_DEV_ENV.md) / [docker/DOCKER_USAGE.md](docker/DOCKER_USAGE.md)) so you can build and run locally.
3. Use [docs/development.md](docs/development.md) for repository layout, CMake options, and routine dev commands.

## How to submit a change

1. **Fork** the upstream repository on GitHub (use the **Fork** button on the project page).
2. **Clone your fork** locally. Optionally add `upstream` pointing at the canonical GitHub repository so you can fetch the latest default branch before opening a pull request.
3. **Work in your fork** — commit locally and push to your fork. Keep changes focused; avoid unrelated refactors in the same pull request.
4. **Open a pull request** from your fork into the upstream default branch (or the branch maintainers specify in an issue). Include:
   - a clear summary of what changed and why
   - test evidence (commands run, logs, or screenshots where relevant)
   - links to related issues (`Fixes #123` or `Relates to #456` when applicable)
5. **Respond to review** — address feedback with additional commits on the same branch in your fork; the pull request updates automatically.

Maintainers review and merge pull requests; there is no supported path to land external work by pushing straight to upstream branches.

## Development checks

After C++ edits, run (at minimum for the areas you touched):

- `clang-format` on changed `.cpp` / `.h` files
- the subproject `build.sh` (for example `encoder_cli/build.sh`)
- that component's `run_static_analysis.sh` when available
- [test/run.sh](test/run.sh) when behavior or tests change

## Tests

```bash
cd test
./run.sh
```

See [test/README.md](test/README.md) for targets and layout.

## Documentation

- User-facing manual sources live under [docs/](docs/).
- Keep [README.md](README.md) aimed at newcomers; deeper contributor detail belongs in [docs/development.md](docs/development.md).
- If you change behavior, update the relevant chapter under `docs/part*` or component README when appropriate.

## Code expectations

- C++20, shared patterns in [libvcai/](libvcai/README.md).
- Respect existing slice boundaries (`encoder_cli`, `rtp_server`, `libvcai`, `vcai_decoder`, `test`).
- Patched third-party code belongs under [patches/](patches/) with matching bootstrap/install docs—not drive-by vendor edits.

## Questions

Open a **GitHub issue** on the upstream repository for bugs and feature requests. For security-sensitive reports, see [SECURITY.md](SECURITY.md).
