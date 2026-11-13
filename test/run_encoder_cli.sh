#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI Encoder CLI test runner.
# Runs the encoder_cli GoogleTest binary (built by ./build.sh).

# Load shared test harness
# shellcheck source=run_test_common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_test_common.sh"

COMPONENT_NAME="Encoder CLI"
TEST_BINARIES=(test_encoder)

show_usage() {
    echo "VCAI Encoder CLI Test Runner"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  run          Run encoder CLI tests (default)"
    echo "  logs         Show recent encoder test log files"
    echo "  help         Show this help message"
}

main() {
    local command="${1:-run}"

    case "$command" in
        "run"|"")
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
