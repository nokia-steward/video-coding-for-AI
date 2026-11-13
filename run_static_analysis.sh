#!/bin/bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

# VCAI Static Analysis Runner
# Runs static analysis on all subprojects using Clang Static Analyzer and Cppcheck

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
ANALYSIS_DIR="${PROJECT_ROOT}/static_analysis_results"
RESULTS_FILE="${ANALYSIS_DIR}/analysis_summary_${TIMESTAMP}.txt"

# Create results directory
mkdir -p "${ANALYSIS_DIR}"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}VCAI Static Analysis Runner${NC}"
echo -e "${BLUE}========================================${NC}"
echo "Timestamp: ${TIMESTAMP}"
echo "Results directory: ${ANALYSIS_DIR}"
echo ""

# Function to run analysis on a subproject
run_project_analysis() {
    local project_name="$1"
    local project_path="${PROJECT_ROOT}/${project_name}"
    
    if [[ ! -d "${project_path}" ]]; then
        echo -e "${YELLOW}Warning: Project ${project_name} not found, skipping...${NC}"
        return 0
    fi
    
    local script_path="${project_path}/run_static_analysis.sh"
    
    if [[ ! -f "${script_path}" ]]; then
        echo -e "${YELLOW}Warning: Static analysis script not found for ${project_name}, skipping...${NC}"
        return 0
    fi
    
    echo -e "${BLUE}Running static analysis for ${project_name}...${NC}"
    echo "----------------------------------------"
    
    if bash "${script_path}" "${ANALYSIS_DIR}" "${TIMESTAMP}"; then
        echo -e "${GREEN}✓ ${project_name} analysis completed successfully${NC}"
        echo ""
        return 0
    else
        echo -e "${RED}✗ ${project_name} analysis failed${NC}"
        echo ""
        return 1
    fi
}

# Track overall results
OVERALL_SUCCESS=true
FAILED_PROJECTS=()

# Run analysis on all subprojects
PROJECTS=("libvcai" "encoder_cli" "rtp_server" "vcai_decoder" "test")

for project in "${PROJECTS[@]}"; do
    if ! run_project_analysis "${project}"; then
        OVERALL_SUCCESS=false
        FAILED_PROJECTS+=("${project}")
    fi
done

# Generate summary report
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Analysis Summary${NC}"
echo -e "${BLUE}========================================${NC}"

{
    echo "VCAI Static Analysis Summary"
    echo "=================================="
    echo "Timestamp: ${TIMESTAMP}"
    echo "Analysis directory: ${ANALYSIS_DIR}"
    echo ""
    
    if [[ "${OVERALL_SUCCESS}" == "true" ]]; then
        echo "Overall Status: SUCCESS"
        echo "All projects passed static analysis."
    else
        echo "Overall Status: FAILURE"
        echo "Failed projects: ${FAILED_PROJECTS[*]}"
    fi
    
    echo ""
    echo "Detailed results available in:"
    for project in "${PROJECTS[@]}"; do
        project_file="${ANALYSIS_DIR}/${project}_analysis_${TIMESTAMP}.txt"
        if [[ -f "${project_file}" ]]; then
            echo "  - ${project}: ${project_file}"
        fi
    done
    
    echo ""
    echo "HTML reports available in:"
    local_html_count=0
    for project in "${PROJECTS[@]}"; do
        html_dir="${ANALYSIS_DIR}/${project}_html_${TIMESTAMP}"
        if [[ -f "${html_dir}/index.html" ]]; then
            echo "  - ${project}: ${html_dir}/index.html"
            local_html_count=$((local_html_count + 1))
        fi
    done
    if [[ ${local_html_count} -eq 0 ]]; then
        echo "  - none (scan-build found no bugs, so no HTML reports were generated)"
    fi
    
} > "${RESULTS_FILE}"

# Display summary
cat "${RESULTS_FILE}"

# Exit with appropriate code
if [[ "${OVERALL_SUCCESS}" == "true" ]]; then
    echo -e "${GREEN}All static analysis checks passed!${NC}"
    exit 0
else
    echo -e "${RED}Static analysis failed for: ${FAILED_PROJECTS[*]}${NC}"
    exit 1
fi
