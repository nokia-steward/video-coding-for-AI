# Chapter 8 — Static analysis and workflow

This chapter explains the required analysis workflow after C++ changes and how to interpret analysis outputs.
Use it when you need a practical command sequence, not just script reference details.

## When to use this chapter

Use this chapter when you:

- changed C++ in `encoder_cli`, `libvcai`, `rtp_server`, or `test`
- need to run project static analysis before handing off or merging work
- need to understand which findings are blocking vs informational
- need quick fixes for common analysis failures

Reference scripts:

- per-project scripts:
  - [**encoder_cli/run_static_analysis.sh**](../../encoder_cli/run_static_analysis.sh)
  - [**libvcai/run_static_analysis.sh**](../../libvcai/run_static_analysis.sh)
  - [**rtp_server/run_static_analysis.sh**](../../rtp_server/run_static_analysis.sh)
  - [**test/run_static_analysis.sh**](../../test/run_static_analysis.sh)
- full-repo wrapper:
  - [**run_static_analysis.sh**](../../run_static_analysis.sh)
- shared checks implementation:
  - [**scripts/lib/common_static_analysis.sh**](../../scripts/lib/common_static_analysis.sh)

## Required order of operations

Use this order of operations:

1. Run `clang-format` on every modified `.cpp` and `.h`.
2. Run the subproject `build.sh` for the area you changed.
3. Run that subproject's `run_static_analysis.sh`.
4. Run [**test/run.sh**](../../test/run.sh) when tests changed.

Do not skip build before analysis. Analysis scripts depend on build outputs such as build directories and, for some
checks, `compile_commands.json`.

## Command cookbook

### Analyze a single subproject

Encoder CLI (from repository root):

```bash
cd encoder_cli
./build.sh
./run_static_analysis.sh
```

`libvcai` (build a consumer first so compile DB exists):

```bash
cd ../encoder_cli
./build.sh
cd ../libvcai
./run_static_analysis.sh
```

RTP server (from repository root):

```bash
cd ../rtp_server
./build.sh
./run_static_analysis.sh
```

Test project (from repository root):

```bash
cd ../test
./run.sh
./run_static_analysis.sh
```

### Analyze all projects from repository root

```bash
cd /home/research/Nokia/VCAI
./run_static_analysis.sh
```

The root wrapper calls each subproject analysis script and aggregates results.

### Script parameter reference (authoritative includes)

Encoder CLI parameters (included from `encoder_cli/README.md`):

--8<-- "encoder_cli/README.md:111:122"

`libvcai` parameters (included from `libvcai/README.md`):

--8<-- "libvcai/README.md:181:188"

## How to read results

By default, outputs are written under `static_analysis_results/` at repository root.

Typical artifacts:

- `*_analysis_<timestamp>.txt` - combined text log with pass/fail summary
- `*_cppcheck_<timestamp>.xml` - Cppcheck XML output
- `*_html_<timestamp>/index.html` - scan-build HTML report

Important: `index.html` can be absent when scan-build reports no bugs. That is expected and not a failure by itself.

## Blocking vs informational checks

Shared behavior comes from [**scripts/lib/common_static_analysis.sh**](../../scripts/lib/common_static_analysis.sh).

Blocking checks (must be fixed or explicitly exempted where supported):

- ownership-contract violations (for example `return ptr.release();`)
- factory-like raw-pointer ownership APIs (`create_*`, `make_*`, `alloc_*`, `clone_*`) without valid exemption context
- owning raw allocations assigned to raw pointers without valid exemption context
- `clang-tidy` ownership violations from `cppcoreguidelines-owning-memory`

Allowed exemptions:

- inline marker with non-empty reason:
  `RAII_EXEMPT: <why raw ownership is unavoidable here>`
- intended only for unavoidable C-API/callback ownership boundaries

Informational or non-blocking behavior:

- heuristic regex scans for integer-overflow, dead-code, and infinite-loop patterns are intentionally skipped and logged
  as guidance to rely on compiler/analyzer diagnostics
- non-ownership clang diagnostics emitted during ownership-focused `clang-tidy` runs are reported as non-blocking

## Common failures and quick fixes

- Missing tools (`clang`, `cppcheck`, `clang-tidy`):
  install required packages, then rerun analysis.
- Missing build directory:
  run the corresponding `build.sh` (or `test/run.sh` for test targets) before analysis.
- Missing `compile_commands.json`:
  re-run the project build; ownership `clang-tidy` checks use compile database entries.
- Ownership finding requires exemption:
  use `RAII_EXEMPT` with a non-empty reason on unavoidable raw ownership transfer boundaries only.
- No HTML report generated:
  verify text summary first; scan-build often omits HTML when no bugs are found.
