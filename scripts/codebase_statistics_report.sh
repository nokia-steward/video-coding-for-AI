#!/usr/bin/env bash
# This file is covered by the license agreement found in the file “LICENSE.txt” in the root of this project.

set -euo pipefail
exec python3 - "$@" <<'PY'
"""Generate a human-readable codebase statistics report.

The report scope is intentionally fixed:
  - encoder_cli
  - rtp_server
  - libvcai
  - test
  - docs
  - manual (when present: generated documentation directory, filesystem scan)
  - all directories named "scripts" anywhere in the repository

Exclusion rules come from Git itself (`git ls-files --exclude-standard`), which
means .gitignore is the single source of truth.
"""

from __future__ import annotations

import argparse
import ast
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import io
import itertools
from pathlib import Path, PurePosixPath
import re
import statistics
import subprocess
import sys
import tokenize

try:
    from clang import cindex as clang_cindex
except ImportError:
    clang_cindex = None

try:
    from pygments import lex
    from pygments.lexers import get_lexer_by_name
    from pygments.token import Token as PygmentsToken
except ImportError:
    lex = None
    get_lexer_by_name = None
    PygmentsToken = None

PRIMARY_ROOTS = ("encoder_cli", "rtp_server", "libvcai", "test", "docs", "manual")
REPORT_PATH = Path("reports/codebase_statistics_report.md")
TOP_FILES_LIMIT = 10
MIN_NON_BLANK_FOR_DENSITY = 40
CPP_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp"}
SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx"}
HEADER_EXTENSIONS = {".h", ".hh", ".hpp", ".hxx", ".ipp"}
COMPLEXITY_EXTENSIONS = SOURCE_EXTENSIONS | HEADER_EXTENSIONS | {".py", ".sh", ".bash"}
DUPLICATION_EXTENSIONS = SOURCE_EXTENSIONS | HEADER_EXTENSIONS | {".py", ".sh", ".bash"}
DUPLICATION_MAX_FILES = 350
DUPLICATION_MAX_LINES_PER_FILE = 2200
DUPLICATION_MIN_UNIT_LINES = 12
DUPLICATION_MAX_UNIT_LINES = 220
DUPLICATION_MIN_TOKENS = 60
DUPLICATION_SHINGLE_SIZE = 3
DUPLICATION_MINHASH_COUNT = 16
DUPLICATION_MINHASH_COLLISIONS = 3
DUPLICATION_SIMILARITY_THRESHOLD = 0.78
DUPLICATION_MAX_PAIR_EVAL = 40000
DEBT_MARKERS = ("TODO", "FIXME", "XXX", "HACK", "BUG")
DEBT_PATTERN = re.compile(r"\b(TODO|FIXME|XXX|HACK|BUG)\b", re.IGNORECASE)
CONTROL_KEYWORDS = {"if", "for", "while", "switch", "catch"}
COMPLEXITY_TOKENS = {
    "if",
    "for",
    "while",
    "case",
    "catch",
    "&&",
    "||",
    "?",
}
TOKEN_PATTERN = re.compile(
    r"[A-Za-z_]\w*|0x[0-9A-Fa-f]+|\d+(?:\.\d+)?|==|!=|<=|>=|&&|\|\||::|->|[{}()[\];,.:+\-*/%<>=!&|^~?]"
)
IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z_]\w*$")
NUMBER_PATTERN = re.compile(r"^(?:0x[0-9A-Fa-f]+|\d+(?:\.\d+)?)$")
CPP_KEYWORDS = {
    "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char",
    "class", "const", "constexpr", "const_cast", "continue", "decltype", "default",
    "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
    "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int",
    "long", "mutable", "namespace", "new", "noexcept", "nullptr", "operator",
    "private", "protected", "public", "register", "reinterpret_cast", "return",
    "short", "signed", "sizeof", "static", "static_cast", "struct", "switch",
    "template", "this", "throw", "true", "try", "typedef", "typeid", "typename",
    "union", "unsigned", "using", "virtual", "void", "volatile", "while",
}
PY_KEYWORDS = {
    "False", "None", "True", "and", "as", "assert", "async", "await", "break",
    "class", "continue", "def", "del", "elif", "else", "except", "finally", "for",
    "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or",
    "pass", "raise", "return", "try", "while", "with", "yield",
}
SH_KEYWORDS = {
    "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case",
    "esac", "function", "in", "select", "until", "time",
}
ALLOWED_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".ipp",
    ".py",
    ".sh",
    ".bash",
    ".cmake",
    ".md",
    ".rst",
    ".ini",
    ".cfg",
    ".conf",
    ".json",
    ".yaml",
    ".yml",
    ".toml",
    ".xml",
    ".html",
    ".css",
}
ALLOWED_BASENAMES = {
    "CMakeLists.txt",
    "Makefile",
    "GNUmakefile",
}
EXCLUDED_PATH_SEGMENTS = {"test_data", "tmp"}
CPP_TEST_PATTERN = re.compile(r"\b(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(")
PY_TEST_PATTERN = re.compile(r"^\s*def\s+test_[A-Za-z0-9_]*\s*\(")
CPP_DOC_PREFIX_PATTERN = re.compile(r"^(///|//!|/\*\*|/\*!)")
CPP_ASSERTION_NAME_PATTERN = re.compile(r"^(?:EXPECT|ASSERT)_[A-Z0-9_]+$")
CPP_ASSERTION_SEARCH_PATTERN = re.compile(r"\b((?:EXPECT|ASSERT)_[A-Z0-9_]+)\s*\(")
_CLANG_INDEX = None


@dataclass(frozen=True)
class LineMetrics:
    """Line-level metrics for one file."""

    total_lines: int
    blank_lines: int
    code_lines: int
    comment_lines: int
    doc_lines: int


@dataclass(frozen=True)
class ComplexityMetrics:
    """Estimated complexity metrics for one file."""

    function_count: int
    function_line_total: int
    max_function_lines: int
    cyclomatic_estimate: int


@dataclass(frozen=True)
class DebtMetrics:
    """Debt markers found in one file."""

    total: int
    by_marker: Counter[str]


@dataclass(frozen=True)
class DupUnit:
    """Candidate unit for refactor-ready duplication detection."""

    file_path: PurePosixPath
    start_line: int
    end_line: int
    token_count: int
    line_span: int
    shingles: frozenset[int]


@dataclass(frozen=True)
class FileStat:
    """All statistics tracked for one analyzed file."""

    path: PurePosixPath
    extension: str
    primary_area: str
    in_scripts_dir: bool
    test_area: str | None
    tests: int
    expect_count: int
    assert_count: int
    function_count: int
    function_line_total: int
    max_function_lines: int
    cyclomatic_estimate: int
    debt_count: int
    debt_todo: int
    debt_fixme: int
    debt_xxx: int
    debt_hack: int
    debt_bug: int
    api_declarations: int
    api_documented: int
    lines: int
    blank_lines: int
    code_lines: int
    comment_lines: int
    doc_lines: int


@dataclass(frozen=True)
class SourceTestHeuristicStat:
    """Heuristic test-linkage metrics for one source file."""

    source: FileStat
    matched_checks: int
    matched_tests: int
    matched_test_files: int


@dataclass(frozen=True)
class SummaryStat:
    """Aggregated counters for a logical area."""

    files: int
    lines: int
    blank_lines: int
    code_lines: int
    comment_lines: int
    doc_lines: int


def run_git(repo_root: Path, args: list[str]) -> str:
    """Run a git command and return stdout."""
    result = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        stderr = result.stderr.strip()
        raise RuntimeError(f"git {' '.join(args)} failed: {stderr}")
    return result.stdout


def list_git_files(repo_root: Path) -> list[PurePosixPath]:
    """List tracked + untracked non-ignored files using git ignore rules."""
    output = run_git(
        repo_root,
        ["ls-files", "--cached", "--others", "--exclude-standard"],
    )
    paths = [PurePosixPath(line.strip()) for line in output.splitlines() if line.strip()]
    return sorted(paths)


def list_manual_files(repo_root: Path) -> list[PurePosixPath]:
    """List repository-relative paths under manual/ when that directory exists (generated docs)."""
    manual_dir = repo_root / "manual"
    if not manual_dir.is_dir():
        return []
    paths = []
    for p in manual_dir.rglob("*"):
        if p.is_file():
            rel = p.relative_to(repo_root)
            paths.append(PurePosixPath(rel.as_posix()))
    return sorted(paths)


def in_scope(path: PurePosixPath) -> bool:
    """Return True if a repository-relative path is in report scope."""
    if not path.parts:
        return False
    if path.parts[0] in PRIMARY_ROOTS:
        return True
    return "scripts" in path.parts


def is_probably_text_file(path: Path) -> bool:
    """Detect obvious binary files via NUL-byte check."""
    try:
        with path.open("rb") as handle:
            chunk = handle.read(8192)
    except OSError:
        return False
    return b"\x00" not in chunk


def is_allowed_file_type(path: PurePosixPath) -> bool:
    """Return True for code/script/config/doc files included in stats."""
    if path.name in ALLOWED_BASENAMES:
        return True
    return path.suffix.lower() in ALLOWED_EXTENSIONS


def is_excluded_path(path: PurePosixPath) -> bool:
    """Return True for paths explicitly excluded from reporting."""
    return any(part in EXCLUDED_PATH_SEGMENTS for part in path.parts)


def classify_primary_area(path: PurePosixPath) -> str:
    """Map a path to one primary scope bucket."""
    first = path.parts[0] if path.parts else ""
    if first in PRIMARY_ROOTS:
        return first
    return "scripts_only"


def classify_test_area(path: PurePosixPath) -> str | None:
    """Map a test file path to the component area under test."""
    if not path.parts or path.parts[0] != "test":
        return None

    if len(path.parts) == 1:
        return "test"

    second = path.parts[1]
    if second in PRIMARY_ROOTS:
        return second
    if second == "common":
        return "common"
    return second


def discover_scripts_dirs(paths: list[PurePosixPath]) -> list[PurePosixPath]:
    """Collect all unique directories named 'scripts' present in analyzed paths."""
    discovered: set[PurePosixPath] = set()
    for file_path in paths:
        for idx, part in enumerate(file_path.parts):
            if part == "scripts":
                discovered.add(PurePosixPath(*file_path.parts[: idx + 1]))
    return sorted(discovered)


def format_int(value: int | float) -> str:
    """Format numbers with group separators."""
    if isinstance(value, float):
        return f"{value:,.2f}"
    return f"{value:,d}"


def format_pct(numerator: int, denominator: int) -> str:
    """Format a ratio as percentage."""
    if denominator <= 0:
        return "0.00%"
    return f"{(numerator / denominator) * 100.0:.2f}%"


def format_ratio(numerator: int, denominator: int) -> str:
    """Format a ratio as a plain floating-point value."""
    if denominator <= 0:
        return "0.00"
    return f"{(numerator / denominator):.2f}"


def count_html_and_content_lines(content: str) -> tuple[int, int]:
    """Count lines that contain HTML markup vs lines with probable text content.

    - html_lines: lines containing at least one tag (contain '<')
    - content_lines: lines that, after removing <...> tags, have non-whitespace left
    """
    html_lines = 0
    content_lines = 0
    for line in content.splitlines():
        if "<" in line:
            html_lines += 1
        without_tags = re.sub(r"<[^>]*>", "", line)
        if without_tags.strip():
            content_lines += 1
    return html_lines, content_lines


def canonical_section_name(heading_line: str) -> str:
    """Normalize top-level section headings for ordering."""
    heading = heading_line.removeprefix("## ").strip()
    if heading.startswith("Largest Files"):
        return "Largest Files"
    if heading.startswith("Duplication Metrics"):
        return "Duplication Metrics"
    return heading


