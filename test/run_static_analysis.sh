#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# Test Project Static Analysis Script
# Runs Clang Static Analyzer and Cppcheck with test-specific configurations

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMMON_STATIC_ANALYSIS_SCRIPT="${PROJECT_ROOT}/../scripts/lib/common_static_analysis.sh"
PROJECT_NAME="test"
if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    TIMESTAMP="$(date +"%Y%m%d_%H%M%S")"
    ANALYSIS_DIR="${PROJECT_ROOT}/../static_analysis_results"
else
    TIMESTAMP="${2:-$(date +"%Y%m%d_%H%M%S")}"
    ANALYSIS_DIR="${1:-${PROJECT_ROOT}/../static_analysis_results}"
fi
RESULTS_FILE="${ANALYSIS_DIR}/${PROJECT_NAME}_analysis_${TIMESTAMP}.txt"
HTML_DIR="${ANALYSIS_DIR}/${PROJECT_NAME}_html_${TIMESTAMP}"
clang_path=""
build_dir=""
build_targets=()
source_roots=()

if [[ ! -f "${COMMON_STATIC_ANALYSIS_SCRIPT}" ]]; then
    echo "ERROR: Missing common static analysis script at ${COMMON_STATIC_ANALYSIS_SCRIPT}"
    exit 1
fi
source "${COMMON_STATIC_ANALYSIS_SCRIPT}"

# Create directories
mkdir -p "${ANALYSIS_DIR}"
mkdir -p "${HTML_DIR}"

# Help function
show_help() {
    echo "Usage: $0 [ANALYSIS_DIR] [TIMESTAMP]"
    echo ""
    echo "Parameters:"
    echo "  ANALYSIS_DIR  Directory to store analysis results (default: ../static_analysis_results)"
    echo "  TIMESTAMP      Timestamp for results (default: current timestamp)"
    echo ""
    echo "Examples:"
    echo "  $0                                    # Use defaults"
    echo "  $0 /tmp/analysis                     # Custom directory"
    echo "  $0 /tmp/analysis 20240101_120000    # Custom directory and timestamp"
    echo ""
    echo "Current configuration:"
    echo "  Analysis directory: ${ANALYSIS_DIR}"
    echo "  Timestamp: ${TIMESTAMP}"
    echo ""
}

# Check for help flag
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    show_help
    exit 0
fi

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Test Project Static Analysis${NC}"
echo -e "${BLUE}========================================${NC}"

# Function to log results
log_result() {
    echo "$1" | tee -a "${RESULTS_FILE}"
}

log_result "Test Project Static Analysis Report"
log_result "==================================="
log_result "Timestamp: ${TIMESTAMP}"
log_result "Project: ${PROJECT_NAME}"
log_result "Source root: ${PROJECT_ROOT}"
log_result ""

# Check if required tools are available
check_tools() {
    check_required_static_analysis_tools "log_result" "clang_path"
}

# Initialize source roots for test analysis.
init_source_roots() {
    source_roots=()
    local candidates=(
        "${PROJECT_ROOT}/encoder_cli"
        "${PROJECT_ROOT}/libvcai"
        "${PROJECT_ROOT}/rtp_server"
        "${PROJECT_ROOT}/common"
        "${PROJECT_ROOT}/src"
    )
    for candidate in "${candidates[@]}"; do
        if [[ -d "${candidate}" ]]; then
            source_roots+=("${candidate}")
        fi
    done
}

# Select a build directory and targets for test analysis.
select_build_targets() {
    local candidates=(
        "${PROJECT_ROOT}/test_build"
        "${PROJECT_ROOT}/../build/test"
        "${PROJECT_ROOT}/../build"
    )
    for candidate in "${candidates[@]}"; do
        if [[ -d "${candidate}" ]]; then
            build_dir="${candidate}"
            build_targets=("test_encoder" "test_rtp_server")
            return 0
        fi
    done
    return 1
}

# Run Clang Static Analyzer
run_clang_analyzer() {
    log_result "Running Clang Static Analyzer..."
    log_result "--------------------------------"
    
    # Set up environment for GStreamer plus optional local plugins
    setup_static_analysis_environment
    
    # Build with scan-build
    cd "${PROJECT_ROOT}"
    
    if ! select_build_targets; then
        log_result "Warning: Build directory not found. Skipping Clang Static Analyzer for tests."
        log_result "Hint: run test/run.sh or configure a root build with BUILD_TESTS to enable analyzer runs."
        log_result ""
        return 0
    fi
    
    log_result "Using clang analyzer: ${clang_path}"
    log_result "Using build directory: ${build_dir}"
    
    for target in "${build_targets[@]}"; do
        log_result "Using build target: ${target}"
        if run_scan_build_command "${HTML_DIR}" "${clang_path}" "Test Project Static Analysis" "${RESULTS_FILE}" \
            cmake --build "${build_dir}" --target "${target}"; then
            log_result "✓ Clang Static Analyzer completed successfully for ${target}"
        else
            log_result "✗ Clang Static Analyzer found issues for ${target}"
            return 1
        fi
    done
    
    if [[ -f "${HTML_DIR}/index.html" ]]; then
        log_result "HTML report: ${HTML_DIR}/index.html"
    else
        log_result "HTML report: not generated (scan-build found no bugs)"
    fi
    
    log_result ""
}

