#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI Decoder test runner.
# Ensures the decoder GoogleTest binary is built, then runs it.
# The decoder binary is built into test_build/test_bin (same as other GoogleTest targets).

# Load shared test harness
# shellcheck source=run_test_common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_test_common.sh"

COMPONENT_NAME="VCAI Decoder"
TEST_BIN_DIR="$TEST_BUILD_DIR/test_bin"
TEST_BINARIES=(test_vcai_decoder)

ensure_decoder_built() {
    print_info "Building test_vcai_decoder (incremental)..."
    if ! cmake --build "$TEST_BUILD_DIR" --target test_vcai_decoder -- -j"$(nproc 2>/dev/null || echo 4)"; then
        print_error "Failed to build test_vcai_decoder. Run ./build.sh first."
        exit 1
    fi
}

show_usage() {
    echo "VCAI Decoder Test Runner"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  run          Build (if needed) and run decoder tests (default)"
    echo "  logs         Show recent decoder test log files"
    echo "  help         Show this help message"
}

main() {
    local command="${1:-run}"

    case "$command" in
        "run"|"")
            ensure_decoder_built
            run_tests "false"
            ;;
        "logs")
            show_recent_logs
            ;;
        "help"|"-h"|"--help")
            show_usage
            ;;
        *)
            print_error "Unknown command: $command"
            show_usage
            exit 1
            ;;
    esac
}

main "$@"
