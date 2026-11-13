#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# LibVCAI Static Analysis Script
# Runs Clang Static Analyzer and Cppcheck with library-specific configurations

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
PROJECT_NAME="libvcai"
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
build_target=""

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
echo -e "${BLUE}LibVCAI Static Analysis${NC}"
echo -e "${BLUE}========================================${NC}"

# Function to log results
log_result() {
    echo "$1" | tee -a "${RESULTS_FILE}"
}

log_result "LibVCAI Static Analysis Report"
log_result "===================================="
log_result "Timestamp: ${TIMESTAMP}"
log_result "Project: ${PROJECT_NAME}"
log_result "Source directory: ${PROJECT_ROOT}/src"
log_result "Include directory: ${PROJECT_ROOT}/include"
log_result ""

# Check if required tools are available
check_tools() {
    check_required_static_analysis_tools "log_result" "clang_path"
}

# Select a build directory and target that include libvcai sources.
select_build_target() {
    local candidates=(
        "${PROJECT_ROOT}/../build/encoder_cli:encoder_cli_vcai"
        "${PROJECT_ROOT}/../build/rtp_server:rtp_server_vcai"
        "${PROJECT_ROOT}/../test/test_build:test_encoder"
        "${PROJECT_ROOT}/../build/test:test_encoder"
        "${PROJECT_ROOT}/../build:test_encoder"
    )
    for entry in "${candidates[@]}"; do
        local candidate_dir="${entry%%:*}"
        local candidate_target="${entry##*:}"
        if [[ -d "${candidate_dir}" ]]; then
            build_dir="${candidate_dir}"
            build_target="${candidate_target}"
            return 0
        fi
    done
    return 1
}

# Run Clang Static Analyzer
run_clang_analyzer() {
    log_result "Running Clang Static Analyzer..."
    log_result "--------------------------------"
    
    # Set up analysis environment
    setup_static_analysis_environment
    
    # Build with scan-build
    cd "${PROJECT_ROOT}"
    
    if ! select_build_target; then
        log_result "Build directory not found. Please run encoder_cli/build.sh, rtp_server/build.sh, or test/run.sh first."
        return 1
    fi
    
    log_result "Using clang analyzer: ${clang_path}"
    log_result "Using build directory: ${build_dir}"
    log_result "Using build target: ${build_target}"
    if run_scan_build_command "${HTML_DIR}" "${clang_path}" "LibVCAI Static Analysis" "${RESULTS_FILE}" \
        cmake --build "${build_dir}" --target "${build_target}"; then
        log_result "✓ Clang Static Analyzer completed successfully"
        if [[ -f "${HTML_DIR}/index.html" ]]; then
            log_result "HTML report: ${HTML_DIR}/index.html"
        else
            log_result "HTML report: not generated (scan-build found no bugs)"
        fi
    else
        log_result "✗ Clang Static Analyzer found issues"
        return 1
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
        "--include=${PROJECT_ROOT}/src"
        "--include=${PROJECT_ROOT}/include"
        "--include=/home/research/Nokia/VCAI/lib/install/include"
        "--include=/usr/include/gstreamer-1.0"
        "--include=/usr/include/glib-2.0"
        "--include=/usr/lib/x86_64-linux-gnu/glib-2.0/include"
        "--xml"
        "--xml-version=2"
        "--output-file=${ANALYSIS_DIR}/${PROJECT_NAME}_cppcheck_${TIMESTAMP}.xml"
        "--check-config"
        "--platform=unix64"
    )
    
    # Add source files
    local source_files=()
    collect_c_family_sources source_files "${PROJECT_ROOT}/src"
    
    if [[ ${#source_files[@]} -eq 0 ]]; then
        log_result "No source files found in ${PROJECT_ROOT}/src"
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
    if [[ -z "${build_dir}" || -z "${build_target}" ]]; then
        if ! select_build_target; then
            log_result "Build directory not found for clang-tidy. Please run encoder_cli/build.sh, rtp_server/build.sh, or test/run.sh first."
            return 1
        fi
    fi
    run_clang_tidy_ownership_checks "log_result" "${build_dir}" "${PROJECT_ROOT}/src" "${PROJECT_ROOT}/include"
}

# Run additional checks
run_additional_checks() {
    local has_blocking_violations=false
    if ! run_common_additional_checks "${PROJECT_ROOT}" "log_result"; then
        has_blocking_violations=true
    fi

    # Check for proper namespace usage (libvcai should use sf namespace)
    log_result "Checking for proper namespace usage..."
    local namespace_violations=0
    
    # Check that public headers use sf namespace
    while IFS= read -r -d '' file; do
        if ! grep -q "namespace sf" "$file"; then
            log_result "⚠ Public header $file should be in sf namespace"
            namespace_violations=$((namespace_violations + 1))
        fi
    done < <(find "${PROJECT_ROOT}/include" -name "*.h" -print0)
    
    if [[ ${namespace_violations} -eq 0 ]]; then
        log_result "✓ All public headers properly use sf namespace"
    fi
    
    # Check for using namespace statements (forbidden)
    log_result "Checking for forbidden using namespace statements..."
    if grep -r -n --include="*.cpp" --include="*.h" \
       -E "using\\s+namespace" "${PROJECT_ROOT}/src" "${PROJECT_ROOT}/include" 2>/dev/null; then
        log_result "✗ Found using namespace statements (forbidden)"
        has_blocking_violations=true
    else
        log_result "✓ No forbidden using namespace statements found"
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
        "${GREEN}✓ LibVCAI static analysis completed successfully${NC}" \
        "${RED}✗ LibVCAI static analysis found issues${NC}"
    
    return $([[ "${overall_success}" == "true" ]] && echo 0 || echo 1)
}

# Run main function
main "$@"