def reorder_top_level_sections(report_text: str) -> str:
    """Reorder top-level sections for summary-first readability."""
    desired_order = [
        "Snapshot",
        "Scope",
        "Overall Totals",
        "Per-Area Totals (Primary Buckets)",
        "Extension Breakdown",
        "Extension Breakdown Per Area (Primary Buckets)",
        "Largest Files",
        "Comments and Documentation",
        "Tests",
        "Assertions and Expectations",
        "Untested Source Candidates",
        "Complexity Metrics",
        "Duplication Metrics",
        "TODO/FIXME Debt",
        "Public API Doc Coverage",
        "Scripts Directories (Cross-Cutting)",
        "Manual (Generated HTML)",
        "Legend",
    ]

    lines = report_text.splitlines()
    preamble: list[str] = []
    sections: list[tuple[str, list[str]]] = []

    current_heading = ""
    current_lines: list[str] = []
    seen_first_section = False
    for line in lines:
        if line.startswith("## "):
            if not seen_first_section:
                seen_first_section = True
            else:
                sections.append((current_heading, current_lines))
            current_heading = line
            current_lines = [line]
            continue

        if not seen_first_section:
            preamble.append(line)
        else:
            current_lines.append(line)

    if current_heading:
        sections.append((current_heading, current_lines))

    section_map: dict[str, list[str]] = {}
    unknown_sections: list[tuple[str, list[str]]] = []
    for heading, block_lines in sections:
        canonical = canonical_section_name(heading)
        if canonical in desired_order and canonical not in section_map:
            section_map[canonical] = block_lines
        else:
            unknown_sections.append((canonical, block_lines))

    ordered_lines: list[str] = preamble[:]
    for canonical in desired_order:
        block = section_map.get(canonical)
        if not block:
            continue
        if ordered_lines and ordered_lines[-1] != "":
            ordered_lines.append("")
        ordered_lines.extend(block)

    for _, block in unknown_sections:
        if ordered_lines and ordered_lines[-1] != "":
            ordered_lines.append("")
        ordered_lines.extend(block)

    return "\n".join(ordered_lines).rstrip() + "\n"


def count_tests(content: str, extension: str) -> int:
    """Count test declarations in supported languages."""
    total = 0
    for line in content.splitlines():
        if extension in CPP_EXTENSIONS:
            if CPP_TEST_PATTERN.search(line):
                total += 1
        elif extension == ".py":
            if PY_TEST_PATTERN.search(line):
                total += 1
    return total


def strip_c_like_comments_and_strings(content: str) -> str:
    """Remove C/C++ comments and string literals, keeping newlines."""
    out_chars: list[str] = []
    i = 0
    length = len(content)
    in_line_comment = False
    in_block_comment = False
    in_string = False
    string_delim = ""
    escaped = False

    while i < length:
        char = content[i]
        next_char = content[i + 1] if i + 1 < length else ""

        if in_line_comment:
            if char == "\n":
                in_line_comment = False
                out_chars.append("\n")
            else:
                out_chars.append(" ")
            i += 1
            continue

        if in_block_comment:
            if char == "*" and next_char == "/":
                in_block_comment = False
                out_chars.extend("  ")
                i += 2
                continue
            if char == "\n":
                out_chars.append("\n")
            else:
                out_chars.append(" ")
            i += 1
            continue

        if in_string:
            if char == "\n":
                in_string = False
                out_chars.append("\n")
                escaped = False
                i += 1
                continue
            if escaped:
                escaped = False
                out_chars.append(" ")
                i += 1
                continue
            if char == "\\":
                escaped = True
                out_chars.append(" ")
                i += 1
                continue
            if char == string_delim:
                in_string = False
                string_delim = ""
            out_chars.append(" ")
            i += 1
            continue

        if char == "/" and next_char == "/":
            in_line_comment = True
            out_chars.extend("  ")
            i += 2
            continue

        if char == "/" and next_char == "*":
            in_block_comment = True
            out_chars.extend("  ")
            i += 2
            continue

        if char in {"'", '"'}:
            in_string = True
            string_delim = char
            escaped = False
            out_chars.append(" ")
            i += 1
            continue

        out_chars.append(char)
        i += 1

    return "".join(out_chars)


def count_cpp_expect_assert_with_clang(path: Path, content: str) -> tuple[int, int, Counter[str]] | None:
    """Count EXPECT_*/ASSERT_* macros in C/C++ via clang tokens."""
    if clang_cindex is None:
        return None
    index = get_clang_index()
    if index is None:
        return None

    parse_args = ["-x", "c++", "-std=c++17"]
    if path.suffix.lower() == ".c":
        parse_args = ["-x", "c", "-std=c11"]

    parse_options = getattr(clang_cindex.TranslationUnit, "PARSE_DETAILED_PROCESSING_RECORD", 0)

    try:
        translation_unit = index.parse(
            str(path),
            args=parse_args,
            unsaved_files=[(str(path), content)],
            options=parse_options,
        )
        token_iter = translation_unit.get_tokens(extent=translation_unit.cursor.extent)
    except Exception:
        return None

    tokens: list = []
    for token in token_iter:
        location_file = getattr(token.location, "file", None)
        if location_file is None:
            continue
        if not same_file(Path(str(location_file)), path):
            continue
        tokens.append(token)

    expect_count = 0
    assert_count = 0
    macro_counts: Counter[str] = Counter()

    for idx, token in enumerate(tokens):
        if token.kind != clang_cindex.TokenKind.IDENTIFIER:
            continue
        macro_name = token.spelling
        if not CPP_ASSERTION_NAME_PATTERN.match(macro_name):
            continue

        next_idx = idx + 1
        while next_idx < len(tokens) and not tokens[next_idx].spelling:
            next_idx += 1
        if next_idx >= len(tokens) or tokens[next_idx].spelling != "(":
            continue

        macro_counts[macro_name] += 1
        if macro_name.startswith("EXPECT_"):
            expect_count += 1
        else:
            assert_count += 1

    return expect_count, assert_count, macro_counts


def count_cpp_expect_assert_fallback(content: str) -> tuple[int, int, Counter[str]]:
    """Fallback EXPECT_*/ASSERT_* counting with stripped C/C++ content."""
    stripped = strip_c_like_comments_and_strings(content)
    macro_counts: Counter[str] = Counter(CPP_ASSERTION_SEARCH_PATTERN.findall(stripped))
    expect_count = sum(count for name, count in macro_counts.items() if name.startswith("EXPECT_"))
    assert_count = sum(count for name, count in macro_counts.items() if name.startswith("ASSERT_"))
    return expect_count, assert_count, macro_counts


def count_python_assertions(content: str) -> tuple[int, int, Counter[str]]:
    """Count Python assert statements and unittest self.assert* calls."""
    try:
        tree = ast.parse(content)
    except SyntaxError:
        return 0, 0, Counter()

    assert_count = 0
    macro_counts: Counter[str] = Counter()
    for node in ast.walk(tree):
        if isinstance(node, ast.Assert):
            assert_count += 1
            macro_counts["assert"] += 1
            continue

        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute):
            if node.func.attr.startswith("assert"):
                assert_count += 1
                macro_counts[f"self.{node.func.attr}"] += 1

    return 0, assert_count, macro_counts


def count_expect_assertions(
    *,
    path: Path,
    extension: str,
    content: str,
    test_area: str | None,
) -> tuple[int, int, Counter[str]]:
    """Count expectations/assertions for test files only."""
    if test_area is None:
        return 0, 0, Counter()

    if extension in CPP_EXTENSIONS:
        clang_counts = count_cpp_expect_assert_with_clang(path, content)
        if clang_counts is not None:
            return clang_counts
        return count_cpp_expect_assert_fallback(content)

    if extension == ".py":
        return count_python_assertions(content)

    return 0, 0, Counter()


def normalize_stem(name: str) -> str:
    """Normalize file stems for heuristic source<->test matching."""
    value = name.lower()
    if value.startswith("test_"):
        value = value[5:]
    if value.endswith("_test"):
        value = value[:-5]
    return re.sub(r"[^a-z0-9]+", "", value)


def estimate_c_like_complexity(content: str) -> ComplexityMetrics:
    """Estimate function counts/sizes and cyclomatic complexity for C-like code."""
    stripped = strip_c_like_comments_and_strings(content)
    lines = stripped.splitlines()
    function_starts: list[int] = []
    for idx, line in enumerate(lines):
        stripped_line = line.strip()
        if not stripped_line or stripped_line.startswith("#"):
            continue
        if "(" not in stripped_line or ")" not in stripped_line or "{" not in stripped_line:
            continue
        if stripped_line.startswith(tuple(CONTROL_KEYWORDS)):
            continue
        if ";" in stripped_line and stripped_line.rfind(";") > stripped_line.find("{"):
            continue
        function_starts.append(idx)

    function_lengths: list[int] = []
    for start_idx in function_starts:
        depth = 0
        started = False
        end_idx = start_idx
        for cursor in range(start_idx, len(lines)):
            line = lines[cursor]
            opens = line.count("{")
            closes = line.count("}")
            if opens > 0:
                started = True
            depth += opens - closes
            end_idx = cursor
            if started and depth <= 0:
                break
        function_lengths.append(max(1, end_idx - start_idx + 1))

    complexity_hits = 0
    complexity_hits += len(re.findall(r"\bif\b", stripped))
    complexity_hits += len(re.findall(r"\bfor\b", stripped))
    complexity_hits += len(re.findall(r"\bwhile\b", stripped))
    complexity_hits += len(re.findall(r"\bcase\b", stripped))
    complexity_hits += len(re.findall(r"\bcatch\b", stripped))
    complexity_hits += stripped.count("&&")
    complexity_hits += stripped.count("||")
    complexity_hits += stripped.count("?")

    function_count = len(function_lengths)
    cyclomatic = complexity_hits + (1 if function_count > 0 else 0)
    return ComplexityMetrics(
        function_count=function_count,
        function_line_total=sum(function_lengths),
        max_function_lines=max(function_lengths, default=0),
        cyclomatic_estimate=cyclomatic,
    )


def estimate_python_complexity(content: str) -> ComplexityMetrics:
    """Estimate function counts/sizes and cyclomatic complexity for Python."""
    try:
        tree = ast.parse(content)
    except SyntaxError:
        return ComplexityMetrics(0, 0, 0, 0)

    function_lengths: list[int] = []
    complexity_hits = 0
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            start = getattr(node, "lineno", None)
            end = getattr(node, "end_lineno", None)
            if start is not None:
                if end is None:
                    end = start
                function_lengths.append(max(1, end - start + 1))
        if isinstance(node, (ast.If, ast.For, ast.AsyncFor, ast.While, ast.ExceptHandler, ast.IfExp)):
            complexity_hits += 1
        elif isinstance(node, ast.BoolOp):
            complexity_hits += max(1, len(node.values) - 1)
        elif isinstance(node, ast.comprehension):
            complexity_hits += 1

    function_count = len(function_lengths)
    cyclomatic = complexity_hits + (1 if function_count > 0 else 0)
    return ComplexityMetrics(
        function_count=function_count,
        function_line_total=sum(function_lengths),
        max_function_lines=max(function_lengths, default=0),
        cyclomatic_estimate=cyclomatic,
    )


def estimate_shell_complexity(content: str) -> ComplexityMetrics:
    """Estimate function counts/sizes and cyclomatic complexity for shell scripts."""
    lines = content.splitlines()
    start_pattern = re.compile(r"^\s*(?:function\s+)?[A-Za-z_]\w*\s*(?:\(\))?\s*\{")
    function_starts = [idx for idx, line in enumerate(lines) if start_pattern.search(line)]

    function_lengths: list[int] = []
    for start_idx in function_starts:
        depth = 0
        started = False
        end_idx = start_idx
        for cursor in range(start_idx, len(lines)):
            line = lines[cursor]
            opens = line.count("{")
            closes = line.count("}")
            if opens > 0:
                started = True
            depth += opens - closes
            end_idx = cursor
            if started and depth <= 0:
                break
        function_lengths.append(max(1, end_idx - start_idx + 1))

    complexity_hits = 0
    complexity_hits += len(re.findall(r"\bif\b", content))
    complexity_hits += len(re.findall(r"\bfor\b", content))
    complexity_hits += len(re.findall(r"\bwhile\b", content))
    complexity_hits += len(re.findall(r"\bcase\b", content))
    complexity_hits += content.count("&&")
    complexity_hits += content.count("||")
    complexity_hits += content.count("?")

    function_count = len(function_lengths)
    cyclomatic = complexity_hits + (1 if function_count > 0 else 0)
    return ComplexityMetrics(
        function_count=function_count,
        function_line_total=sum(function_lengths),
        max_function_lines=max(function_lengths, default=0),
        cyclomatic_estimate=cyclomatic,
    )


