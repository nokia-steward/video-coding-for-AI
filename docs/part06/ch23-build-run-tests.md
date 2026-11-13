# Chapter 23 — Build and run tests

From [**test/README.md**](../../test/README.md) (Chapter 22): [**test/build.sh**](../../test/build.sh), [**test/run.sh**](../../test/run.sh).

```bash
cd test
./build.sh      # optional explicit build
./run.sh        # auto-build then run
```

Targets: `test_encoder`, `test_rtp_server`. libvcai tests are split across both binaries.