# Run Cppcheck
run_cppcheck() {
    log_result "Running Cppcheck..."
    log_result "-------------------"
    
    # Cppcheck configuration with comprehensive checks
    local cppcheck_args=(
        "--enable=all"
        "--inconclusive"
        "--std=c++20"
        "--verbose"
        "--error-exitcode=1"
        "--suppress=missingIncludeSystem"
        "--suppress=unmatchedSuppression"
        "--include=${PROJECT_ROOT}"
        "--include=${PROJECT_ROOT}/common"
        "--include=/home/research/Nokia/VCAI/libvcai/include"
        "--include=/home/research/Nokia/VCAI/lib/openhevc/include"
        "--include=/usr/include/gstreamer-1.0"
        "--include=/usr/include/glib-2.0"
        "--include=/usr/lib/x86_64-linux-gnu/glib-2.0/include"
        "--xml"
        "--xml-version=2"
        "--output-file=${ANALYSIS_DIR}/${PROJECT_NAME}_cppcheck_${TIMESTAMP}.xml"
        "--check-config"
        "--platform=unix64"
    )
    
    if [[ ${#source_roots[@]} -eq 0 ]]; then
        log_result "No source roots found under ${PROJECT_ROOT}"
        return 1
    fi
    
    log_result "Source roots: ${source_roots[*]}"
    
    # Add source files
    local source_files=()
    collect_c_family_sources source_files "${source_roots[@]}"
    
    if [[ ${#source_files[@]} -eq 0 ]]; then
        log_result "No source files found under ${PROJECT_ROOT}"
        return 1
    fi
    
    log_result "Analyzing ${#source_files[@]} source files..."
    log_result "Files: ${source_files[*]}"
    log_result ""
    
    if cppcheck "${cppcheck_args[@]}" "${source_files[@]}" 2>&1 | tee -a "${RESULTS_FILE}"; then
        log_result "✓ Cppcheck completed successfully"
    else
        log_result "✗ Cppcheck found issues"
        return 1
    fi
    
    log_result ""
}

# Run clang-tidy ownership checks.
run_clang_tidy_ownership() {
    if [[ -z "${build_dir}" ]]; then
        if ! select_build_targets; then
            log_result "Build directory not found for clang-tidy ownership checks."
            return 1
        fi
    fi
    run_clang_tidy_ownership_checks "log_result" "${build_dir}" "${source_roots[@]}"
}

# Run additional checks
run_additional_checks() {
    local has_blocking_violations=false
    if [[ ${#source_roots[@]} -eq 0 ]]; then
        log_result "No source roots found under ${PROJECT_ROOT}"
        return 1
    fi

    log_result "Running Additional Checks..."
    log_result "---------------------------"
    log_result "Source roots: ${source_roots[*]}"

    for source_root in "${source_roots[@]}"; do
        if ! run_common_additional_checks "${source_root}" "log_result"; then
            has_blocking_violations=true
        fi
    done

    # Project-specific namespace rule: tests should not define or use sf namespace.
    log_result "Checking for namespace violations..."
    if grep -r -n --include="*.cpp" --include="*.h" \
       -E "(using\\s+namespace\\s+sf|namespace\\s+sf)" "${source_roots[@]}" 2>/dev/null; then
        log_result "✗ Found sf namespace usage (not allowed in test project)"
        has_blocking_violations=true
    else
        log_result "✓ No forbidden namespace usage found"
    fi
    
    # Check for proper test structure
    log_result "Checking for proper test structure..."
    local test_files=0
    local test_functions=0
    
    while IFS= read -r -d '' file; do
        test_files=$((test_files + 1))
        local count=$(grep -c -E "(TEST|test_|Test)" "$file" 2>/dev/null || true)
        test_functions=$((test_functions + count))
    done < <(find "${source_roots[@]}" -name "*.cpp" -print0)
    
    log_result "Found ${test_files} test files with ${test_functions} test functions"
    
    # Check for proper assertions
    log_result "Checking for proper assertions..."
    local assertion_count=0
    while IFS= read -r -d '' file; do
        local count=$(grep -c -E "(ASSERT|assert|EXPECT|expect)" "$file" 2>/dev/null || true)
        assertion_count=$((assertion_count + count))
    done < <(find "${source_roots[@]}" -name "*.cpp" -print0)
    
    if [[ ${assertion_count} -gt 0 ]]; then
        log_result "✓ Found ${assertion_count} assertions in test code"
    else
        log_result "⚠ No assertions found in test code"
    fi
    
    log_result ""
    if [[ "${has_blocking_violations}" == "true" ]]; then
        return 1
    fi
    return 0
}

# Main execution
main() {
    local overall_success=true
    
    # Check tools
    if ! check_tools; then
        overall_success=false
    fi
    
    init_source_roots
    
    # Run analyses
    if ! run_clang_analyzer; then
        overall_success=false
    fi
    
    if ! run_cppcheck; then
        overall_success=false
    fi
    
    if ! run_clang_tidy_ownership; then
        overall_success=false
    fi

    if ! run_additional_checks; then
        overall_success=false
    fi
    
    write_static_analysis_summary "log_result" "${overall_success}" "${RESULTS_FILE}" \
        "${HTML_DIR}/index.html" "${ANALYSIS_DIR}/${PROJECT_NAME}_cppcheck_${TIMESTAMP}.xml" \
        "${GREEN}✓ Test project static analysis completed successfully${NC}" \
        "${RED}✗ Test project static analysis found issues${NC}"
    
    return $([[ "${overall_success}" == "true" ]] && echo 0 || echo 1)
}

# Run main function
main "$@"
