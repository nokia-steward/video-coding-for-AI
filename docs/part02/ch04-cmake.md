# Chapter 4 — CMake and subprojects

## Root CMakeLists.txt

The root project sets C++20, optional subprojects (`BUILD_ENCODER_CLI`, `BUILD_RTP_SERVER`, `BUILD_VCAI_DECODER`, `BUILD_TESTS`), and includes [`cmake/cpp_warnings.cmake`](../../cmake/cpp_warnings.cmake). **libvcai** is built as a local target inside each consumer (`encoder_cli_vcai`, `rtp_server_vcai`, test targets), not as a top-level library.

--8<-- "CMakeLists.txt"

## libvcai source reuse

Sources are listed in [`libvcai/libvcai_sources.cmake`](../../libvcai/libvcai_sources.cmake). See **Chapter 19 (Part V)** for the full library overview.
