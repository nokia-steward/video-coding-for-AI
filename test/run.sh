#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI Test Runner.
# Orchestrates the build and runs all component test scripts
# (encoder_cli, rtp_server, vcai_decoder), reporting an overall summary.

set -e

# Load shared test harness
# shellcheck source=run_test_common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/run_test_common.sh"

# Component run scripts, in execution order. Each supports run/logs/help and
# exits non-zero on test failure.
COMPONENT_SCRIPTS=(
    run_encoder_cli.sh
    run_rtp_server.sh
    run_vcai_decoder.sh
)

show_usage() {
    echo "VCAI Test Runner"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  run          Ensure tests are built (incremental) and run all component tests (default)"
    echo "  logs         Show recent test log files across all components"
    echo "  help         Show this help message"
    echo ""
    echo "Components:"
    for script in "${COMPONENT_SCRIPTS[@]}"; do
        echo "  - $script"
    done
}

# Ensure all test binaries are built (incremental). Encoder/rtp_server are built
# via build.sh; the decoder target is built by its own run script.
ensure_tests_built() {
    local build_script="$SCRIPT_DIR/build.sh"

    if [ ! -f "$build_script" ]; then
        print_error "Build script not found at: $build_script"
        exit 1
    fi

    print_info "Ensuring test binaries are built (incremental)..."
    if ! bash "$build_script"; then
        print_error "Failed to build test binaries"
        exit 1
    fi
}

# Run every component test script and aggregate the results.
run_all_components() {
    local failed_comps=()
    local script
    for script in "${COMPONENT_SCRIPTS[@]}"; do
        local script_path="$SCRIPT_DIR/$script"
        echo ""
        echo "##########################################"
        echo "### $script"
        echo "##########################################"
        if ! bash "$script_path" "run"; then
            failed_comps+=("$script")
        fi
    done

    echo ""
    echo "=========================================="
    echo "OVERALL TEST RESULTS SUMMARY"
    echo "=========================================="
    if [ ${#failed_comps[@]} -gt 0 ]; then
        for script in "${COMPONENT_SCRIPTS[@]}"; do
            if printf '%s\n' "${failed_comps[@]}" | grep -qx "$script"; then
                echo "  - $script: FAILED"
            else
                echo "  - $script: PASSED"
            fi
        done
        echo "=========================================="
        print_error "Some test suites failed"
        exit 1
    else
        for script in "${COMPONENT_SCRIPTS[@]}"; do
            echo "  - $script: PASSED"
        done
        echo "=========================================="
        print_success "All test suites passed!"
        exit 0
    fi
}

# Show recent logs across all component scripts.
show_all_logs() {
    local script
    for script in "${COMPONENT_SCRIPTS[@]}"; do
        echo ""
        echo "=== $script ==="
        bash "$SCRIPT_DIR/$script" "logs"
    done
}

main() {
    local command="${1:-run}"

    case "$command" in
        "run"|"")
            print_info "Ensuring build is up to date and running all tests..."
            ensure_tests_built
            run_all_components
            ;;
        "help"|"-h"|"--help")
            show_usage
            ;;
        "logs")
            show_all_logs
            ;;
        *)
            print_error "Unknown command: $command"
            show_usage
            exit 1
            ;;
    esac
}

main "$@"
