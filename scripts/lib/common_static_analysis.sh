#!/bin/bash

# Common Static Analysis Functions
# This script contains shared static analysis checks used by all project analysis scripts

# Resolve clang path, trying versioned binaries when plain clang is unavailable.
locate_clang_binary() {
    local clang_path=""
    clang_path="$(command -v clang 2>/dev/null || true)"
    if [[ -z "${clang_path}" ]]; then
        for candidate in clang-17 clang-16 clang-15 clang-14 clang-13; do
            clang_path="$(command -v "${candidate}" 2>/dev/null || true)"
            if [[ -n "${clang_path}" ]]; then
                break
            fi
        done
    fi

    if [[ -z "${clang_path}" ]]; then
        return 1
    fi

    printf '%s\n' "${clang_path}"
    return 0
}

# Check if required static-analysis tools are installed.
# Parameters:
#   $1: logger function name
#   $2: output variable name for resolved clang path
check_required_static_analysis_tools() {
    local log_result="$1"
    local clang_output_var="$2"
    local tools_missing=()
    local resolved_clang_path=""

    resolved_clang_path="$(locate_clang_binary || true)"
    if [[ -z "${resolved_clang_path}" ]]; then
        tools_missing+=("clang")
    fi

    if ! command -v cppcheck &> /dev/null; then
        tools_missing+=("cppcheck")
    fi
    if ! command -v clang-tidy &> /dev/null; then
        tools_missing+=("clang-tidy")
    fi

    if [[ ${#tools_missing[@]} -gt 0 ]]; then
        "${log_result}" "ERROR: Missing required tools: ${tools_missing[*]}"
        "${log_result}" "Please install: sudo apt-get install clang cppcheck clang-tidy"
        return 1
    fi

    printf -v "${clang_output_var}" '%s' "${resolved_clang_path}"
    return 0
}

# Configure environment for GStreamer/CUDA used by analysis builds.
setup_static_analysis_environment() {
    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
    local openhevc_pkgconfig="${repo_root}/lib/openhevc/lib/pkgconfig"
    local openhevc_lib="${repo_root}/lib/openhevc/lib"

    if [[ -d "${openhevc_pkgconfig}" ]]; then
        export PKG_CONFIG_PATH="${openhevc_pkgconfig}:${PKG_CONFIG_PATH:-}"
    fi

    if [[ -d "${openhevc_lib}" ]]; then
        export LD_LIBRARY_PATH="${openhevc_lib}:/usr/local/cuda/lib64:${LD_LIBRARY_PATH:-}"
    else
        export LD_LIBRARY_PATH="/usr/local/cuda/lib64:${LD_LIBRARY_PATH:-}"
    fi
}

# Run scan-build and stream logs to the provided results file.
# Parameters:
#   $1: output directory
#   $2: clang analyzer path
#   $3: html title
#   $4: results file path
#   $5..: build command
run_scan_build_command() {
    local html_dir="$1"
    local clang_path="$2"
    local html_title="$3"
    local results_file="$4"
    shift 4

    (
        set -o pipefail
        scan-build -o "${html_dir}" --use-analyzer="${clang_path}" \
            --status-bugs \
            --html-title="${html_title}" \
            "$@" 2>&1 | tee -a "${results_file}"
    )
}

# Collect .cpp/.c files from one or more roots into an output array.
# Parameters:
#   $1: output array variable name (nameref)
#   $2..: source roots
collect_c_family_sources() {
    local -n source_files_ref="$1"
    shift

    source_files_ref=()
    local source_root=""
    for source_root in "$@"; do
        while IFS= read -r -d '' file; do
            source_files_ref+=("${file}")
        done < <(find "${source_root}" \( -name "*.cpp" -o -name "*.c" \) -print0)
    done
}

# Collect C/C++ files listed in compile_commands.json and filtered by source roots.
# Parameters:
#   $1: output array variable name (nameref)
#   $2: compile_commands.json path
#   $3..: source roots
collect_compile_db_sources() {
    local -n source_files_ref="$1"
    local compile_db_path="$2"
    shift 2
    local source_roots=("$@")

    source_files_ref=()
    if [[ ! -f "${compile_db_path}" ]]; then
        return 0
    fi

    local source_file=""
    while IFS= read -r source_file; do
        if [[ -n "${source_file}" ]]; then
            source_files_ref+=("${source_file}")
        fi
    done < <(
        python3 - "${compile_db_path}" "${source_roots[@]}" <<'PY'
import json
import pathlib
import sys

compile_db_path = pathlib.Path(sys.argv[1])
source_roots = [pathlib.Path(arg).resolve() for arg in sys.argv[2:]]
c_family_suffixes = {".c", ".cc", ".cpp", ".cxx", ".c++", ".cp", ".cppm", ".ixx"}

def is_under_root(file_path: pathlib.Path, root: pathlib.Path) -> bool:
    return file_path == root or root in file_path.parents

try:
    with compile_db_path.open(encoding="utf-8") as handle:
        entries = json.load(handle)
except Exception:
    sys.exit(0)

seen = set()
for entry in entries:
    if not isinstance(entry, dict):
        continue
    file_field = entry.get("file")
    if not file_field:
        continue

    file_path = pathlib.Path(file_field)
    if not file_path.is_absolute():
        directory = entry.get("directory")
        if directory:
            file_path = pathlib.Path(directory) / file_path
    file_path = file_path.resolve()

    if file_path.suffix.lower() not in c_family_suffixes:
        continue
    if source_roots and not any(is_under_root(file_path, root) for root in source_roots):
        continue

    file_path_str = str(file_path)
    if file_path_str in seen:
        continue
    seen.add(file_path_str)
    print(file_path_str)
PY
    )
}

# Check whether a line has a nearby RAII exemption marker with a non-empty reason.
# Parameters:
#   $1: file path
#   $2: line number
has_raii_exemption_marker() {
    local file="$1"
    local line="$2"
    local start_line=$((line - 2))
    if [[ ${start_line} -lt 1 ]]; then
        start_line=1
    fi
    local end_line=$((line + 2))

    awk -v start_line="${start_line}" -v end_line="${end_line}" '
        NR < start_line || NR > end_line { next }
        /RAII_EXEMPT:[[:space:]]*[^[:space:]].*/ { found = 1 }
        END { exit(found ? 0 : 1) }
    ' "${file}"
}

# Check for ownership-contract violations:
#  - returning ptr.release()
#  - factory-like raw-pointer signatures (create_/make_/alloc_/clone_)
check_ownership_contracts() {
    local project_root="$1"
    local log_result="$2"
    resolve_analysis_roots "${project_root}"

    log_result "Checking ownership-contract violations..."
    local ownership_violations=0
    local exempted_violations=0
    local file=""
    while IFS= read -r -d '' file; do
        while IFS=$'\t' read -r match_type line_number source_line; do
            if [[ -z "${match_type}" ]]; then
                continue
            fi

            if has_raii_exemption_marker "${file}" "${line_number}"; then
                exempted_violations=$((exempted_violations + 1))
                continue
            fi

            log_result "${file}:${line_number}: ${match_type}: ${source_line}"
            ownership_violations=$((ownership_violations + 1))
        done < <(
            awk '
                BEGIN {
                    in_block_comment = 0
                }
                {
                    raw_line = $0
                    code = $0

                    if (in_block_comment == 1) {
                        if (match(code, /\*\//)) {
                            code = substr(code, RSTART + RLENGTH)
                            in_block_comment = 0
                        } else {
                            next
                        }
                    }
                    while (match(code, /\/\*/)) {
                        prefix = substr(code, 1, RSTART - 1)
                        suffix = substr(code, RSTART + RLENGTH)
                        if (match(suffix, /\*\//)) {
                            code = prefix substr(suffix, RSTART + RLENGTH)
                        } else {
                            code = prefix
                            in_block_comment = 1
                            break
                        }
                    }

                    sub(/\/\/.*/, "", code)
                    gsub(/"([^"\\]|\\.)*"/, "", code)

                    if (code ~ /return[[:space:]]+[^;]*\.release[[:space:]]*\([[:space:]]*\)[[:space:]]*;/) {
                        printf "release_return\t%d\t%s\n", NR, raw_line
                    }

                    if (code ~ /^[[:space:]]*[^=;]*\*[[:space:]]*(create|make|alloc|clone)_[A-Za-z0-9_]*[[:space:]]*\([^;]*\)[[:space:]]*(const[[:space:]]*)?(noexcept[[:space:]]*)?[{;][[:space:]]*$/) {
                        printf "factory_raw_pointer_api\t%d\t%s\n", NR, raw_line
                    }
                }
            ' "${file}"
        )
    done < <(find "${analysis_roots[@]}" \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0)

    if [[ ${exempted_violations} -gt 0 ]]; then
        log_result "✓ Accepted ${exempted_violations} ownership cases via RAII_EXEMPT markers"
    fi

    if [[ ${ownership_violations} -gt 0 ]]; then
        log_result "✗ Found ${ownership_violations} ownership-contract violations"
        return 1
    fi

    log_result "✓ No ownership-contract violations found"
    return 0
}

# Write a standard static-analysis summary section.
# Parameters:
#   $1: logger function name
#   $2: overall_success (true/false)
#   $3: results file path
#   $4: html report path
#   $5: xml report path
#   $6: console success message
#   $7: console failure message
write_static_analysis_summary() {
    local log_result="$1"
    local overall_success="$2"
    local results_file="$3"
    local html_report="$4"
    local xml_report="$5"
    local success_message="$6"
    local failure_message="$7"

    "${log_result}" "========================================="
    "${log_result}" "Analysis Summary"
    "${log_result}" "========================================="

    if [[ "${overall_success}" == "true" ]]; then
        "${log_result}" "✓ All static analysis checks passed!"
        "${log_result}" "Status: SUCCESS"
    else
        "${log_result}" "✗ Static analysis found issues"
        "${log_result}" "Status: FAILURE"
    fi

    "${log_result}" ""
    "${log_result}" "Results saved to: ${results_file}"
    if [[ -f "${html_report}" ]]; then
        "${log_result}" "HTML report: ${html_report}"
    else
        "${log_result}" "HTML report: not generated (scan-build found no bugs)"
    fi
    "${log_result}" "XML report: ${xml_report}"

    if [[ "${overall_success}" == "true" ]]; then
        printf '%b\n' "${success_message}"
    else
        printf '%b\n' "${failure_message}"
    fi
}

# Resolve analysis roots for a project.
# Prefers src/include when present; falls back to project root.
resolve_analysis_roots() {
    local project_root="$1"
    analysis_roots=()

    if [[ -d "${project_root}/src" ]]; then
        analysis_roots+=("${project_root}/src")
    fi
    if [[ -d "${project_root}/include" ]]; then
        analysis_roots+=("${project_root}/include")
    fi
    if [[ ${#analysis_roots[@]} -eq 0 ]]; then
        analysis_roots+=("${project_root}")
    fi
}

# Function to check for forbidden naming patterns (leading/trailing underscores)
check_forbidden_naming_patterns() {
    local project_root="$1"
    local log_result="$2"
    resolve_analysis_roots "${project_root}"
    
    log_result "Checking for forbidden naming patterns..."
    local forbidden_patterns=(
        "^[[:space:]]*_[a-zA-Z]"  # Leading underscore at start of line
        "[a-zA-Z]_[[:space:]]*$"  # Trailing underscore at end of line
    )
    
    local violations_found=false
    for pattern in "${forbidden_patterns[@]}"; do
        if grep -r -n --include="*.cpp" --include="*.h" --include="*.c" \
           -E "${pattern}" "${analysis_roots[@]}" 2>/dev/null; then
            log_result "✗ Found forbidden naming pattern: ${pattern}"
            violations_found=true
        fi
    done
    
    if [[ "${violations_found}" == "false" ]]; then
        log_result "✓ No forbidden naming patterns found"
    fi
}

# Function to check for old-style casts
check_old_style_casts() {
    local project_root="$1"
    local log_result="$2"
    resolve_analysis_roots "${project_root}"
    
    log_result "Checking for old-style casts..."
    if grep -r -n --include="*.cpp" --include="*.c" \
       -E "\\([^)]*\\)" "${analysis_roots[@]}" 2>/dev/null | \
       grep -v "//" | grep -v "/*" | grep -v "*/"; then
        log_result "✗ Found potential old-style casts"
    else
        log_result "✓ No old-style casts found"
    fi
}

# Function to check for NULL usage
check_null_usage() {
    local project_root="$1"
    local log_result="$2"
    resolve_analysis_roots "${project_root}"
    
    log_result "Checking for NULL usage..."
    local null_matches=()
    while IFS= read -r -d '' file; do
        while IFS= read -r match_line; do
            null_matches+=("${match_line}")
        done < <(
            awk '
                BEGIN {
                    in_block_comment = 0
                }
                {
                    code = $0
                    if (in_block_comment == 1) {
                        if (match(code, /\*\//)) {
                            code = substr(code, RSTART + RLENGTH)
                            in_block_comment = 0
                        } else {
                            next
                        }
                    }
                    while (match(code, /\/\*/)) {
                        prefix = substr(code, 1, RSTART - 1)
                        suffix = substr(code, RSTART + RLENGTH)
                        if (match(suffix, /\*\//)) {
                            code = prefix substr(suffix, RSTART + RLENGTH)
                        } else {
                            code = prefix
                            in_block_comment = 1
                            break
                        }
                    }
                    sub(/\/\/.*/, "", code)
                    gsub(/"([^"\\]|\\.)*"/, "", code)
                    if (code ~ /(^|[^A-Za-z0-9_])NULL([^A-Za-z0-9_]|$)/) {
                        printf "%s:%d:%s\n", FILENAME, NR, $0
                    }
                }
            ' "$file"
        )
    done < <(find "${analysis_roots[@]}" \( -name "*.cpp" -o -name "*.c" -o -name "*.h" -o -name "*.hpp" \) -print0)

    if [[ ${#null_matches[@]} -gt 0 ]]; then
        for match_line in "${null_matches[@]}"; do
            log_result "${match_line}"
        done
        log_result "✗ Found NULL usage in code tokens (should use nullptr)"
    else
        log_result "✓ No NULL usage found"
    fi
}

# Function to check for missing [[nodiscard]] on functions that return values
check_missing_nodiscard() {
    local project_root="$1"
    local log_result="$2"
    resolve_analysis_roots "${project_root}"
    
    log_result "Checking for missing [[nodiscard]]..."
    local functions_without_nodiscard=0
    local functions_needing_nodiscard=()
    
    # Find functions that return values but don't have [[nodiscard]]
    while IFS= read -r -d '' file; do
        # Look for function definitions that return non-void types
        while IFS= read -r line; do
            # Skip lines that already have [[nodiscard]]
            if echo "$line" | grep -q "\[\[nodiscard\]\]"; then
                continue
            fi
            
            # Check for functions that return values (not void)
            if echo "$line" | grep -qE "^(\\s*)(static\\s+)?(const\\s+)?(bool|int|double|float|std::\\w+|\\w+\\*|\\w+&)\\s+\\w+\\s*\\([^)]*\\)\\s*\\{"; then
                local line_num=$(echo "$line" | cut -d: -f2)
                local func_name=$(echo "$line" | grep -oE "\\w+\\s*\\([^)]*\\)" | head -1 | cut -d'(' -f1 | tr -d ' ')
                functions_needing_nodiscard+=("$file:$line_num:$func_name")
                functions_without_nodiscard=$((functions_without_nodiscard + 1))
            fi
        done < <(grep -n -E "^(\\s*)(static\\s+)?(const\\s+)?(bool|int|double|float|std::\\w+|\\w+\\*|\\w+&)\\s+\\w+\\s*\\([^)]*\\)\\s*\\{" "$file" 2>/dev/null || true)
    done < <(find "${analysis_roots[@]}" -name "*.cpp" -print0)
    
    if [[ ${functions_without_nodiscard} -gt 0 ]]; then
        log_result "⚠ Found ${functions_without_nodiscard} functions that need [[nodiscard]]:"
        for func in "${functions_needing_nodiscard[@]}"; do
            log_result "  - $func"
        done
    else
        log_result "✓ All functions that return values have [[nodiscard]]"
    fi
}

# Function to check for potential memory leaks (raw new/delete)
check_memory_leaks() {
    local project_root="$1"
    local log_result="$2"
    resolve_analysis_roots "${project_root}"
    
    log_result "Checking for potential memory leaks (high-confidence patterns)..."
    local owning_raw_new_matches=()
    local ownership_violations=0
    local exempted_violations=0
    while IFS= read -r -d '' file; do
        while IFS=$'\t' read -r line_number source_line; do
            if [[ -z "${line_number}" ]]; then
                continue
            fi
            if has_raii_exemption_marker "${file}" "${line_number}"; then
                exempted_violations=$((exempted_violations + 1))
                continue
            fi
            owning_raw_new_matches+=("${file}:${line_number}:${source_line}")
            ownership_violations=$((ownership_violations + 1))
        done < <(
            awk '
                {
                    code = $0
                    sub(/\/\/.*/, "", code)
                    gsub(/"([^"\\]|\\.)*"/, "", code)
                    if (code ~ /[A-Za-z_][A-Za-z0-9_:<>]*[[:space:]]*\\*[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=[[:space:]]*new[[:space:]]+/) {
                        printf "%d\t%s\n", NR, $0
                    }
                }
            ' "$file"
        )
    done < <(find "${analysis_roots[@]}" -name "*.cpp" -print0)
    
    if [[ ${exempted_violations} -gt 0 ]]; then
        log_result "✓ Accepted ${exempted_violations} owning raw allocation cases via RAII_EXEMPT markers"
    fi

    if [[ ${ownership_violations} -gt 0 ]]; then
        for match_line in "${owning_raw_new_matches[@]}"; do
            log_result "${match_line}"
        done
        log_result "✗ Found ${ownership_violations} owning raw-pointer allocations without valid RAII_EXEMPT reasons"
        return 1
    fi

    log_result "✓ No high-confidence raw owning allocations found"
    return 0
}

# Function to check for potential buffer overflows
check_buffer_overflows() {
    local project_root="$1"
    local log_result="$2"
    resolve_analysis_roots "${project_root}"
    
    log_result "Checking for potential buffer overflow patterns..."
    local buffer_issues=0
    while IFS= read -r -d '' file; do
        local count=$(grep -c -E "(strcpy|strcat|sprintf|gets)" "$file" 2>/dev/null || echo "0")
        if [[ "$count" =~ ^[0-9]+$ ]]; then
            buffer_issues=$((buffer_issues + count))
        fi
    done < <(find "${analysis_roots[@]}" -name "*.cpp" -o -name "*.c" -print0)
    
    if [[ ${buffer_issues} -gt 0 ]]; then
        log_result "⚠ Found ${buffer_issues} potentially unsafe string operations"
    else
        log_result "✓ No unsafe string operations found"
    fi
}

# Function to check for potential integer overflow
check_integer_overflow() {
    local project_root="$1"
    local log_result="$2"
    # project_root is intentionally unused by this check; arithmetic safety is covered by compiler flags + cppcheck.
    # shellcheck disable=SC2034
    local _unused_project_root="${project_root}"
    
    log_result "Checking for potential integer overflow patterns..."
    log_result "✓ Heuristic overflow scan skipped; rely on -Wconversion/-Wsign-conversion and cppcheck diagnostics"
}

# Function to check for potential dead code
check_dead_code() {
    local project_root="$1"
    local log_result="$2"
    # project_root is intentionally unused by this check; dead code findings should come from analyzers, not regex heuristics.
    # shellcheck disable=SC2034
    local _unused_project_root="${project_root}"
    
    log_result "Checking for potential dead code..."
    log_result "✓ Heuristic dead-code scan skipped; rely on clang static analyzer/cppcheck for actionable findings"
}

# Function to check for potential infinite loops
check_infinite_loops() {
    local project_root="$1"
    local log_result="$2"
    # project_root is intentionally unused by this check; tight loops can be intentional in media pipelines.
    # shellcheck disable=SC2034
    local _unused_project_root="${project_root}"
    
    log_result "Checking for potential infinite loops..."
    log_result "✓ Heuristic infinite-loop scan skipped; rely on static analyzers and targeted runtime tests"
}

# Function to check for proper header guards (#pragma once)
check_header_guards() {
    local project_root="$1"
    local log_result="$2"
    resolve_analysis_roots "${project_root}"
    
    log_result "Checking for proper header guards..."
    local header_guard_violations=0
    while IFS= read -r -d '' file; do
        if ! head -1 "$file" | grep -q "#pragma once"; then
            log_result "⚠ Header $file should use #pragma once"
            header_guard_violations=$((header_guard_violations + 1))
        fi
    done < <(find "${analysis_roots[@]}" -name "*.h" -print0)
    
    if [[ ${header_guard_violations} -eq 0 ]]; then
        log_result "✓ All headers use #pragma once"
    fi
}

# Function to check for proper RAII usage
check_raii_compliance() {
    local project_root="$1"
    local log_result="$2"
    # project_root is intentionally unused by this check because concrete ownership checks are performed in
    # check_memory_leaks().
    # shellcheck disable=SC2034
    local _unused_project_root="${project_root}"
    
    log_result "Checking for RAII compliance..."
    log_result "✓ RAII ownership warnings are emitted only from the high-confidence memory-leak check"
}

# Function to run all common additional checks
run_common_additional_checks() {
    local project_root="$1"
    local log_result="$2"
    local has_blocking_violations=false
    
    log_result "Running Additional Checks..."
    log_result "---------------------------"
    
    check_forbidden_naming_patterns "$project_root" "$log_result"
    check_old_style_casts "$project_root" "$log_result"
    check_null_usage "$project_root" "$log_result"
    check_missing_nodiscard "$project_root" "$log_result"
    if ! check_ownership_contracts "$project_root" "$log_result"; then
        has_blocking_violations=true
    fi
    if ! check_memory_leaks "$project_root" "$log_result"; then
        has_blocking_violations=true
    fi
    check_buffer_overflows "$project_root" "$log_result"
    check_integer_overflow "$project_root" "$log_result"
    check_dead_code "$project_root" "$log_result"
    check_infinite_loops "$project_root" "$log_result"
    check_header_guards "$project_root" "$log_result"
    check_raii_compliance "$project_root" "$log_result"
    
    log_result ""
    if [[ "${has_blocking_violations}" == "true" ]]; then
        return 1
    fi
    return 0
}

# Run clang-tidy ownership checks using a compile_commands.json directory.
# Parameters:
#   $1: logger function name
#   $2: build directory containing compile_commands.json
#   $3..: source roots
run_clang_tidy_ownership_checks() {
    local log_result="$1"
    local compile_db_dir="$2"
    shift 2
    local source_roots=("$@")

    "${log_result}" "Running clang-tidy ownership checks..."
    "${log_result}" "-----------------------------------"

    if [[ ! -f "${compile_db_dir}/compile_commands.json" ]]; then
        "${log_result}" "Skipping clang-tidy ownership checks: compile_commands.json not found in ${compile_db_dir}"
        "${log_result}" ""
        return 0
    fi

    if ! command -v clang-tidy &> /dev/null; then
        "${log_result}" "✗ clang-tidy is not installed; ownership checks cannot run"
        "${log_result}" ""
        return 1
    fi

    local source_files=()
    collect_compile_db_sources source_files "${compile_db_dir}/compile_commands.json" "${source_roots[@]}"
    if [[ ${#source_files[@]} -eq 0 ]]; then
        "${log_result}" "No compile_db-mapped source files found for clang-tidy ownership checks"
        "${log_result}" ""
        return 0
    fi

    "${log_result}" "Using compile database: ${compile_db_dir}/compile_commands.json"
    "${log_result}" "Analyzing ${#source_files[@]} source files with clang-tidy..."

    local checks="-*,cppcoreguidelines-owning-memory"
    local violation_count=0
    local exempted_count=0
    local non_ownership_diagnostic_count=0
    local source_file=""

    while IFS= read -r source_file; do
        local tidy_output=""
        tidy_output="$(clang-tidy -p "${compile_db_dir}" -checks="${checks}" "${source_file}" -- 2>&1 || true)"
        while IFS= read -r diagnostic_line; do
            if [[ "${diagnostic_line}" =~ ^(/[^:]+):([0-9]+):([0-9]+):[[:space:]](warning|error):[[:space:]](.*)$ ]]; then
                local diagnostic_file="${BASH_REMATCH[1]}"
                local diagnostic_line_number="${BASH_REMATCH[2]}"
                local diagnostic_message="${BASH_REMATCH[5]}"
                local diagnostic_check_name=""

                if [[ "${diagnostic_message}" =~ \[([A-Za-z0-9_.-]+)\][[:space:]]*$ ]]; then
                    diagnostic_check_name="${BASH_REMATCH[1]}"
                fi

                if [[ "${diagnostic_check_name}" == "cppcoreguidelines-owning-memory" ]]; then
                    if has_raii_exemption_marker "${diagnostic_file}" "${diagnostic_line_number}"; then
                        exempted_count=$((exempted_count + 1))
                        continue
                    fi
                    "${log_result}" "${diagnostic_line}"
                    violation_count=$((violation_count + 1))
                    continue
                fi

                "${log_result}" "non_ownership_diagnostic: ${diagnostic_line}"
                non_ownership_diagnostic_count=$((non_ownership_diagnostic_count + 1))
            fi
        done <<< "${tidy_output}"
    done < <(printf "%s\n" "${source_files[@]}")

    if [[ ${exempted_count} -gt 0 ]]; then
        "${log_result}" "✓ Accepted ${exempted_count} clang-tidy ownership diagnostics via RAII_EXEMPT markers"
    fi

    if [[ ${violation_count} -gt 0 ]]; then
        "${log_result}" "✗ Found ${violation_count} clang-tidy ownership violations"
        "${log_result}" ""
        return 1
    fi

    if [[ ${non_ownership_diagnostic_count} -gt 0 ]]; then
        "${log_result}" "⚠ Reported ${non_ownership_diagnostic_count} non-ownership clang diagnostics (non-blocking)"
    fi

    "${log_result}" "✓ No clang-tidy ownership violations found"
    "${log_result}" ""
    return 0
}