def estimate_complexity(path: Path, extension: str, content: str) -> ComplexityMetrics:
    """Estimate complexity for a file based on language heuristics."""
    if extension in CPP_EXTENSIONS:
        return estimate_c_like_complexity(content)
    if extension == ".py":
        return estimate_python_complexity(content)
    if extension in {".sh", ".bash"}:
        return estimate_shell_complexity(content)
    return ComplexityMetrics(0, 0, 0, 0)


def extract_comment_fragments_for_debt(line: str, extension: str, in_block_comment: bool) -> tuple[list[str], bool]:
    """Extract comment-like fragments from a line for debt scanning."""
    if extension in {".md", ".rst"}:
        return [line], in_block_comment

    fragments: list[str] = []
    if extension in CPP_EXTENSIONS:
        idx = 0
        while idx < len(line):
            if in_block_comment:
                end = line.find("*/", idx)
                if end < 0:
                    fragments.append(line[idx:])
                    idx = len(line)
                    break
                fragments.append(line[idx:end])
                in_block_comment = False
                idx = end + 2
                continue

            line_comment_idx = line.find("//", idx)
            block_idx = line.find("/*", idx)
            candidates = [value for value in (line_comment_idx, block_idx) if value >= 0]
            if not candidates:
                break
            start = min(candidates)
            if line_comment_idx >= 0 and start == line_comment_idx:
                fragments.append(line[start + 2 :])
                break
            end = line.find("*/", start + 2)
            if end < 0:
                fragments.append(line[start + 2 :])
                in_block_comment = True
                break
            fragments.append(line[start + 2 : end])
            idx = end + 2
        return fragments, in_block_comment

    if extension in {".py", ".sh", ".bash", ".cmake", ".yaml", ".yml", ".toml"}:
        marker_idx = marker_index_outside_quotes(line, "#")
        if marker_idx >= 0:
            fragments.append(line[marker_idx + 1 :])
        return fragments, in_block_comment

    if extension in {".ini", ".cfg", ".conf"}:
        hash_idx = marker_index_outside_quotes(line, "#")
        semi_idx = marker_index_outside_quotes(line, ";")
        indexes = [value for value in (hash_idx, semi_idx) if value >= 0]
        if indexes:
            idx = min(indexes)
            fragments.append(line[idx + 1 :])
        return fragments, in_block_comment

    return fragments, in_block_comment


def count_debt_markers(content: str, extension: str) -> DebtMetrics:
    """Count TODO/FIXME-like debt markers."""
    if extension in {".md", ".rst"}:
        return DebtMetrics(total=0, by_marker=Counter())

    marker_counter: Counter[str] = Counter()
    in_block_comment = False
    for line in content.splitlines():
        fragments, in_block_comment = extract_comment_fragments_for_debt(
            line,
            extension,
            in_block_comment,
        )
        for fragment in fragments:
            for marker in DEBT_PATTERN.findall(fragment):
                marker_counter[marker.upper()] += 1

    return DebtMetrics(total=sum(marker_counter.values()), by_marker=marker_counter)


def is_public_api_declaration(statement: str) -> bool:
    """Heuristic check for public API declaration statements in headers."""
    statement = statement.strip()
    if not statement:
        return False
    if statement.startswith(("if ", "for ", "while ", "switch ", "catch ", "return ", "static_assert", "using namespace")):
        return False
    if re.search(r"\b(class|struct|enum)\s+[A-Za-z_]\w*", statement):
        return True
    if "(" in statement and ")" in statement and ";" in statement:
        if statement.startswith(("typedef ", "using ")):
            return False
        return True
    return False


def analyze_public_api_doc_coverage(relative_path: PurePosixPath, extension: str, content: str) -> tuple[int, int]:
    """Estimate public API documentation coverage for include headers."""
    if extension not in HEADER_EXTENSIONS:
        return 0, 0
    if "include" not in relative_path.parts:
        return 0, 0

    total_decl = 0
    documented_decl = 0
    in_doc_block = False
    pending_doc = False
    statement_parts: list[str] = []

    for raw_line in content.splitlines():
        line = raw_line.strip()
        if not line:
            continue

        if in_doc_block:
            pending_doc = True
            if "*/" in line:
                in_doc_block = False
            continue

        if line.startswith(("/**", "/*!")):
            pending_doc = True
            if "*/" not in line:
                in_doc_block = True
            continue
        if line.startswith(("///", "//!")):
            pending_doc = True
            continue
        if line.startswith("//"):
            continue
        if line.startswith("#"):
            statement_parts.clear()
            pending_doc = False
            continue

        statement_parts.append(line)
        if ";" not in line and "{" not in line:
            continue

        statement = " ".join(statement_parts)
        statement_parts.clear()
        if is_public_api_declaration(statement):
            total_decl += 1
            if pending_doc:
                documented_decl += 1
        pending_doc = False

    return total_decl, documented_decl


def normalize_token_for_duplication(token: str, extension: str) -> str:
    """Normalize tokens for near-duplicate detection."""
    if NUMBER_PATTERN.match(token):
        return "NUM"
    if not IDENTIFIER_PATTERN.match(token):
        return token

    keywords = CPP_KEYWORDS
    if extension == ".py":
        keywords = PY_KEYWORDS
    elif extension in {".sh", ".bash"}:
        keywords = SH_KEYWORDS
    return token if token in keywords else "ID"


def normalized_line_signatures_for_duplication(content: str, extension: str) -> list[tuple[int, str]]:
    """Build token-normalized per-line signatures."""
    if extension in CPP_EXTENSIONS:
        normalized_source = strip_c_like_comments_and_strings(content)
    else:
        normalized_source = re.sub(r"'[^'\\]*(?:\\.[^'\\]*)*'|\"[^\"\\]*(?:\\.[^\"\\]*)*\"", "STR", content)

    signatures: list[tuple[int, str]] = []
    for line_no, line in enumerate(normalized_source.splitlines(), start=1):
        tokens = TOKEN_PATTERN.findall(line)
        if not tokens:
            continue
        normalized_tokens = [normalize_token_for_duplication(token, extension) for token in tokens]
        if len(normalized_tokens) < 4:
            continue
        signatures.append((line_no, " ".join(normalized_tokens)))
    return signatures


def extract_c_like_function_ranges(content: str) -> list[tuple[int, int]]:
    """Extract heuristic C-like function ranges (1-based line numbers)."""
    stripped = strip_c_like_comments_and_strings(content)
    lines = stripped.splitlines()
    starts: list[int] = []
    for idx, line in enumerate(lines):
        stripped_line = line.strip()
        if not stripped_line or stripped_line.startswith("#"):
            continue
        if "(" not in stripped_line or ")" not in stripped_line or "{" not in stripped_line:
            continue
        if stripped_line.startswith(tuple(CONTROL_KEYWORDS)):
            continue
        if ";" in stripped_line and stripped_line.rfind(";") > stripped_line.find("{"):
            continue
        starts.append(idx)

    ranges: list[tuple[int, int]] = []
    for start_idx in starts:
        depth = 0
        started = False
        end_idx = start_idx
        for cursor in range(start_idx, len(lines)):
            current = lines[cursor]
            opens = current.count("{")
            closes = current.count("}")
            if opens > 0:
                started = True
            depth += opens - closes
            end_idx = cursor
            if started and depth <= 0:
                break
        if end_idx >= start_idx:
            ranges.append((start_idx + 1, end_idx + 1))
    return ranges


def extract_python_function_ranges(content: str) -> list[tuple[int, int]]:
    """Extract Python function ranges via AST."""
    try:
        tree = ast.parse(content)
    except SyntaxError:
        return []

    ranges: list[tuple[int, int]] = []
    for node in ast.walk(tree):
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        start = getattr(node, "lineno", None)
        end = getattr(node, "end_lineno", None)
        if start is None:
            continue
        if end is None:
            end = start
        ranges.append((start, end))
    return ranges


def extract_shell_function_ranges(content: str) -> list[tuple[int, int]]:
    """Extract shell function ranges by brace matching."""
    lines = content.splitlines()
    start_pattern = re.compile(r"^\s*(?:function\s+)?[A-Za-z_]\w*\s*(?:\(\))?\s*\{")
    starts = [idx for idx, line in enumerate(lines) if start_pattern.search(line)]
    ranges: list[tuple[int, int]] = []
    for start_idx in starts:
        depth = 0
        started = False
        end_idx = start_idx
        for cursor in range(start_idx, len(lines)):
            current = lines[cursor]
            opens = current.count("{")
            closes = current.count("}")
            if opens > 0:
                started = True
            depth += opens - closes
            end_idx = cursor
            if started and depth <= 0:
                break
        if end_idx >= start_idx:
            ranges.append((start_idx + 1, end_idx + 1))
    return ranges


def stable_hash_int(value: str) -> int:
    """Stable 64-bit hash for duplication signatures."""
    return int.from_bytes(hashlib.blake2b(value.encode("utf-8"), digest_size=8).digest(), "big")


def jaccard_similarity(left: frozenset[int], right: frozenset[int]) -> float:
    """Compute Jaccard similarity of shingle sets."""
    if not left or not right:
        return 0.0
    intersection = len(left & right)
    union = len(left | right)
    if union == 0:
        return 0.0
    return intersection / union


def extract_dup_units_for_file(
    *,
    relative_path: PurePosixPath,
    extension: str,
    content: str,
) -> list[DupUnit]:
    """Extract function-level duplication units for refactorability analysis."""
    if extension in CPP_EXTENSIONS:
        ranges = extract_c_like_function_ranges(content)
    elif extension == ".py":
        ranges = extract_python_function_ranges(content)
    elif extension in {".sh", ".bash"}:
        ranges = extract_shell_function_ranges(content)
    else:
        ranges = []

    if not ranges:
        return []

    signatures = normalized_line_signatures_for_duplication(content, extension)
    signature_by_line = {line_no: signature for line_no, signature in signatures}

    units: list[DupUnit] = []
    for start_line, end_line in ranges:
        line_span = end_line - start_line + 1
        if line_span < DUPLICATION_MIN_UNIT_LINES or line_span > DUPLICATION_MAX_UNIT_LINES:
            continue

        token_stream: list[str] = []
        for line_no in range(start_line, end_line + 1):
            signature = signature_by_line.get(line_no)
            if not signature:
                continue
            token_stream.extend(signature.split())

        if len(token_stream) < DUPLICATION_MIN_TOKENS:
            continue
        if len(token_stream) < DUPLICATION_SHINGLE_SIZE:
            continue

        shingles: set[int] = set()
        for idx in range(0, len(token_stream) - DUPLICATION_SHINGLE_SIZE + 1):
            shingle = " ".join(token_stream[idx : idx + DUPLICATION_SHINGLE_SIZE])
            shingles.add(stable_hash_int(shingle))

        if len(shingles) < 8:
            continue

        units.append(
            DupUnit(
                file_path=relative_path,
                start_line=start_line,
                end_line=end_line,
                token_count=len(token_stream),
                line_span=line_span,
                shingles=frozenset(shingles),
            )
        )

    return units


