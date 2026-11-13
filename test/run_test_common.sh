#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI shared test harness.
# Sourced by per-component run scripts (run_encoder_cli.sh, run_rtp_server.sh,
# run_vcai_decoder.sh) and orchestrated by run.sh. Provides the common
# test-running machinery: environment setup, single-binary execution, log
# capture, and GoogleTest result parsing.

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Get the script directory and project root.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_BUILD_DIR="$SCRIPT_DIR/test_build"
# The directory holding the GoogleTest binaries for the current component.
# Overridable per component (vcai_decoder lives in its own subdir).
TEST_BIN_DIR="${TEST_BIN_DIR:-$TEST_BUILD_DIR/test_bin}"

# Test results tracking
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0
SKIPPED_TESTS=0
FAILED_TEST_NAMES=()
SKIPPED_TEST_NAMES=()

# Set up DeepStream plugin paths for GStreamer.
setup_deepstream_env() {
    if [ -z "${ORIGINAL_PKG_CONFIG_PATH+x}" ]; then
        export ORIGINAL_PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}"
        export ORIGINAL_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
        export ORIGINAL_CMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}"
        export ORIGINAL_CMAKE_LIBRARY_PATH="${CMAKE_LIBRARY_PATH:-}"
        export ORIGINAL_GST_PLUGIN_PATH="${GST_PLUGIN_PATH:-}"
        export ORIGINAL_GST_PLUGIN_SCANNER="${GST_PLUGIN_SCANNER:-}"
    fi

    local arch_triplet
    arch_triplet="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo "x86_64-linux-gnu")"

    local deepstream_system_plugin_path="/usr/lib/${arch_triplet}/gstreamer-1.0/deepstream"
    local deepstream_symlink_dir="/opt/nvidia/deepstream/deepstream"
    local deepstream_versioned_dir="/opt/nvidia/deepstream/deepstream-7.1"
    local deepstream_yolo_lib_dir="${PROJECT_ROOT}/yolo/DeepStream-Yolo/nvdsinfer_custom_impl_Yolo"
    local vcai_decoder_plugin_dir_1="${PROJECT_ROOT}/test/test_build/vcai_decoder"
    local vcai_decoder_plugin_dir_2="${PROJECT_ROOT}/build/rtp_server/vcai_decoder"
    local vcai_decoder_plugin_dir_3="${PROJECT_ROOT}/build/vcai_decoder/vcai_decoder"
    local openhevc_install_dir="${PROJECT_ROOT}/lib/openhevc"
    local openhevc_plugin_dir="${openhevc_install_dir}/lib/gstreamer-1.0"
    local system_gst_plugin_dir="/usr/lib/${arch_triplet}/gstreamer-1.0"

    local gst_plugin_paths=()
    if [ -d "${system_gst_plugin_dir}" ]; then
        gst_plugin_paths+=("${system_gst_plugin_dir}")
    fi
    if [ -d "${vcai_decoder_plugin_dir_1}" ]; then
        gst_plugin_paths+=("${vcai_decoder_plugin_dir_1}")
    fi
    if [ -d "${vcai_decoder_plugin_dir_2}" ]; then
        gst_plugin_paths+=("${vcai_decoder_plugin_dir_2}")
    fi
    if [ -d "${vcai_decoder_plugin_dir_3}" ]; then
        gst_plugin_paths+=("${vcai_decoder_plugin_dir_3}")
    fi
    if [ -d "${deepstream_system_plugin_path}" ]; then
        gst_plugin_paths+=("${deepstream_system_plugin_path}")
    fi
    if [ -d "${deepstream_symlink_dir}/lib/gstreamer-1.0" ]; then
        gst_plugin_paths+=("${deepstream_symlink_dir}/lib/gstreamer-1.0")
    fi
    if [ -d "${deepstream_versioned_dir}/lib/gstreamer-1.0" ]; then
        gst_plugin_paths+=("${deepstream_versioned_dir}/lib/gstreamer-1.0")
    fi
    if [ -d "${openhevc_plugin_dir}" ]; then
        gst_plugin_paths+=("${openhevc_plugin_dir}")
    fi

    local ld_library_paths=()
    if [ -d "${deepstream_symlink_dir}/lib" ]; then
        ld_library_paths+=("${deepstream_symlink_dir}/lib")
    fi
    if [ -d "${deepstream_versioned_dir}/lib" ]; then
        ld_library_paths+=("${deepstream_versioned_dir}/lib")
    fi
    if [ -d "${deepstream_yolo_lib_dir}" ]; then
        ld_library_paths+=("${deepstream_yolo_lib_dir}")
    fi
    if [ -d "${openhevc_install_dir}/lib" ]; then
        ld_library_paths+=("${openhevc_install_dir}/lib")
    fi

    if [ ${#gst_plugin_paths[@]} -gt 0 ]; then
        export GST_PLUGIN_PATH="$(IFS=:; echo "${gst_plugin_paths[*]}")${ORIGINAL_GST_PLUGIN_PATH:+:${ORIGINAL_GST_PLUGIN_PATH}}"
    fi
    if [ ${#ld_library_paths[@]} -gt 0 ]; then
        export LD_LIBRARY_PATH="$(IFS=:; echo "${ld_library_paths[*]}"):${ORIGINAL_LD_LIBRARY_PATH}"
    fi

    print_info "GST_PLUGIN_PATH set to: ${GST_PLUGIN_PATH}"
    print_info "LD_LIBRARY_PATH set to: ${LD_LIBRARY_PATH}"
}

# Restore original GStreamer environment.
restore_original_gstreamer() {
    if [ -n "${ORIGINAL_PKG_CONFIG_PATH+x}" ]; then
        export PKG_CONFIG_PATH="${ORIGINAL_PKG_CONFIG_PATH}"
        export LD_LIBRARY_PATH="${ORIGINAL_LD_LIBRARY_PATH}"
        export CMAKE_PREFIX_PATH="${ORIGINAL_CMAKE_PREFIX_PATH}"
        export CMAKE_LIBRARY_PATH="${ORIGINAL_CMAKE_LIBRARY_PATH}"
        export GST_PLUGIN_PATH="${ORIGINAL_GST_PLUGIN_PATH}"
        export GST_PLUGIN_SCANNER="${ORIGINAL_GST_PLUGIN_SCANNER}"
        unset ORIGINAL_PKG_CONFIG_PATH
        unset ORIGINAL_LD_LIBRARY_PATH
        unset ORIGINAL_CMAKE_PREFIX_PATH
        unset ORIGINAL_CMAKE_LIBRARY_PATH
        unset ORIGINAL_GST_PLUGIN_PATH
        unset ORIGINAL_GST_PLUGIN_SCANNER
    else
        unset PKG_CONFIG_PATH
        unset LD_LIBRARY_PATH
        unset CMAKE_PREFIX_PATH
        unset CMAKE_LIBRARY_PATH
        unset GST_PLUGIN_PATH
        unset GST_PLUGIN_SCANNER
    fi
}

# Run a single test binary and record its results into the global counters.
run_test_binary() {
    local test_name="$1"
    local quick_mode="$2"
    local output_dir="$3"
    local timestamp="$4"

    print_info "Running $test_name tests..."

    if [ ! -f "$TEST_BIN_DIR/$test_name" ]; then
        print_error "$test_name executable not found in $TEST_BIN_DIR. Please build tests first."
        return 1
    fi

    local test_output_dir="${output_dir}/${test_name}"
    if ! mkdir -p "$test_output_dir"; then
        print_error "Failed to create output directory: $test_output_dir"
        return 1
    fi

    local log_file="${test_output_dir}/test_run_${timestamp}.log"
    local xml_file="${test_output_dir}/test_results.xml"

    setup_deepstream_env

    cd "$TEST_BIN_DIR"

    local exit_code
    local gstreamer_crash=false
    local temp_output
    temp_output=$(mktemp)

    if [ "$quick_mode" = "true" ]; then
        print_info "Running $test_name in quick mode..."
        ./"$test_name" --gtest_shuffle=false 2>&1 | tee "$log_file" | tee "$temp_output"
        exit_code=${PIPESTATUS[0]}
    else
        print_info "Running $test_name with detailed output..."
        ./"$test_name" --gtest_output=xml:"$xml_file" --gtest_shuffle=false 2>&1 | tee "$log_file" | tee "$temp_output"
        exit_code=${PIPESTATUS[0]}
    fi

    # Exit code 133 is SIGTRAP from GStreamer deinitialization crash (known issue).
    if [ $exit_code -eq 133 ]; then
        gstreamer_crash=true
        exit_code=0
    fi

    # Read test output from log file (preferred) or temp file (fallback).
    local test_output
    if [ -f "$log_file" ] && [ -s "$log_file" ]; then
        test_output=$(cat "$log_file")
    elif [ -f "$temp_output" ] && [ -s "$temp_output" ]; then
        test_output=$(cat "$temp_output")
        cp "$temp_output" "$log_file" 2>/dev/null || cat "$temp_output" > "$log_file" 2>/dev/null || true
    else
        test_output=""
        print_warning "Failed to capture $test_name test output"
    fi

    rm -f "$temp_output"

    parse_test_results "$test_output"

    local test_passed=$((PASSED_TESTS))
    local test_failed=$((FAILED_TESTS))
    local test_skipped=$((SKIPPED_TESTS))
    local test_total=$((TOTAL_TESTS))

    restore_original_gstreamer

    echo ""
    print_info "=== $test_name Results ==="
    echo "Total: $test_total | Passed: $test_passed | Failed: $test_failed | Skipped: $test_skipped"
    print_info "Log file: $log_file"

    if [ "$gstreamer_crash" = "true" ]; then
        print_warning "$test_name crashed with GStreamer deinitialization error (exit code 133)"
    fi

    if [ $test_failed -gt 0 ]; then
        return 1
    else
        return 0
    fi
}

# Run the configured TEST_BINARIES for the current component and report a summary.
run_tests() {
    local quick_mode="$1"

    local output_dir="$SCRIPT_DIR/test_output"
    if ! mkdir -p "$output_dir"; then
        print_error "Failed to create output directory: $output_dir"
        return 1
    fi

    output_dir="$(cd "$output_dir" && pwd)"

    local timestamp=$(date +"%Y%m%d_%H%M%S")

    if [ ! -w "$output_dir" ]; then
        print_error "Output directory is not writable: $output_dir"
        return 1
    fi

    print_info "Test output will be saved to: $output_dir"

    local any_failed=false
    local binary
    local bin_status=()
    for binary in "${TEST_BINARIES[@]}"; do
        if run_test_binary "$binary" "$quick_mode" "$output_dir" "$timestamp"; then
            bin_status+=("$binary PASSED")
        else
            bin_status+=("$binary FAILED")
            any_failed=true
        fi
        echo ""
        echo "------------------------------------------"
    done

    echo ""
    echo "=========================================="
    echo "${COMPONENT_NAME:-TEST} RESULTS SUMMARY"
    echo "=========================================="
    local entry
    for entry in "${bin_status[@]}"; do
        echo "  - $entry"
    done
    echo "=========================================="

    if [ "$any_failed" = "true" ]; then
        print_error "${COMPONENT_NAME:-Some} test suites failed"
        return 1
    else
        print_success "All ${COMPONENT_NAME:-test} suites passed!"
        return 0
    fi
}

# Parse GoogleTest output into the global counters.
parse_test_results() {
    local output="$1"

    TOTAL_TESTS=0
    PASSED_TESTS=0
    FAILED_TESTS=0
    SKIPPED_TESTS=0
    FAILED_TEST_NAMES=()
    SKIPPED_TEST_NAMES=()

    local total_line=$(echo "$output" | grep "\[==========\] Running.*tests from.*test suites")
    if [[ $total_line =~ ([0-9]+)\ tests ]]; then
        TOTAL_TESTS=${BASH_REMATCH[1]}
    fi

    local passed_summary=$(echo "$output" | grep "\[  PASSED  \]" | grep -E "[0-9]+ tests" | tail -1)
    if [[ $passed_summary =~ ([0-9]+)\ tests ]]; then
        PASSED_TESTS=${BASH_REMATCH[1]}
    else
        local passed_count=$(echo "$output" | grep "\[       OK \]" | grep -vE "(tests\.|tests,)" | wc -l)
        PASSED_TESTS=$((passed_count + 0))
    fi

    local failed_summary=$(echo "$output" | grep "\[  FAILED  \]" | grep -E "[0-9]+ tests" | tail -1)
    if [[ $failed_summary =~ ([0-9]+)\ tests ]]; then
        FAILED_TESTS=${BASH_REMATCH[1]}
    else
        local failed_count
        failed_count=$(echo "$output" | grep "\[  FAILED  \]" | grep -vE "(tests, listed below:|test, listed below:|tests\.)" | grep -E "\." | sed 's/.*\[  FAILED  \] //' | sed 's/ (.*//' | sort -u | wc -l)
        FAILED_TESTS=$((failed_count + 0))
    fi

    local skipped_summary=$(echo "$output" | grep "\[  SKIPPED \]" | grep -E "[0-9]+ tests" | tail -1)
    if [[ $skipped_summary =~ ([0-9]+)\ tests ]]; then
        SKIPPED_TESTS=${BASH_REMATCH[1]}
    else
        local skipped_count=$(echo "$output" | grep "\[  SKIPPED \]" | grep -vE "(tests, listed below:|test, listed below:|tests\.)" | grep -E "\." | wc -l)
        SKIPPED_TESTS=$((skipped_count + 0))
    fi

    if [ "$TOTAL_TESTS" -eq 0 ]; then
        TOTAL_TESTS=$((PASSED_TESTS + FAILED_TESTS + SKIPPED_TESTS))
    fi

    if [ "$FAILED_TESTS" -gt 0 ]; then
        FAILED_TEST_NAMES=($(echo "$output" | grep "\[  FAILED  \]" | grep -vE "(tests, listed below:|test, listed below:|tests\.)" | grep -E "\." | sed 's/.*\[  FAILED  \] //' | sed 's/ (.*//' | sort -u))
        local failed_count=${#FAILED_TEST_NAMES[@]}
        if [ "$failed_count" -eq 0 ]; then
            FAILED_TEST_NAMES=($(echo "$output" | grep -E "(FAILED|Failure)" | grep -v "\[  FAILED  \]" | grep -v "tests, listed below:" | sed 's/.*test_//' | sed 's/\.cpp.*//' | sort -u))
        fi
    fi

    if [ "$SKIPPED_TESTS" -gt 0 ]; then
        SKIPPED_TEST_NAMES=($(echo "$output" | grep "\[  SKIPPED \]" | grep -vE "(tests, listed below:|test, listed below:|tests\.)" | grep -E "\." | sed 's/.*\[  SKIPPED \] //' | sed 's/ (.*//' | sort -u))
    fi
}

# Show recent log files for the configured TEST_BINARIES.
show_recent_logs() {
    local output_dir="$SCRIPT_DIR/test_output"

    if [ ! -d "$output_dir" ]; then
        print_info "No test output directory found. Run tests first to generate logs."
        return
    fi

    print_info "Recent test log files:"
    echo ""

    local found_logs=false
    local binary
    for binary in "${TEST_BINARIES[@]}"; do
        local test_dir="${output_dir}/${binary}"
        if [ -d "$test_dir" ] && ls "$test_dir"/test_run_*.log >/dev/null 2>&1; then
            found_logs=true
            echo "=== $binary ==="
            echo "Timestamp              | Size    | Filename"
            echo "----------------------|---------|----------------------------------------"
            ls -la "$test_dir"/test_run_*.log | awk -v dir="$binary" '{printf "%-20s | %7s | %s/%s\n", $6" "$7" "$8, $5, dir, $9}' | tail -10
            echo ""
        fi
    done

    if [ "$found_logs" = "false" ]; then
        print_info "No test log files found."
    else
        print_info "To view a specific log file, use: cat $output_dir/<binary>/test_run_YYYYMMDD_HHMMSS.log"
    fi
}
