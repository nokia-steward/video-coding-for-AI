#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI Decoder Static Analysis Script
# Runs Clang Static Analyzer and Cppcheck on the plugin C++ sources

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
PROJECT_NAME="vcai_decoder"
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
echo -e "${BLUE}VCAI Decoder Static Analysis${NC}"
echo -e "${BLUE}========================================${NC}"

# Function to log results
log_result() {
    echo "$1" | tee -a "${RESULTS_FILE}"
}

log_result "VCAI Decoder Static Analysis Report"
log_result "=========================================="
log_result "Timestamp: ${TIMESTAMP}"
log_result "Project: ${PROJECT_NAME}"
log_result "Source directory: ${PROJECT_ROOT}/src"
log_result ""

# Check if required tools are available
check_tools() {
    check_required_static_analysis_tools "log_result" "clang_path"
}

# Run Clang Static Analyzer
run_clang_analyzer() {
    log_result "Running Clang Static Analyzer..."
    log_result "--------------------------------"

    # Set up analysis environment
    setup_static_analysis_environment

    cd "${PROJECT_ROOT}"

    if [[ ! -d "${PROJECT_ROOT}/../build/vcai_decoder" ]]; then
        log_result "Build directory not found. Please run ./build.sh first."
        return 1
    fi

    log_result "Using clang analyzer: ${clang_path}"
    if run_scan_build_command "${HTML_DIR}" "${clang_path}" "VCAI Decoder Static Analysis" "${RESULTS_FILE}" \
        cmake --build "${PROJECT_ROOT}/../build/vcai_decoder" --target gstvcai_decoder; then
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

    local source_files=()
    collect_c_family_sources source_files "${PROJECT_ROOT}/src" "${PROJECT_ROOT}/test"

    if [[ ${#source_files[@]} -eq 0 ]]; then
        log_result "No source files found in ${PROJECT_ROOT}/src or ${PROJECT_ROOT}/test"
        return 1
    fi

    log_result "Analyzing ${#source_files[@]} source files..."
    log_result "Files: ${source_files[*]}"
    log_result ""

    local cppcheck_args=(
        "--enable=all"
        "--inconclusive"
        "--std=c++20"
        "--verbose"
        "--error-exitcode=1"
        "--suppress=missingIncludeSystem"
        "--suppress=unmatchedSuppression"
        "--include=${PROJECT_ROOT}/src"
        "--include=/usr/include/gstreamer-1.0"
        "--include=/usr/include/glib-2.0"
        "--include=/usr/lib/x86_64-linux-gnu/glib-2.0/include"
        "--xml"
        "--xml-version=2"
        "--output-file=${ANALYSIS_DIR}/${PROJECT_NAME}_cppcheck_${TIMESTAMP}.xml"
        "--platform=unix64"
    )

    if cppcheck "${cppcheck_args[@]}" "${source_files[@]}" 2>&1 | tee -a "${RESULTS_FILE}"; then
        log_result "✓ Cppcheck completed successfully"
    else
        log_result "✗ Cppcheck found issues"
        return 1
    fi

    log_result ""
}

# Run clang-tidy ownership checks.
# Use a normalized compile_commands.json so "directory" is the build root; otherwise clang-tidy
# fails to resolve includes (e.g. "ops/sf_tet_tiles.h") when directory is build/vcai_decoder/vcai_decoder.
run_clang_tidy_ownership() {
    local build_dir="${PROJECT_ROOT}/../build/vcai_decoder"
    local build_dir_abs
    build_dir_abs="$(cd "${build_dir}" 2>/dev/null && pwd)" || build_dir_abs="${build_dir}"
    local compile_db="${build_dir}/compile_commands.json"
    if [[ ! -f "${compile_db}" ]]; then
        run_clang_tidy_ownership_checks "log_result" "${build_dir}" "${PROJECT_ROOT}/src"
        return $?
    fi
    local tidy_db_dir="${ANALYSIS_DIR}/clang_tidy_db_${TIMESTAMP}"
    mkdir -p "${tidy_db_dir}"
    python3 - "${compile_db}" "${build_dir_abs}" "${tidy_db_dir}" <<'NORM_PY'
import json
import sys
p = sys.argv[1]
build_root = sys.argv[2]
out_dir = sys.argv[3]
with open(p, encoding="utf-8") as f:
    entries = json.load(f)
for e in entries:
    if isinstance(e, dict) and "directory" in e:
        e["directory"] = build_root
with open(out_dir + "/compile_commands.json", "w", encoding="utf-8") as f:
    json.dump(entries, f, indent=1)
NORM_PY
    run_clang_tidy_ownership_checks "log_result" "${tidy_db_dir}" "${PROJECT_ROOT}/src"
    local ret=$?
    rm -rf "${tidy_db_dir}"
    return ${ret}
}

# Run additional checks
run_additional_checks() {
    local has_blocking_violations=false
    if ! run_common_additional_checks "${PROJECT_ROOT}" "log_result"; then
        has_blocking_violations=true
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

    if ! check_tools; then
        overall_success=false
    fi

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
        "${GREEN}✓ VCAI Decoder static analysis completed successfully${NC}" \
        "${RED}✗ VCAI Decoder static analysis found issues${NC}"

    return $([[ "${overall_success}" == "true" ]] && echo 0 || echo 1)
}

main "$@"