def compute_duplication_metrics(repo_root: Path, file_stats: list[FileStat]) -> dict[str, object]:
    """Compute refactor-ready near-duplicate function clusters."""
    candidates = [
        item
        for item in sorted(file_stats, key=lambda stat: (-stat.lines, str(stat.path)))
        if item.extension in DUPLICATION_EXTENSIONS and item.lines <= DUPLICATION_MAX_LINES_PER_FILE
    ][:DUPLICATION_MAX_FILES]

    units: list[DupUnit] = []
    for item in candidates:
        absolute_path = repo_root / item.path
        try:
            content = absolute_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        units.extend(
            extract_dup_units_for_file(
                relative_path=item.path,
                extension=item.extension,
                content=content,
            )
        )

    if len(units) < 2:
        return {
            "analyzed_files": len(candidates),
            "units_considered": len(units),
            "candidate_pairs": 0,
            "pairs_evaluated": 0,
            "refactor_clusters": 0,
            "estimated_extractable_lines": 0,
            "top_files": [],
            "top_clusters": [],
        }

    minhash_index: dict[int, list[int]] = {}
    for idx, unit in enumerate(units):
        minhashes = sorted(unit.shingles)[:DUPLICATION_MINHASH_COUNT]
        for value in minhashes:
            minhash_index.setdefault(value, []).append(idx)

    pair_collision_counter: Counter[tuple[int, int]] = Counter()
    for bucket in minhash_index.values():
        if len(bucket) < 2:
            continue
        if len(bucket) > 80:
            continue
        for left_idx, right_idx in itertools.combinations(sorted(bucket), 2):
            pair_collision_counter[(left_idx, right_idx)] += 1

    candidate_pairs = [
        pair
        for pair, collisions in pair_collision_counter.items()
        if collisions >= DUPLICATION_MINHASH_COLLISIONS
    ]
    candidate_pairs.sort(key=lambda pair: (-pair_collision_counter[pair], pair))

    adjacency: dict[int, set[int]] = {}
    edge_similarity: dict[tuple[int, int], float] = {}
    evaluated = 0
    for left_idx, right_idx in candidate_pairs:
        if evaluated >= DUPLICATION_MAX_PAIR_EVAL:
            break
        evaluated += 1
        left_unit = units[left_idx]
        right_unit = units[right_idx]
        if left_unit.file_path == right_unit.file_path:
            continue
        similarity = jaccard_similarity(left_unit.shingles, right_unit.shingles)
        if similarity < DUPLICATION_SIMILARITY_THRESHOLD:
            continue
        adjacency.setdefault(left_idx, set()).add(right_idx)
        adjacency.setdefault(right_idx, set()).add(left_idx)
        edge_similarity[(left_idx, right_idx)] = similarity

    components: list[list[int]] = []
    visited: set[int] = set()
    for node in adjacency:
        if node in visited:
            continue
        stack = [node]
        component: list[int] = []
        visited.add(node)
        while stack:
            current = stack.pop()
            component.append(current)
            for neighbor in adjacency.get(current, set()):
                if neighbor in visited:
                    continue
                visited.add(neighbor)
                stack.append(neighbor)
        if len(component) >= 2:
            components.append(sorted(component))

    clusters: list[dict[str, object]] = []
    file_dup_line_estimate: Counter[PurePosixPath] = Counter()
    for component in components:
        member_units = [units[index] for index in component]
        distinct_files = {unit.file_path for unit in member_units}
        if len(distinct_files) < 2:
            continue

        similarities: list[float] = []
        for left_idx, right_idx in itertools.combinations(component, 2):
            key = (left_idx, right_idx) if (left_idx, right_idx) in edge_similarity else (right_idx, left_idx)
            similarity = edge_similarity.get(key)
            if similarity is not None:
                similarities.append(similarity)
        average_similarity = sum(similarities) / len(similarities) if similarities else DUPLICATION_SIMILARITY_THRESHOLD

        line_spans = [unit.line_span for unit in member_units]
        common_lines = int(statistics.median(line_spans))
        estimated_savings = max(0, (len(member_units) - 1) * common_lines)

        for unit in member_units:
            file_dup_line_estimate[unit.file_path] += common_lines

        examples = sorted(
            member_units,
            key=lambda unit: (-unit.line_span, str(unit.file_path), unit.start_line),
        )[:3]
        locations = [
            f"{unit.file_path.as_posix()}:{unit.start_line}-{unit.end_line}"
            for unit in examples
        ]

        clusters.append(
            {
                "members": len(member_units),
                "files": len(distinct_files),
                "avg_similarity": average_similarity,
                "common_lines": common_lines,
                "estimated_savings": estimated_savings,
                "locations": locations,
            }
        )

    clusters.sort(
        key=lambda cluster: (
            -int(cluster["estimated_savings"]),
            -float(cluster["avg_similarity"]),
            -int(cluster["members"]),
        )
    )

    top_files = sorted(
        file_dup_line_estimate.items(),
        key=lambda pair: (-pair[1], str(pair[0])),
    )[:TOP_FILES_LIMIT]

    return {
        "analyzed_files": len(candidates),
        "units_considered": len(units),
        "candidate_pairs": len(candidate_pairs),
        "pairs_evaluated": evaluated,
        "refactor_clusters": len(clusters),
        "estimated_extractable_lines": sum(int(cluster["estimated_savings"]) for cluster in clusters),
        "top_clusters": clusters[:TOP_FILES_LIMIT],
        "top_files": top_files,
    }


def stems_heuristically_match(source_stem: str, test_stem: str) -> bool:
    """Return True when two normalized stems likely refer to same target."""
    if not source_stem or not test_stem:
        return False
    return (
        source_stem == test_stem
        or source_stem in test_stem
        or test_stem in source_stem
    )


def rank_least_tested_source_files(file_stats: list[FileStat]) -> list[SourceTestHeuristicStat]:
    """Rank source files by heuristic matched checks (`EXPECT + ASSERT`)."""
    test_stem_metrics_by_area: dict[str, dict[str, list[int]]] = {}
    for item in file_stats:
        if item.test_area is None:
            continue
        if item.extension not in SOURCE_EXTENSIONS | HEADER_EXTENSIONS | {".py"}:
            continue
        stem = normalize_stem(item.path.stem)
        if not stem:
            continue
        area_metrics = test_stem_metrics_by_area.setdefault(item.test_area, {})
        aggregate = area_metrics.setdefault(stem, [0, 0, 0])
        aggregate[0] += item.expect_count + item.assert_count
        aggregate[1] += item.tests
        aggregate[2] += 1

    candidates: list[SourceTestHeuristicStat] = []
    for item in file_stats:
        parts = item.path.parts
        if not parts:
            continue
        area = parts[0]
        if area not in {"encoder_cli", "rtp_server", "libvcai"}:
            continue
        if "src" not in parts:
            continue
        if item.extension not in SOURCE_EXTENSIONS:
            continue

        source_stem = normalize_stem(item.path.stem)
        matched_checks = 0
        matched_tests = 0
        matched_test_files = 0

        area_metrics = test_stem_metrics_by_area.get(area, {})
        common_metrics = test_stem_metrics_by_area.get("common", {})
        for test_stem, values in itertools.chain(area_metrics.items(), common_metrics.items()):
            if not stems_heuristically_match(source_stem, test_stem):
                continue
            checks_count, tests_count, test_file_count = values
            matched_checks += checks_count
            matched_tests += tests_count
            matched_test_files += test_file_count

        candidates.append(
            SourceTestHeuristicStat(
                source=item,
                matched_checks=matched_checks,
                matched_tests=matched_tests,
                matched_test_files=matched_test_files,
            )
        )

    return sorted(
        candidates,
        key=lambda stat: (
            stat.matched_checks,
            -stat.source.lines,
            str(stat.source.path),
        ),
    )


def line_numbers_for_blank(lines: list[str]) -> set[int]:
    """Return 1-based line numbers that are blank."""
    return {idx for idx, line in enumerate(lines, start=1) if not line.strip()}


def finalize_line_metrics(
    *,
    total_lines: int,
    blank_lines: set[int],
    code_lines: set[int],
    comment_lines: set[int],
    doc_lines: set[int],
) -> LineMetrics:
    """Normalize line-number sets and build metric totals."""
    if total_lines <= 0:
        return LineMetrics(
            total_lines=0,
            blank_lines=0,
            code_lines=0,
            comment_lines=0,
            doc_lines=0,
        )

    valid_lines = set(range(1, total_lines + 1))
    blank = blank_lines & valid_lines
    code = (code_lines & valid_lines) - blank
    comment = (comment_lines & valid_lines) - blank
    docs = (doc_lines & valid_lines) - blank

    return LineMetrics(
        total_lines=total_lines,
        blank_lines=len(blank),
        code_lines=len(code),
        comment_lines=len(comment),
        doc_lines=len(docs),
    )


def analyze_plain_non_comment(lines: list[str]) -> LineMetrics:
    """Treat all non-blank lines as code and no comments/docs."""
    total_lines = len(lines)
    blank = line_numbers_for_blank(lines)
    code = set(range(1, total_lines + 1)) - blank
    return finalize_line_metrics(
        total_lines=total_lines,
        blank_lines=blank,
        code_lines=code,
        comment_lines=set(),
        doc_lines=set(),
    )


def analyze_markdown(lines: list[str]) -> LineMetrics:
    """Treat non-blank markdown/reStructuredText content as documentation."""
    total_lines = len(lines)
    blank = line_numbers_for_blank(lines)
    docs = set(range(1, total_lines + 1)) - blank
    return finalize_line_metrics(
        total_lines=total_lines,
        blank_lines=blank,
        code_lines=set(),
        comment_lines=set(),
        doc_lines=docs,
    )


def marker_index_outside_quotes(line: str, marker: str) -> int:
    """Find marker index outside single/double quotes, or -1 if absent."""
    in_single = False
    in_double = False
    escaped = False
    for idx, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if (in_single or in_double) and char == "\\":
            escaped = True
            continue
        if char == "'" and not in_double:
            in_single = not in_single
            continue
        if char == '"' and not in_single:
            in_double = not in_double
            continue
        if not in_single and not in_double and char == marker:
            return idx
    return -1


def analyze_line_comments(
    lines: list[str],
    *,
    markers: tuple[str, ...],
    treat_comments_as_docs: bool,
) -> LineMetrics:
    """Line-oriented parser for languages with single-line comment markers."""
    blank = line_numbers_for_blank(lines)
    code_lines: set[int] = set()
    comment_lines: set[int] = set()
    doc_lines: set[int] = set()

    for line_no, line in enumerate(lines, start=1):
        stripped = line.strip()
        if not stripped:
            continue

        leading_comment = False
        for marker in markers:
            if stripped.startswith(marker):
                leading_comment = True
                break

        if leading_comment:
            comment_lines.add(line_no)
            if treat_comments_as_docs:
                doc_lines.add(line_no)
            continue

        comment_index = -1
        for marker in markers:
            idx = marker_index_outside_quotes(line, marker)
            if idx >= 0 and (comment_index < 0 or idx < comment_index):
                comment_index = idx

        if comment_index >= 0:
            prefix = line[:comment_index]
            suffix = line[comment_index:]
            if prefix.strip():
                code_lines.add(line_no)
            if suffix.strip():
                comment_lines.add(line_no)
                if treat_comments_as_docs:
                    doc_lines.add(line_no)
        else:
            code_lines.add(line_no)

    return finalize_line_metrics(
        total_lines=len(lines),
        blank_lines=blank,
        code_lines=code_lines,
        comment_lines=comment_lines,
        doc_lines=doc_lines,
    )


def analyze_xml_fallback(lines: list[str]) -> LineMetrics:
    """Fallback XML analyzer with support for <!-- --> comments."""
    blank = line_numbers_for_blank(lines)
    code_lines: set[int] = set()
    comment_lines: set[int] = set()
    doc_lines: set[int] = set()

    in_comment = False
    for line_no, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        idx = 0
        has_code = False
        has_comment = False
        while idx < len(line):
            if in_comment:
                has_comment = True
                end = line.find("-->", idx)
                if end < 0:
                    idx = len(line)
                    break
                in_comment = False
                idx = end + 3
                continue

            start = line.find("<!--", idx)
            if start < 0:
                if line[idx:].strip():
                    has_code = True
                break

            if line[idx:start].strip():
                has_code = True
            has_comment = True
            end = line.find("-->", start + 4)
            if end < 0:
                in_comment = True
                idx = len(line)
            else:
                idx = end + 3

        if has_code:
            code_lines.add(line_no)
        if has_comment:
            comment_lines.add(line_no)
            doc_lines.add(line_no)

    return finalize_line_metrics(
        total_lines=len(lines),
        blank_lines=blank,
        code_lines=code_lines,
        comment_lines=comment_lines,
        doc_lines=doc_lines,
    )


def analyze_with_pygments(
    *,
    content: str,
    lines: list[str],
    lexer_name: str,
    treat_comments_as_docs: bool,
) -> LineMetrics | None:
    """Analyze file with Pygments tokenization."""
    if lex is None or get_lexer_by_name is None or PygmentsToken is None:
        return None

    try:
        lexer = get_lexer_by_name(lexer_name)
    except Exception:
        return None

    blank = line_numbers_for_blank(lines)
    code_lines: set[int] = set()
    comment_lines: set[int] = set()
    doc_lines: set[int] = set()
    current_line = 1

    try:
        for token_type, token_text in lex(content, lexer):
            if not token_text:
                continue
            chunks = token_text.splitlines(keepends=True)
            for chunk in chunks:
                chunk_text = chunk[:-1] if chunk.endswith("\n") else chunk
                if chunk_text.strip():
                    if token_type in PygmentsToken.Comment:
                        comment_lines.add(current_line)
                        if treat_comments_as_docs:
                            doc_lines.add(current_line)
                    else:
                        code_lines.add(current_line)
                if chunk.endswith("\n"):
                    current_line += 1
    except Exception:
        return None

    return finalize_line_metrics(
        total_lines=len(lines),
        blank_lines=blank,
        code_lines=code_lines,
        comment_lines=comment_lines,
        doc_lines=doc_lines,
    )


