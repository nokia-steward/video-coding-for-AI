#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI RTP Server test runner.
# Runs the rtp_server GoogleTest binary (built by ./build.sh).

# Load shared test harness
# shellcheck source=run_test_common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_test_common.sh"

COMPONENT_NAME="RTP Server"
TEST_BINARIES=(test_rtp_server)

show_usage() {
    echo "VCAI RTP Server Test Runner"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  run          Run rtp_server tests (default)"
    echo "  logs         Show recent rtp_server test log files"
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
