#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." &> /dev/null && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/vcai_decoder"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" -DBUILD_VCAI_DECODER=ON -DBUILD_VCAI_DECODER_TESTS=ON
cmake --build "${BUILD_DIR}" --target test_vcai_decoder
ctest --test-dir "${BUILD_DIR}/vcai_decoder" --output-on-failure -R vcai_decoder_tests