def extract_python_docstring_lines(tree: ast.AST) -> set[int]:
    """Collect source line numbers that belong to Python docstrings."""
    doc_lines: set[int] = set()
    doc_nodes = (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)

    for node in ast.walk(tree):
        if not isinstance(node, doc_nodes):
            continue
        body = getattr(node, "body", None)
        if not body:
            continue
        first_stmt = body[0]
        if not isinstance(first_stmt, ast.Expr):
            continue
        value = first_stmt.value
        if isinstance(value, ast.Constant):
            is_doc = isinstance(value.value, str)
            doc_value = value.value if is_doc else ""
        elif isinstance(value, ast.Str):
            is_doc = True
            doc_value = value.s
        else:
            is_doc = False
            doc_value = ""
        if not is_doc:
            continue

        start_line = getattr(first_stmt, "lineno", None)
        end_line = getattr(first_stmt, "end_lineno", None)
        if start_line is None:
            continue
        if end_line is None:
            end_line = start_line + doc_value.count("\n")
        doc_lines.update(range(start_line, end_line + 1))

    return doc_lines


def analyze_python(content: str, lines: list[str]) -> LineMetrics:
    """Analyze Python via tokenize + AST docstring extraction."""
    blank = line_numbers_for_blank(lines)
    code_lines: set[int] = set()
    comment_lines: set[int] = set()
    doc_lines: set[int] = set()

    try:
        tree = ast.parse(content)
        doc_lines = extract_python_docstring_lines(tree)
    except SyntaxError:
        doc_lines = set()

    ignored_tokens = {
        tokenize.NL,
        tokenize.NEWLINE,
        tokenize.INDENT,
        tokenize.DEDENT,
        tokenize.ENDMARKER,
        tokenize.ENCODING,
    }

    try:
        for tok in tokenize.generate_tokens(io.StringIO(content).readline):
            token_type = tok.type
            start_line = tok.start[0]
            end_line = tok.end[0]
            if start_line <= 0:
                continue
            line_range = range(start_line, end_line + 1)
            if token_type == tokenize.COMMENT:
                comment_lines.update(line_range)
                continue
            if token_type in ignored_tokens:
                continue
            if token_type == tokenize.STRING and start_line in doc_lines:
                continue
            if tok.string.strip():
                code_lines.update(line_range)
    except tokenize.TokenError:
        fallback = analyze_line_comments(
            lines,
            markers=("#",),
            treat_comments_as_docs=False,
        )
        return LineMetrics(
            total_lines=fallback.total_lines,
            blank_lines=fallback.blank_lines,
            code_lines=fallback.code_lines,
            comment_lines=fallback.comment_lines,
            doc_lines=0,
        )

    return finalize_line_metrics(
        total_lines=len(lines),
        blank_lines=blank,
        code_lines=code_lines,
        comment_lines=comment_lines,
        doc_lines=doc_lines,
    )


def get_clang_index():
    """Lazily create a shared clang index."""
    global _CLANG_INDEX
    if clang_cindex is None:
        return None
    if _CLANG_INDEX is None:
        _CLANG_INDEX = clang_cindex.Index.create()
    return _CLANG_INDEX


def same_file(a: Path, b: Path) -> bool:
    """Safely compare paths that may be relative/symlinked."""
    try:
        return a.resolve() == b.resolve()
    except OSError:
        return a == b


def analyze_cpp_with_clang(path: Path, content: str, lines: list[str]) -> LineMetrics | None:
    """Analyze C/C++ comments and code with clang tokenization."""
    if clang_cindex is None:
        return None
    index = get_clang_index()
    if index is None:
        return None

    parse_args = ["-x", "c++", "-std=c++17"]
    if path.suffix.lower() == ".c":
        parse_args = ["-x", "c", "-std=c11"]

    parse_options = 0
    tu_options = getattr(clang_cindex.TranslationUnit, "PARSE_DETAILED_PROCESSING_RECORD", 0)
    parse_options |= tu_options

    try:
        translation_unit = index.parse(
            str(path),
            args=parse_args,
            unsaved_files=[(str(path), content)],
            options=parse_options,
        )
    except Exception:
        return None

    blank = line_numbers_for_blank(lines)
    code_lines: set[int] = set()
    comment_lines: set[int] = set()
    doc_lines: set[int] = set()

    try:
        token_iter = translation_unit.get_tokens(extent=translation_unit.cursor.extent)
    except Exception:
        return None

    for token in token_iter:
        location = token.location
        location_file = getattr(location, "file", None)
        if location_file is None:
            continue

        token_file = Path(str(location_file))
        if not same_file(token_file, path):
            continue

        start_line = token.extent.start.line
        end_line = token.extent.end.line
        if end_line < start_line:
            continue
        token_lines = range(start_line, end_line + 1)

        if token.kind == clang_cindex.TokenKind.COMMENT:
            comment_lines.update(token_lines)
            spelling = token.spelling.lstrip()
            if CPP_DOC_PREFIX_PATTERN.match(spelling):
                doc_lines.update(token_lines)
        elif token.spelling.strip():
            code_lines.update(token_lines)

    return finalize_line_metrics(
        total_lines=len(lines),
        blank_lines=blank,
        code_lines=code_lines,
        comment_lines=comment_lines,
        doc_lines=doc_lines,
    )


def analyze_c_like_fallback(lines: list[str]) -> LineMetrics:
    """Fallback C/C++ analyzer handling // and /* */ comments."""
    blank = line_numbers_for_blank(lines)
    code_lines: set[int] = set()
    comment_lines: set[int] = set()
    doc_lines: set[int] = set()

    in_block_comment = False
    block_is_doc = False

    for line_no, line in enumerate(lines, start=1):
        idx = 0
        has_code = False
        has_comment = False
        has_doc = False
        line_len = len(line)

        while idx < line_len:
            char = line[idx]
            next_char = line[idx + 1] if idx + 1 < line_len else ""

            if in_block_comment:
                has_comment = True
                if block_is_doc:
                    has_doc = True
                end_idx = line.find("*/", idx)
                if end_idx < 0:
                    idx = line_len
                    break
                in_block_comment = False
                block_is_doc = False
                idx = end_idx + 2
                continue

            if char in {" ", "\t", "\r"}:
                idx += 1
                continue

            if char == "/" and next_char == "/":
                has_comment = True
                comment_text = line[idx:]
                if comment_text.startswith("///") or comment_text.startswith("//!"):
                    has_doc = True
                idx = line_len
                break

            if char == "/" and next_char == "*":
                has_comment = True
                is_doc = idx + 2 < line_len and line[idx + 2] in {"*", "!"}
                if is_doc:
                    has_doc = True
                in_block_comment = True
                block_is_doc = is_doc
                idx += 2
                continue

            if char in {"'", '"'}:
                has_code = True
                quote = char
                idx += 1
                while idx < line_len:
                    if line[idx] == "\\":
                        idx += 2
                        continue
                    if line[idx] == quote:
                        idx += 1
                        break
                    idx += 1
                continue

            has_code = True
            idx += 1

        if has_code:
            code_lines.add(line_no)
        if has_comment:
            comment_lines.add(line_no)
        if has_doc:
            doc_lines.add(line_no)

    return finalize_line_metrics(
        total_lines=len(lines),
        blank_lines=blank,
        code_lines=code_lines,
        comment_lines=comment_lines,
        doc_lines=doc_lines,
    )


def analyze_line_metrics(path: Path, content: str) -> LineMetrics:
    """Dispatch line analysis by file type/language."""
    lines = content.splitlines()
    extension = path.suffix.lower() if path.suffix else "(no_ext)"
    basename = path.name

    if extension in CPP_EXTENSIONS:
        with_clang = analyze_cpp_with_clang(path, content, lines)
        if with_clang is not None:
            return with_clang
        return analyze_c_like_fallback(lines)

    if extension == ".py":
        return analyze_python(content, lines)

    if extension in {".md", ".rst"}:
        return analyze_markdown(lines)

    if extension in {".sh", ".bash"}:
        with_pygments = analyze_with_pygments(
            content=content,
            lines=lines,
            lexer_name="bash",
            treat_comments_as_docs=True,
        )
        if with_pygments is not None:
            return with_pygments
        return analyze_line_comments(
            lines,
            markers=("#",),
            treat_comments_as_docs=True,
        )

    if extension == ".cmake" or basename == "CMakeLists.txt":
        with_pygments = analyze_with_pygments(
            content=content,
            lines=lines,
            lexer_name="cmake",
            treat_comments_as_docs=True,
        )
        if with_pygments is not None:
            return with_pygments
        return analyze_line_comments(
            lines,
            markers=("#",),
            treat_comments_as_docs=True,
        )

    if extension in {".ini", ".cfg", ".conf"}:
        with_pygments = analyze_with_pygments(
            content=content,
            lines=lines,
            lexer_name="ini",
            treat_comments_as_docs=True,
        )
        if with_pygments is not None:
            return with_pygments
        return analyze_line_comments(
            lines,
            markers=("#", ";"),
            treat_comments_as_docs=True,
        )

    if extension in {".yaml", ".yml"}:
        with_pygments = analyze_with_pygments(
            content=content,
            lines=lines,
            lexer_name="yaml",
            treat_comments_as_docs=True,
        )
        if with_pygments is not None:
            return with_pygments
        return analyze_line_comments(
            lines,
            markers=("#",),
            treat_comments_as_docs=True,
        )

    if extension == ".toml":
        with_pygments = analyze_with_pygments(
            content=content,
            lines=lines,
            lexer_name="toml",
            treat_comments_as_docs=True,
        )
        if with_pygments is not None:
            return with_pygments
        return analyze_line_comments(
            lines,
            markers=("#",),
            treat_comments_as_docs=True,
        )

    if extension == ".xml":
        with_pygments = analyze_with_pygments(
            content=content,
            lines=lines,
            lexer_name="xml",
            treat_comments_as_docs=True,
        )
        if with_pygments is not None:
            return with_pygments
        return analyze_xml_fallback(lines)

    if extension == ".json":
        with_pygments = analyze_with_pygments(
            content=content,
            lines=lines,
            lexer_name="json",
            treat_comments_as_docs=False,
        )
        if with_pygments is not None:
            return with_pygments
        return analyze_plain_non_comment(lines)

    return analyze_plain_non_comment(lines)


def summarize(stats: list[FileStat]) -> dict[str, SummaryStat]:
    """Build per-area summary counters."""
    summary: dict[str, SummaryStat] = {}
    for area in (*PRIMARY_ROOTS, "scripts_only"):
        area_items = [item for item in stats if item.primary_area == area]
        summary[area] = SummaryStat(
            files=len(area_items),
            lines=sum(item.lines for item in area_items),
            blank_lines=sum(item.blank_lines for item in area_items),
            code_lines=sum(item.code_lines for item in area_items),
            comment_lines=sum(item.comment_lines for item in area_items),
            doc_lines=sum(item.doc_lines for item in area_items),
        )
    return summary


def build_report(
    *,
    repo_root: Path,
    file_stats: list[FileStat],
    assertion_macro_counts: Counter[str],
    duplication_metrics: dict[str, object],
    least_tested_sources: list[SourceTestHeuristicStat],
    skipped_by_path: int,
    skipped_by_type: int,
    skipped_binary: int,
    skipped_errors: int,
    manual_html_metrics: tuple[int, int, int] = (0, 0, 0),
) -> str:
    """Render the final markdown report."""
    now_utc = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S %Z")
    now_local = datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %Z")
    try:
        branch = run_git(repo_root, ["rev-parse", "--abbrev-ref", "HEAD"]).strip()
        commit = run_git(repo_root, ["rev-parse", "--short", "HEAD"]).strip()
    except RuntimeError:
        branch = "unknown"
        commit = "unknown"

    total_files = len(file_stats)
    total_lines = sum(item.lines for item in file_stats)
    total_blank = sum(item.blank_lines for item in file_stats)
    total_code = sum(item.code_lines for item in file_stats)
    total_comment = sum(item.comment_lines for item in file_stats)
    total_doc = sum(item.doc_lines for item in file_stats)
    total_non_blank = total_lines - total_blank
    line_values = [item.lines for item in file_stats]

    avg_lines = (total_lines / total_files) if total_files else 0.0
    median_lines = statistics.median(line_values) if line_values else 0.0
    min_item = min(file_stats, key=lambda item: (item.lines, str(item.path)), default=None)
    max_item = max(file_stats, key=lambda item: (item.lines, str(item.path)), default=None)
    total_tests = sum(item.tests for item in file_stats)
    test_files = sum(1 for item in file_stats if item.tests > 0)
    tests_by_area: Counter[str] = Counter()
    test_files_by_area: Counter[str] = Counter()
    expect_by_area: Counter[str] = Counter()
    assert_by_area: Counter[str] = Counter()
    total_expect = sum(item.expect_count for item in file_stats)
    total_assert = sum(item.assert_count for item in file_stats)
    total_checks = total_expect + total_assert
    total_functions = sum(item.function_count for item in file_stats)
    total_function_lines = sum(item.function_line_total for item in file_stats)
    max_function_lines = max((item.max_function_lines for item in file_stats), default=0)
    total_cyclomatic = sum(item.cyclomatic_estimate for item in file_stats)
    complexity_file_count = sum(1 for item in file_stats if item.cyclomatic_estimate > 0)
    total_debt = sum(item.debt_count for item in file_stats)
    total_api_declarations = sum(item.api_declarations for item in file_stats)
    total_api_documented = sum(item.api_documented for item in file_stats)
    untested_source_count = sum(1 for item in least_tested_sources if item.matched_test_files <= 0)
    for item in file_stats:
        if item.tests <= 0 and item.expect_count <= 0 and item.assert_count <= 0:
            continue
        area = item.test_area if item.test_area is not None else item.primary_area
        tests_by_area[area] += item.tests
        test_files_by_area[area] += 1
        expect_by_area[area] += item.expect_count
        assert_by_area[area] += item.assert_count

    per_area = summarize(file_stats)
    scripts_slice = [item for item in file_stats if item.in_scripts_dir]
    scripts_dirs = discover_scripts_dirs([item.path for item in scripts_slice])

    ext_files: Counter[str] = Counter()
    ext_lines: Counter[str] = Counter()
    ext_blank: Counter[str] = Counter()
    ext_code: Counter[str] = Counter()
    ext_comment: Counter[str] = Counter()
    ext_doc: Counter[str] = Counter()
    ext_min_lines: dict[str, int] = {}
    ext_max_lines: dict[str, int] = {}
    area_ext_files: dict[str, Counter[str]] = {
        area: Counter() for area in (*PRIMARY_ROOTS, "scripts_only")
    }
    area_ext_lines: dict[str, Counter[str]] = {
        area: Counter() for area in (*PRIMARY_ROOTS, "scripts_only")
    }
    area_ext_blank: dict[str, Counter[str]] = {
        area: Counter() for area in (*PRIMARY_ROOTS, "scripts_only")
    }
    area_ext_code: dict[str, Counter[str]] = {
        area: Counter() for area in (*PRIMARY_ROOTS, "scripts_only")
    }
    area_ext_comment: dict[str, Counter[str]] = {
        area: Counter() for area in (*PRIMARY_ROOTS, "scripts_only")
    }
    area_ext_doc: dict[str, Counter[str]] = {
        area: Counter() for area in (*PRIMARY_ROOTS, "scripts_only")
    }

    for item in file_stats:
        ext_files[item.extension] += 1
        ext_lines[item.extension] += item.lines
        ext_blank[item.extension] += item.blank_lines
        ext_code[item.extension] += item.code_lines
        ext_comment[item.extension] += item.comment_lines
        ext_doc[item.extension] += item.doc_lines
        current_min = ext_min_lines.get(item.extension)
        if current_min is None or item.lines < current_min:
            ext_min_lines[item.extension] = item.lines
        current_max = ext_max_lines.get(item.extension)
        if current_max is None or item.lines > current_max:
            ext_max_lines[item.extension] = item.lines

        area = item.primary_area
        area_ext_files[area][item.extension] += 1
        area_ext_lines[area][item.extension] += item.lines
        area_ext_blank[area][item.extension] += item.blank_lines
        area_ext_code[area][item.extension] += item.code_lines
        area_ext_comment[area][item.extension] += item.comment_lines
        area_ext_doc[area][item.extension] += item.doc_lines

    extension_rows = []
    for extension, line_count in sorted(
        ext_lines.items(),
        key=lambda pair: (-pair[1], pair[0]),
    ):
        files_count = ext_files[extension]
        avg_for_ext = line_count / files_count if files_count else 0.0
        min_for_ext = ext_min_lines.get(extension, 0)
        max_for_ext = ext_max_lines.get(extension, 0)
        blank_count = ext_blank[extension]
        extension_rows.append(
            f"| `{extension}` | {format_int(files_count)} | {format_int(line_count)} | {format_int(blank_count)} | {format_int(avg_for_ext)} | {format_int(min_for_ext)} | {format_int(max_for_ext)} |"
        )

    comment_doc_area_rows = []
    for area in (*PRIMARY_ROOTS, "scripts_only"):
        area_stat = per_area[area]
        area_non_blank = area_stat.lines - area_stat.blank_lines
        area_comment_den = area_stat.code_lines + area_stat.comment_lines
        comment_doc_area_rows.append(
            f"| `{area}` | {format_int(area_stat.files)} | {format_int(area_stat.code_lines)} | {format_int(area_stat.comment_lines)} | {format_int(area_stat.doc_lines)} | {format_int(area_stat.blank_lines)} | {format_pct(area_stat.comment_lines, area_comment_den)} | {format_pct(area_stat.doc_lines, area_non_blank)} |"
        )

    comment_doc_ext_rows = []
    for extension, line_count in sorted(
        ext_lines.items(),
        key=lambda pair: (-pair[1], pair[0]),
    ):
        files_count = ext_files[extension]
        code_count = ext_code[extension]
        comment_count = ext_comment[extension]
        doc_count = ext_doc[extension]
        blank_count = ext_blank[extension]
        non_blank_count = line_count - blank_count
        comment_doc_ext_rows.append(
            f"| `{extension}` | {format_int(files_count)} | {format_int(code_count)} | {format_int(comment_count)} | {format_int(doc_count)} | {format_int(blank_count)} | {format_pct(comment_count, code_count + comment_count)} | {format_pct(doc_count, non_blank_count)} |"
        )

    largest_files = sorted(
        file_stats,
        key=lambda item: (-item.lines, str(item.path)),
    )[:TOP_FILES_LIMIT]

    most_commented_files = [
        item
        for item in sorted(
            file_stats,
            key=lambda stat: (-stat.comment_lines, -stat.lines, str(stat.path)),
        )
        if item.comment_lines > 0
    ][:TOP_FILES_LIMIT]

    most_documented_files = [
        item
        for item in sorted(
            file_stats,
            key=lambda stat: (-stat.doc_lines, -stat.lines, str(stat.path)),
        )
        if item.doc_lines > 0
    ][:TOP_FILES_LIMIT]

    comment_density_files: list[FileStat] = []
    for item in sorted(
        file_stats,
        key=lambda stat: (
            -(
                stat.comment_lines / (stat.code_lines + stat.comment_lines)
                if (stat.code_lines + stat.comment_lines) > 0
                else -1.0
            ),
            -stat.comment_lines,
            str(stat.path),
        ),
    ):
        denominator = item.code_lines + item.comment_lines
        if denominator < MIN_NON_BLANK_FOR_DENSITY:
            continue
        if item.comment_lines <= 0:
            continue
        comment_density_files.append(item)
        if len(comment_density_files) >= TOP_FILES_LIMIT:
            break

    assertion_area_rows = []
    assertion_areas = sorted(
        set(tests_by_area.keys()) | set(expect_by_area.keys()) | set(assert_by_area.keys()),
        key=lambda area: (-(expect_by_area[area] + assert_by_area[area]), area),
    )
    for area in assertion_areas:
        expect_count = expect_by_area[area]
        assert_count = assert_by_area[area]
        checks_count = expect_count + assert_count
        area_tests = tests_by_area.get(area, 0)
        assertion_area_rows.append(
            f"| `{area}` | {format_int(expect_count)} | {format_int(assert_count)} | {format_int(checks_count)} | {format_int(area_tests)} | {format_ratio(checks_count, area_tests)} |"
        )

    top_assertion_files = [
        item
        for item in sorted(
            file_stats,
            key=lambda stat: (
                -(stat.expect_count + stat.assert_count),
                -stat.expect_count,
                -stat.assert_count,
                str(stat.path),
            ),
        )
        if (item.expect_count + item.assert_count) > 0
    ][:TOP_FILES_LIMIT]

    top_assertion_macros = assertion_macro_counts.most_common(TOP_FILES_LIMIT)

    weak_test_files = [
        item
        for item in sorted(
            file_stats,
            key=lambda stat: (
                (
                    (stat.expect_count + stat.assert_count) / stat.tests
                    if stat.tests > 0
                    else float("inf")
                ),
                -(stat.tests),
                str(stat.path),
            ),
        )
        if item.tests > 0
    ]
    weak_test_files = [item for item in weak_test_files if item.tests >= 2][:TOP_FILES_LIMIT]
    tests_without_checks = [
        item
        for item in file_stats
        if item.tests > 0 and (item.expect_count + item.assert_count) == 0
    ]

    complexity_area_rows = []
    for area in (*PRIMARY_ROOTS, "scripts_only"):
        area_items = [item for item in file_stats if item.primary_area == area]
        area_functions = sum(item.function_count for item in area_items)
        area_function_lines = sum(item.function_line_total for item in area_items)
        area_cyclomatic = sum(item.cyclomatic_estimate for item in area_items)
        area_complexity_files = sum(1 for item in area_items if item.cyclomatic_estimate > 0)
        complexity_area_rows.append(
            f"| `{area}` | {format_int(area_functions)} | {format_ratio(area_function_lines, area_functions)} | {format_int(max((item.max_function_lines for item in area_items), default=0))} | {format_int(area_cyclomatic)} | {format_ratio(area_cyclomatic, area_complexity_files)} |"
        )

    complex_files = [
        item
        for item in sorted(
            file_stats,
            key=lambda stat: (
                -stat.cyclomatic_estimate,
                -stat.max_function_lines,
                str(stat.path),
            ),
        )
        if item.cyclomatic_estimate > 0
    ][:TOP_FILES_LIMIT]

    debt_area_rows = []
    for area in (*PRIMARY_ROOTS, "scripts_only"):
        area_items = [item for item in file_stats if item.primary_area == area]
        debt_area_rows.append(
            f"| `{area}` | {format_int(sum(item.debt_count for item in area_items))} | {format_int(sum(item.debt_todo for item in area_items))} | {format_int(sum(item.debt_fixme for item in area_items))} | {format_int(sum(item.debt_xxx for item in area_items))} | {format_int(sum(item.debt_hack for item in area_items))} | {format_int(sum(item.debt_bug for item in area_items))} |"
        )

    debt_files = [
        item
        for item in sorted(
            file_stats,
            key=lambda stat: (-stat.debt_count, -stat.lines, str(stat.path)),
        )
        if item.debt_count > 0
    ][:TOP_FILES_LIMIT]

    api_area_rows = []
    for area in ("encoder_cli", "rtp_server", "libvcai"):
        area_items = [item for item in file_stats if item.path.parts and item.path.parts[0] == area]
        area_decl = sum(item.api_declarations for item in area_items)
        area_doc = sum(item.api_documented for item in area_items)
        api_area_rows.append(
            f"| `{area}` | {format_int(area_decl)} | {format_int(area_doc)} | {format_pct(area_doc, area_decl)} |"
        )

    api_undocumented_headers = [
        item
        for item in sorted(
            file_stats,
            key=lambda stat: (
                -(stat.api_declarations - stat.api_documented),
                -stat.api_declarations,
                str(stat.path),
            ),
        )
        if item.api_declarations > item.api_documented
    ][:TOP_FILES_LIMIT]

    area_rows = []
    for area in (*PRIMARY_ROOTS, "scripts_only"):
        area_stat = per_area[area]
        avg_for_area = (area_stat.lines / area_stat.files) if area_stat.files else 0.0
        area_rows.append(
            f"| `{area}` | {format_int(area_stat.files)} | {format_int(area_stat.lines)} | {format_int(avg_for_area)} |"
        )

    scripts_files = len(scripts_slice)
    scripts_lines = sum(item.lines for item in scripts_slice)
    scripts_avg = (scripts_lines / scripts_files) if scripts_files else 0.0

    lines: list[str] = [
        "# Codebase Statistics Report",
        "",
        "## Snapshot",
        f"- Generated (Local): {now_local}",
        f"- Generated (UTC): {now_utc}",
        f"- Repository: `{repo_root.name}`",
        f"- Branch: `{branch}`",
        f"- Commit: `{commit}`",
        "",
        "## Scope",
        "- Primary roots: `encoder_cli`, `rtp_server`, `libvcai`, `test`, `docs`, `manual` (when present)",
        "- Additional scope: all directories named `scripts` anywhere in the repository",
        "- Exclusions: resolved by Git using `.gitignore` (`git ls-files --exclude-standard`); `manual/` is scanned from the filesystem when it exists",
        "- Path filter: excludes files under directories named `test_data` or `tmp`",
        "- File-type filter: source/script/config/doc files only; generic `.txt` data files are excluded except explicitly allowed build files",
        "",
        "## Legend",
        "- `Files`: number of analyzed files in the scope or bucket",
        "- `Lines`: physical line count (includes code, comments, docs, and blank lines)",
        "- `Code`: lines containing non-comment language tokens",
        "- `Comments`: lines containing comment tokens (can overlap with `Code` for inline comments)",
        "- `Docs`: lines treated as documentation (for example doc-comments/docstrings/markdown prose)",
        "- `Blank`: empty or whitespace-only lines",
        "- `Comment density`: `Comments / (Code + Comments)`",
        "- `Doc density`: `Docs / (Lines - Blank)`",
        "- `EXPECT`: count of `EXPECT_*` style checks in test files",
        "- `ASSERT`: count of `ASSERT_*` style checks in test files",
        "- `Checks/test`: `(EXPECT + ASSERT) / test declarations`",
        "- `Matched checks` / `Matched tests` / `Matched test files`: heuristic source-to-test linkage by normalized filename stems (same area plus `test/common`); use as a signal, not exact coverage",
        "- `Cyclomatic`: estimated branch complexity using control-flow tokens/nodes",
        "- `Function avg length`: `function_line_total / function_count`",
        "- `Debt`: count of TODO/FIXME/XXX/HACK/BUG markers in comment-like text",
        "- `API coverage`: documented public declarations / total public declarations in `include` headers",
        "- `Refactor-ready cluster`: a high-similarity function-like duplicate group across multiple files",
        "- `scripts_only`: files in scoped `scripts` directories that are not under a primary root",
        "- Extension table `Min lines` / `Max lines`: minimum/maximum full-file line counts for that extension",
        "",
        "## Overall Totals",
        f"- Files analyzed: {format_int(total_files)}",
        f"- Total lines: {format_int(total_lines)}",
        f"- Average lines per file: {format_int(avg_lines)}",
        f"- Median lines per file: {format_int(float(median_lines))}",
        (
            f"- Min lines file: {format_int(min_item.lines)} (`{min_item.path.as_posix()}`)"
            if min_item
            else "- Min lines file: n/a"
        ),
        (
            f"- Max lines file: {format_int(max_item.lines)} (`{max_item.path.as_posix()}`)"
            if max_item
            else "- Max lines file: n/a"
        ),
        f"- Skipped by path filter: {format_int(skipped_by_path)}",
        f"- Skipped by file-type filter: {format_int(skipped_by_type)}",
        f"- Skipped binary files: {format_int(skipped_binary)}",
        f"- Skipped unreadable files: {format_int(skipped_errors)}",
        "",
        "## Comments and Documentation",
        f"- Code lines: {format_int(total_code)}",
        f"- Comment lines: {format_int(total_comment)}",
        f"- Documentation lines: {format_int(total_doc)}",
        f"- Blank lines: {format_int(total_blank)}",
        f"- Comment density (comments / (code + comments)): {format_pct(total_comment, total_code + total_comment)}",
        f"- Documentation density (docs / non-blank): {format_pct(total_doc, total_non_blank)}",
        "",
        "### Comment/Doc Breakdown Per Area",
        "",
        "| Area | Files | Code | Comments | Docs | Blank | Comment density | Doc density |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
        *comment_doc_area_rows,
        "",
        "### Comment/Doc Breakdown Per Extension",
        "",
        "| Extension | Files | Code | Comments | Docs | Blank | Comment density | Doc density |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    if comment_doc_ext_rows:
        lines.extend(comment_doc_ext_rows)
    else:
        lines.append("| `(none)` | 0 | 0 | 0 | 0 | 0 | 0.00% | 0.00% |")

    lines.extend(
        [
            "",
            f"### Most Commented Files (Top {TOP_FILES_LIMIT})",
            "",
            "| File | Comment lines | Comment density |",
            "|---|---:|---:|",
        ]
    )
    if most_commented_files:
        for item in most_commented_files:
            comment_den = item.code_lines + item.comment_lines
            lines.append(
                f"| `{item.path.as_posix()}` | {format_int(item.comment_lines)} | {format_pct(item.comment_lines, comment_den)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0.00% |")

    lines.extend(
        [
            "",
            (
                f"### Highest Comment Density Files (Top {TOP_FILES_LIMIT}, "
                f"minimum {MIN_NON_BLANK_FOR_DENSITY} code+comment lines)"
            ),
            "",
            "| File | Code lines | Comment lines | Comment density |",
            "|---|---:|---:|---:|",
        ]
    )
    if comment_density_files:
        for item in comment_density_files:
            comment_den = item.code_lines + item.comment_lines
            lines.append(
                f"| `{item.path.as_posix()}` | {format_int(item.code_lines)} | {format_int(item.comment_lines)} | {format_pct(item.comment_lines, comment_den)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0 | 0.00% |")

    lines.extend(
        [
            "",
            f"### Most Documented Files (Top {TOP_FILES_LIMIT})",
            "",
            "| File | Doc lines | Doc density |",
            "|---|---:|---:|",
        ]
    )
    if most_documented_files:
        for item in most_documented_files:
            non_blank = item.lines - item.blank_lines
            lines.append(
                f"| `{item.path.as_posix()}` | {format_int(item.doc_lines)} | {format_pct(item.doc_lines, non_blank)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0.00% |")

    lines.extend(
        [
            "",
            "## Tests",
            f"- Total test declarations detected: {format_int(total_tests)}",
            f"- Files containing tests: {format_int(test_files)}",
            "",
            "### Tests Per Area",
            "",
            "| Area | Tests |",
            "|---|---:|",
        ]
    )
    if tests_by_area:
        for area, tests_count in sorted(
            tests_by_area.items(),
            key=lambda pair: (-pair[1], pair[0]),
        ):
            lines.append(f"| `{area}` | {format_int(tests_count)} |")
    else:
        lines.append("| `(none)` | 0 |")

    lines.extend(
        [
            "",
            "### Test Files Per Area",
            "",
            "| Area | Files containing tests |",
            "|---|---:|",
        ]
    )
    if test_files_by_area:
        for area, files_count in sorted(
            test_files_by_area.items(),
            key=lambda pair: (-pair[1], pair[0]),
        ):
            lines.append(f"| `{area}` | {format_int(files_count)} |")
    else:
        lines.append("| `(none)` | 0 |")

    lines.extend(
        [
            "",
            "### Test Strength Metrics",
            f"- Files with tests but zero checks: {format_int(len(tests_without_checks))}",
            f"- Average checks per test declaration: {format_ratio(total_checks, total_tests)}",
            f"- Average checks per test file: {format_ratio(total_checks, test_files)}",
            "",
            f"### Weakest Test Files By Checks/Test (Top {TOP_FILES_LIMIT})",
            "",
            "| File | Tests | EXPECT | ASSERT | Checks/test |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    if weak_test_files:
        for item in weak_test_files:
            checks = item.expect_count + item.assert_count
            lines.append(
                f"| `{item.path.as_posix()}` | {format_int(item.tests)} | {format_int(item.expect_count)} | {format_int(item.assert_count)} | {format_ratio(checks, item.tests)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0 | 0 | 0.00 |")

    lines.extend(
        [
            "",
            "## Assertions and Expectations",
            f"- Total `EXPECT_*` checks: {format_int(total_expect)}",
            f"- Total `ASSERT_*` checks: {format_int(total_assert)}",
            f"- Total checks (`EXPECT + ASSERT`): {format_int(total_checks)}",
            f"- Checks per test declaration: {format_ratio(total_checks, total_tests)}",
            f"- Checks per file with tests: {format_ratio(total_checks, test_files)}",
            "",
            "### Checks Per Area",
            "",
            "| Area | EXPECT | ASSERT | Total checks | Tests | Checks/test |",
            "|---|---:|---:|---:|---:|---:|",
        ]
    )
    if assertion_area_rows:
        lines.extend(assertion_area_rows)
    else:
        lines.append("| `(none)` | 0 | 0 | 0 | 0 | 0.00 |")

    lines.extend(
        [
            "",
            f"### Files With Most Checks (Top {TOP_FILES_LIMIT})",
            "",
            "| File | EXPECT | ASSERT | Total checks |",
            "|---|---:|---:|---:|",
        ]
    )
    if top_assertion_files:
        for item in top_assertion_files:
            lines.append(
                f"| `{item.path.as_posix()}` | {format_int(item.expect_count)} | {format_int(item.assert_count)} | {format_int(item.expect_count + item.assert_count)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0 | 0 |")

    lines.extend(
        [
            "",
            f"### Most Used Check Macros (Top {TOP_FILES_LIMIT})",
            "",
            "| Macro | Count |",
            "|---|---:|",
        ]
    )
    if top_assertion_macros:
        for macro_name, macro_count in top_assertion_macros:
            lines.append(f"| `{macro_name}` | {format_int(macro_count)} |")
    else:
        lines.append("| `(none)` | 0 |")

    lines.extend(
        [
            "",
            "## Untested Source Candidates",
            f"- Candidate source files without heuristic test match: {format_int(untested_source_count)}",
            "- Heuristic: source and test filenames are stem-normalized and matched by equality/containment within the same component area plus `test/common`.",
            "",
            f"### Least-Tested Source Files (Top {TOP_FILES_LIMIT})",
            "",
            "| Source file | Matched checks | Matched tests | Matched test files | Lines |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    if least_tested_sources:
        for item in least_tested_sources[:TOP_FILES_LIMIT]:
            lines.append(
                f"| `{item.source.path.as_posix()}` | {format_int(item.matched_checks)} | {format_int(item.matched_tests)} | {format_int(item.matched_test_files)} | {format_int(item.source.lines)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0 | 0 | 0 |")

    lines.extend(
        [
            "",
            "## Complexity Metrics",
            f"- Estimated cyclomatic complexity (total): {format_int(total_cyclomatic)}",
            f"- Files with complexity estimates: {format_int(complexity_file_count)}",
            f"- Functions detected: {format_int(total_functions)}",
            f"- Average function length: {format_ratio(total_function_lines, total_functions)}",
            f"- Max function length: {format_int(max_function_lines)}",
            "",
            "### Complexity Per Area",
            "",
            "| Area | Functions | Avg function length | Max function length | Cyclomatic | Avg cyclomatic/file |",
            "|---|---:|---:|---:|---:|---:|",
        ]
    )
    lines.extend(complexity_area_rows)

    lines.extend(
        [
            "",
            f"### Most Complex Files (Top {TOP_FILES_LIMIT})",
            "",
            "| File | Cyclomatic | Functions | Avg function length | Max function length |",
            "|---|---:|---:|---:|---:|",
        ]
    )
    if complex_files:
        for item in complex_files:
            lines.append(
                f"| `{item.path.as_posix()}` | {format_int(item.cyclomatic_estimate)} | {format_int(item.function_count)} | {format_ratio(item.function_line_total, item.function_count)} | {format_int(item.max_function_lines)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0 | 0.00 | 0 |")

    lines.extend(
        [
            "",
            "## Duplication Metrics (Refactor-Ready)",
            f"- Files analyzed for duplication: {format_int(int(duplication_metrics.get('analyzed_files', 0)))}",
            f"- Function-like units considered: {format_int(int(duplication_metrics.get('units_considered', 0)))}",
            f"- Candidate unit pairs: {format_int(int(duplication_metrics.get('candidate_pairs', 0)))}",
            f"- Pairs evaluated for similarity: {format_int(int(duplication_metrics.get('pairs_evaluated', 0)))}",
            f"- Refactor-ready clusters: {format_int(int(duplication_metrics.get('refactor_clusters', 0)))}",
            f"- Estimated extractable lines: {format_int(int(duplication_metrics.get('estimated_extractable_lines', 0)))}",
            "",
            f"### Top Refactor-Ready Clone Clusters (Top {TOP_FILES_LIMIT})",
            "",
            "| Avg similarity | Members | Files | Common lines | Est. extractable lines | Example locations |",
            "|---:|---:|---:|---:|---:|---|",
        ]
    )
    top_clusters = duplication_metrics.get("top_clusters", [])
    if top_clusters:
        for cluster in top_clusters[:TOP_FILES_LIMIT]:
            locations = ", ".join(cluster.get("locations", []))
            lines.append(
                f"| {float(cluster.get('avg_similarity', 0.0)):.2f} | {format_int(int(cluster.get('members', 0)))} | {format_int(int(cluster.get('files', 0)))} | {format_int(int(cluster.get('common_lines', 0)))} | {format_int(int(cluster.get('estimated_savings', 0)))} | `{locations}` |"
            )
    else:
        lines.append("| 0.00 | 0 | 0 | 0 | 0 | `(none)` |")

    lines.extend(
        [
            "",
            f"### Files With Highest Refactor Potential (Top {TOP_FILES_LIMIT})",
            "",
            "| File | Estimated extractable lines |",
            "|---|---:|",
        ]
    )
    top_dup_files = duplication_metrics.get("top_files", [])
    if top_dup_files:
        for file_path, dup_lines in top_dup_files[:TOP_FILES_LIMIT]:
            lines.append(f"| `{file_path.as_posix()}` | {format_int(dup_lines)} |")
    else:
        lines.append("| `(none)` | 0 |")

    lines.extend(
        [
            "",
            "## TODO/FIXME Debt",
            f"- Total debt markers: {format_int(total_debt)}",
            "",
            "### Debt Per Area",
            "",
            "| Area | Total | TODO | FIXME | XXX | HACK | BUG |",
            "|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    lines.extend(debt_area_rows)

    lines.extend(
        [
            "",
            f"### Files With Most Debt (Top {TOP_FILES_LIMIT})",
            "",
            "| File | Total debt | TODO | FIXME | XXX | HACK | BUG |",
            "|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    if debt_files:
        for item in debt_files:
            lines.append(
                f"| `{item.path.as_posix()}` | {format_int(item.debt_count)} | {format_int(item.debt_todo)} | {format_int(item.debt_fixme)} | {format_int(item.debt_xxx)} | {format_int(item.debt_hack)} | {format_int(item.debt_bug)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0 | 0 | 0 | 0 | 0 |")

    lines.extend(
        [
            "",
            "## Public API Doc Coverage",
            f"- Public declarations in `include` headers: {format_int(total_api_declarations)}",
            f"- Documented declarations: {format_int(total_api_documented)}",
            f"- Coverage: {format_pct(total_api_documented, total_api_declarations)}",
            "",
            "### Coverage Per Area",
            "",
            "| Area | Declarations | Documented | Coverage |",
            "|---|---:|---:|---:|",
        ]
    )
    lines.extend(api_area_rows)

    lines.extend(
        [
            "",
            f"### Most Undocumented Public Headers (Top {TOP_FILES_LIMIT})",
            "",
            "| Header | Undocumented declarations | Total declarations | Coverage |",
            "|---|---:|---:|---:|",
        ]
    )
    if api_undocumented_headers:
        for item in api_undocumented_headers:
            undocumented = item.api_declarations - item.api_documented
            lines.append(
                f"| `{item.path.as_posix()}` | {format_int(undocumented)} | {format_int(item.api_declarations)} | {format_pct(item.api_documented, item.api_declarations)} |"
            )
    else:
        lines.append("| `(none)` | 0 | 0 | 0.00% |")

    lines.extend(
        [
            "",
            "## Per-Area Totals (Primary Buckets)",
            "",
            "| Area | Files | Lines | Avg lines/file |",
            "|---|---:|---:|---:|",
            *area_rows,
            "",
            "## Scripts Directories (Cross-Cutting)",
            f"- Files under any `scripts` directory: {format_int(scripts_files)}",
            f"- Lines under any `scripts` directory: {format_int(scripts_lines)}",
            f"- Average lines per file under `scripts`: {format_int(scripts_avg)}",
            "",
            f"- Discovered `scripts` directories ({format_int(len(scripts_dirs))}):",
        ]
    )
    if scripts_dirs:
        lines.extend(f"  - `{path.as_posix()}`" for path in scripts_dirs)
    else:
        lines.append("  - none")

    manual_html_files, manual_html_lines, manual_content_lines = manual_html_metrics
    lines.extend(["", "## Manual (Generated HTML)", ""])
    if manual_html_files > 0:
        lines.extend(
            [
                "- HTML files in `manual/`: " + format_int(manual_html_files),
                "- Lines containing HTML markup: " + format_int(manual_html_lines),
                "- Lines of actual/probable content (text): " + format_int(manual_content_lines),
                "",
            ]
        )
    else:
        lines.append("- No HTML files in `manual/` (directory absent or empty).")
        lines.append("")

    lines.extend(
        [
            "",
            "## Extension Breakdown",
            "",
            "| Extension | Files | Lines | Blank | Avg lines/file | Min lines | Max lines |",
            "|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    if extension_rows:
        lines.extend(extension_rows)
    else:
        lines.append("| `(none)` | 0 | 0 | 0 | 0.00 | 0 | 0 |")

    lines.extend(["", "## Extension Breakdown Per Area (Primary Buckets)"])
    for area in (*PRIMARY_ROOTS, "scripts_only"):
        lines.extend(
            [
                "",
                f"### `{area}`",
                "",
                "| Extension | Files | Lines | Code | Comments | Docs | Blank | Avg lines/file |",
                "|---|---:|---:|---:|---:|---:|---:|---:|",
            ]
        )
        per_area_lines = area_ext_lines[area]
        if per_area_lines:
            for extension, line_count in sorted(
                per_area_lines.items(),
                key=lambda pair: (-pair[1], pair[0]),
            ):
                files_count = area_ext_files[area][extension]
                avg_for_ext = line_count / files_count if files_count else 0.0
                lines.append(
                    f"| `{extension}` | {format_int(files_count)} | {format_int(line_count)} | {format_int(area_ext_code[area][extension])} | {format_int(area_ext_comment[area][extension])} | {format_int(area_ext_doc[area][extension])} | {format_int(area_ext_blank[area][extension])} | {format_int(avg_for_ext)} |"
                )
        else:
            lines.append("| `(none)` | 0 | 0 | 0 | 0 | 0 | 0 | 0.00 |")

    lines.extend(["", f"## Largest Files (Top {TOP_FILES_LIMIT})", ""])
    lines.extend(
        [
            "| File | Lines |",
            "|---|---:|",
        ]
    )
    if largest_files:
        lines.extend(
            f"| `{item.path.as_posix()}` | {format_int(item.lines)} |"
            for item in largest_files
        )
    else:
        lines.append("| `(none)` | 0 |")

    raw_report = "\n".join(lines) + "\n"
    return reorder_top_level_sections(raw_report)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        type=Path,
        default=REPORT_PATH,
        help="Output markdown file path (default: %(default)s)",
    )
    parser.add_argument(
        "--stdout",
        action="store_true",
        help="Also print the generated report to stdout",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    # When run via python3 - (stdin), __file__ is unset; walk up from cwd for .git.
    try:
        _f = __file__
    except NameError:
        _f = None
    if _f and _f != "<stdin>":
        repo_root = Path(_f).resolve().parents[1]
    else:
        cwd = Path.cwd().resolve()
        repo_root = cwd
        for parent in [cwd, *cwd.parents]:
            if (parent / ".git").exists():
                repo_root = parent
                break
        else:
            print(f"ERROR: {cwd} is not inside a git repository.", file=sys.stderr)
            return 1
    if not (repo_root / ".git").exists():
        print(f"ERROR: {repo_root} is not a git repository.", file=sys.stderr)
        return 1

    try:
        all_files = list_git_files(repo_root)
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    manual_paths = list_manual_files(repo_root)
    all_files = sorted(set(all_files) | set(manual_paths))

    scoped_files = [path for path in all_files if in_scope(path)]

    stats: list[FileStat] = []
    assertion_macro_counts: Counter[str] = Counter()
    skipped_by_path = 0
    skipped_by_type = 0
    skipped_binary = 0
    skipped_errors = 0
    manual_html_metrics: list[int] = [0, 0, 0]  # [html_files, html_lines, content_lines]

    for relative_path in scoped_files:
        absolute_path = repo_root / relative_path
        if not absolute_path.is_file():
            continue

        if is_excluded_path(relative_path):
            skipped_by_path += 1
            continue

        if not is_allowed_file_type(relative_path):
            skipped_by_type += 1
            continue

        if not is_probably_text_file(absolute_path):
            skipped_binary += 1
            continue

        try:
            content = absolute_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            skipped_errors += 1
            continue

        extension = absolute_path.suffix.lower() if absolute_path.suffix else "(no_ext)"
        tests_count = count_tests(content, extension)
        test_area = classify_test_area(relative_path)
        expect_count, assert_count, macro_counts = count_expect_assertions(
            path=absolute_path,
            extension=extension,
            content=content,
            test_area=test_area,
        )
        assertion_macro_counts.update(macro_counts)
        complexity = estimate_complexity(absolute_path, extension, content)
        debt = count_debt_markers(content, extension)
        api_declarations, api_documented = analyze_public_api_doc_coverage(
            relative_path,
            extension,
            content,
        )

        try:
            metrics = analyze_line_metrics(absolute_path, content)
        except Exception:
            skipped_errors += 1
            continue

        if relative_path.parts and relative_path.parts[0] == "manual" and extension == ".html":
            h_lines, c_lines = count_html_and_content_lines(content)
            manual_html_metrics[0] += 1
            manual_html_metrics[1] += h_lines
            manual_html_metrics[2] += c_lines

        stats.append(
            FileStat(
                path=relative_path,
                extension=extension,
                primary_area=classify_primary_area(relative_path),
                in_scripts_dir=("scripts" in relative_path.parts),
                test_area=test_area,
                tests=tests_count,
                expect_count=expect_count,
                assert_count=assert_count,
                function_count=complexity.function_count,
                function_line_total=complexity.function_line_total,
                max_function_lines=complexity.max_function_lines,
                cyclomatic_estimate=complexity.cyclomatic_estimate,
                debt_count=debt.total,
                debt_todo=debt.by_marker.get("TODO", 0),
                debt_fixme=debt.by_marker.get("FIXME", 0),
                debt_xxx=debt.by_marker.get("XXX", 0),
                debt_hack=debt.by_marker.get("HACK", 0),
                debt_bug=debt.by_marker.get("BUG", 0),
                api_declarations=api_declarations,
                api_documented=api_documented,
                lines=metrics.total_lines,
                blank_lines=metrics.blank_lines,
                code_lines=metrics.code_lines,
                comment_lines=metrics.comment_lines,
                doc_lines=metrics.doc_lines,
            )
        )

    least_tested_sources = rank_least_tested_source_files(stats)
    duplication_metrics = compute_duplication_metrics(repo_root, stats)

    report_text = build_report(
        repo_root=repo_root,
        file_stats=stats,
        assertion_macro_counts=assertion_macro_counts,
        duplication_metrics=duplication_metrics,
        least_tested_sources=least_tested_sources,
        skipped_by_path=skipped_by_path,
        skipped_by_type=skipped_by_type,
        skipped_binary=skipped_binary,
        skipped_errors=skipped_errors,
        manual_html_metrics=tuple(manual_html_metrics),
    )

    report_target = args.output
    report_absolute = report_target if report_target.is_absolute() else (repo_root / report_target)
    report_absolute.parent.mkdir(parents=True, exist_ok=True)
    try:
        report_absolute.write_text(report_text, encoding="utf-8")
    except OSError as error:
        print(f"ERROR: failed to write report to {report_absolute}: {error}", file=sys.stderr)
        return 1

    print(f"Wrote report to {report_absolute}")
    if args.stdout:
        print(report_text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
PY
