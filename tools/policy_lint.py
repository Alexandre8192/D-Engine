"""Lightweight policy lint for D-Engine.

Checks Source/Core (and optionally Source/Modules) for:
- forbidden exception-related tokens (throw/try/catch/dynamic_cast/typeid)
- raw new/delete expressions in Core code (placement new allowed)
- heavy STL includes (regex/filesystem/iostream/locale) unless whitelisted
- non-ASCII bytes in code files

Exit status is non-zero on any violation. Output is plain ASCII for CI.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, List, Set, Tuple

CODE_EXTENSIONS: Set[str] = {
    ".h",
    ".hpp",
    ".hxx",
    ".hh",
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".ipp",
    ".tpp",
    ".inl",
}

FORBIDDEN_TOKENS: List[str] = ["throw", "try", "catch", "dynamic_cast", "typeid"]

# Strict mode fails if any entry here is used.
TOKEN_ALLOWLIST: dict[str, Set[str]] = {}

HEAVY_INCLUDES: Set[str] = {
    "regex",
    "filesystem",
    "iostream",
    "locale",
}

HEAVY_INCLUDE_ALLOWLIST: dict[str, Set[str]] = {}

# Strict mode fails if any entry here is used.
CORE_TOKEN_ALLOWLIST: dict[str, Set[str]] = {
    "Source/Core/Memory/GlobalNewDelete.cpp": {"malloc", "free"},
}

STRICT_BLESSED_HITS: Set[Tuple[str, str]] = {
    ("Source/Core/Memory/GlobalNewDelete.cpp", "core:malloc"),
    ("Source/Core/Memory/GlobalNewDelete.cpp", "core:free"),
}

EXPR_NEW_PATTERN = re.compile(r"\bnew\b")
EXPR_DELETE_PATTERN = re.compile(r"\bdelete\b")

# Matches both #include <...> and #include "..."
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')

USING_NAMESPACE_PATTERN = re.compile(r"\busing\s+namespace\b")
SHARED_PTR_PATTERN = re.compile(r"std::shared_ptr\b")
WEAK_PTR_PATTERN = re.compile(r"std::weak_ptr\b")
ASSERT_PATTERN = re.compile(r"\bassert\s*\(")

# CRT allocation call detection (avoid member names like obj.free())
MALLOC_CALL_PATTERN = re.compile(r"(?<!->)(?<!\.)\bmalloc\s*\(")
FREE_CALL_PATTERN = re.compile(r"(?<!->)(?<!\.)\bfree\s*\(")
OTHER_ALLOC_PATTERNS = {
    "realloc": re.compile(r"(?<!->)(?<!\.)\brealloc\s*\("),
    "calloc": re.compile(r"(?<!->)(?<!\.)\bcalloc\s*\("),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="D-Engine policy lint")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parent.parent,
        help="Repository root (defaults to script/..)",
    )
    parser.add_argument(
        "--modules",
        action="store_true",
        help="Also scan Source/Modules in addition to Source/Core",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail if any allowlist entry is exercised while scanning",
    )
    parser.add_argument(
        "--no-allowlists",
        action="store_true",
        help="Fail immediately if any allowlist map is non-empty (cleanup mode)",
    )
    return parser.parse_args()


def iter_code_files(root: Path, include_modules: bool) -> Iterable[Path]:
    search_roots = [root / "Source" / "Core"]
    if include_modules:
        search_roots.append(root / "Source" / "Modules")

    for base in search_roots:
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix.lower() in CODE_EXTENSIONS:
                yield path


def rel(path: Path, root: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def is_core_path(path: str) -> bool:
    return path.startswith("Source/Core/")


def first_non_ascii(data: bytes) -> Tuple[int, int, int]:
    # Returns (bad_byte, line, col). If ok, returns (0, 0, 0).
    for idx, b in enumerate(data):
        if b > 0x7F:
            line = data.count(b"\n", 0, idx) + 1
            last_nl = data.rfind(b"\n", 0, idx)
            col = idx + 1 if last_nl == -1 else idx - last_nl
            return b, line, col
    return 0, 0, 0


def strip_comments_and_strings(text: str) -> List[str]:
    # NOTE: This is a lightweight sanitizer, not a full C/C++ parser.
    # It removes most comments and string/char literals to reduce false positives.
    lines = text.splitlines()
    out_lines: List[str] = []
    in_block = False
    in_string = False
    string_delim = ""

    for line in lines:
        i = 0
        out_chars: List[str] = []
        in_line_comment = False

        while i < len(line):
            ch = line[i]
            nxt = line[i + 1] if i + 1 < len(line) else ""

            if in_line_comment:
                out_chars.append(" ")
                i += 1
                continue

            if in_block:
                if ch == "*" and nxt == "/":
                    in_block = False
                    out_chars.extend([" ", " "])
                    i += 2
                else:
                    out_chars.append(" ")
                    i += 1
                continue

            if in_string:
                if ch == "\\":
                    # Skip escaped character.
                    out_chars.append(" ")
                    i += 2
                    continue
                if ch == string_delim:
                    in_string = False
                out_chars.append(" ")
                i += 1
                continue

            if ch == "/" and nxt == "/":
                in_line_comment = True
                out_chars.extend(" " * (len(line) - i))
                break

            if ch == "/" and nxt == "*":
                in_block = True
                out_chars.extend([" ", " "])
                i += 2
                continue

            if ch == '"' or ch == "'":
                in_string = True
                string_delim = ch
                out_chars.append(" ")
                i += 1
                continue

            out_chars.append(ch)
            i += 1

        out_lines.append("".join(out_chars))

    return out_lines


def strip_comments_only(text: str) -> List[str]:
    # Keep string contents intact for include parsing; only comments are blanked.
    lines = text.splitlines()
    out_lines: List[str] = []
    in_block = False

    for line in lines:
        i = 0
        out_chars: List[str] = []
        in_line_comment = False

        while i < len(line):
            ch = line[i]
            nxt = line[i + 1] if i + 1 < len(line) else ""

            if in_line_comment:
                out_chars.append(" ")
                i += 1
                continue

            if in_block:
                if ch == "*" and nxt == "/":
                    in_block = False
                    out_chars.extend([" ", " "])
                    i += 2
                else:
                    out_chars.append(" ")
                    i += 1
                continue

            if ch == "/" and nxt == "/":
                in_line_comment = True
                out_chars.extend(" " * (len(line) - i))
                break

            if ch == "/" and nxt == "*":
                in_block = True
                out_chars.extend([" ", " "])
                i += 2
                continue

            out_chars.append(ch)
            i += 1

        out_lines.append("".join(out_chars))

    return out_lines


def is_token_allowed(path: str, token: str) -> bool:
    allowed = TOKEN_ALLOWLIST.get(path)
    return allowed is not None and token in allowed


def check_forbidden_tokens(path: str, lines: List[str], violations: List[Tuple[str, int, str]], allowlist_hits: List[Tuple[str, int, str]]) -> None:
    for token in FORBIDDEN_TOKENS:
        pattern = re.compile(rf"\b{re.escape(token)}\b")
        for lineno, line in enumerate(lines, start=1):
            if not pattern.search(line):
                continue
            if is_token_allowed(path, token):
                allowlist_hits.append((path, lineno, f"token:{token}"))
                continue
            violations.append((path, lineno, f"forbidden token '{token}'"))


def is_raw_new_line(line: str) -> bool:
    stripped = line.lstrip()
    if stripped.startswith("#include"):
        return False
    if "operator new" in line:
        return False

    # Allow placement new: new (ptr) T(...)
    # Flag aligned/nothrow forms (still raw allocation paths).
    if "new (" in line:
        inner = line.split("new", 1)[1]
        if "nothrow" in inner or "align_val" in inner or "align_val_t" in inner:
            return True
        return False

    return bool(EXPR_NEW_PATTERN.search(line))


def is_raw_delete_line(line: str) -> bool:
    stripped = line.lstrip()
    if stripped.startswith("#include"):
        return False
    if "operator delete" in line:
        return False
    # Allow "= delete" for deleted functions.
    if re.search(r"=\s*delete\b", line):
        return False
    return bool(EXPR_DELETE_PATTERN.search(line))


def check_alloc_tokens(path: str, lines: List[str], violations: List[Tuple[str, int, str]]) -> None:
    for lineno, line in enumerate(lines, start=1):
        if is_raw_new_line(line):
            violations.append((path, lineno, "raw new expression; use engine allocator or placement new"))
        if is_raw_delete_line(line):
            violations.append((path, lineno, "raw delete expression; ownership should use engine allocators"))


def check_heavy_includes(path: str, lines: List[str], violations: List[Tuple[str, int, str]], allowlist_hits: List[Tuple[str, int, str]]) -> None:
    allowed = HEAVY_INCLUDE_ALLOWLIST.get(path, set())
    for lineno, line in enumerate(lines, start=1):
        match = INCLUDE_PATTERN.search(line)
        if not match:
            continue

        header = match.group(1).strip()
        base = header.split("/")[-1]

        hit = None
        if header in HEAVY_INCLUDES:
            hit = header
        elif base in HEAVY_INCLUDES:
            hit = base

        if hit is None:
            continue

        if hit not in allowed:
            violations.append((path, lineno, f"heavy STL include '{header}' is not allowed"))
        else:
            allowlist_hits.append((path, lineno, f"include:{header}"))


def check_ascii(path: str, data: bytes, violations: List[Tuple[str, int, str]]) -> None:
    bad_byte, line, col = first_non_ascii(data)
    if bad_byte:
        violations.append((path, line, f"non-ASCII byte 0x{bad_byte:02X} at column {col}"))


def check_core_only_tokens(path: str, lines: List[str], violations: List[Tuple[str, int, str]], allowlist_hits: List[Tuple[str, int, str]]) -> None:
    allowed_tokens = CORE_TOKEN_ALLOWLIST.get(path, set())
    for lineno, line in enumerate(lines, start=1):
        if MALLOC_CALL_PATTERN.search(line):
            if "malloc" in allowed_tokens:
                allowlist_hits.append((path, lineno, "core:malloc"))
            else:
                violations.append((path, lineno, "forbidden token 'malloc' in Core; use engine allocators"))

        if FREE_CALL_PATTERN.search(line):
            if "free" in allowed_tokens:
                allowlist_hits.append((path, lineno, "core:free"))
            else:
                violations.append((path, lineno, "forbidden token 'free' in Core; use engine allocators"))

        for token, pattern in OTHER_ALLOC_PATTERNS.items():
            if pattern.search(line):
                violations.append((path, lineno, f"forbidden token '{token}' in Core; use engine allocators"))

        if USING_NAMESPACE_PATTERN.search(line):
            violations.append((path, lineno, "'using namespace' is banned in Core"))
        if SHARED_PTR_PATTERN.search(line):
            violations.append((path, lineno, "std::shared_ptr is banned in Core"))
        if WEAK_PTR_PATTERN.search(line):
            violations.append((path, lineno, "std::weak_ptr is banned in Core"))
        if ASSERT_PATTERN.search(line):
            violations.append((path, lineno, "use DNG_ASSERT/DNG_CHECK instead of assert() in Core"))


def check_relative_includes_core(path: str, lines: List[str], violations: List[Tuple[str, int, str]]) -> None:
    for lineno, line in enumerate(lines, start=1):
        match = INCLUDE_PATTERN.search(line)
        if not match:
            continue
        header = match.group(1).strip().replace("\\", "/")
        if header.startswith("../") or header.startswith("./") or "/../" in header or "/./" in header:
            violations.append((path, lineno, "relative include is banned in Core"))


def main() -> int:
    args = parse_args()
    root = args.root.resolve()

    if args.no_allowlists:
        non_empty: List[str] = []
        if TOKEN_ALLOWLIST:
            non_empty.append(f"TOKEN_ALLOWLIST={len(TOKEN_ALLOWLIST)}")
        if CORE_TOKEN_ALLOWLIST:
            non_empty.append(f"CORE_TOKEN_ALLOWLIST={len(CORE_TOKEN_ALLOWLIST)}")
        if HEAVY_INCLUDE_ALLOWLIST:
            non_empty.append(f"HEAVY_INCLUDE_ALLOWLIST={len(HEAVY_INCLUDE_ALLOWLIST)}")
        if non_empty:
            print("--no-allowlists: allowlists present -> fail")
            for entry in non_empty:
                print(f"  {entry}")
            return 1

    violations: List[Tuple[str, int, str]] = []
    allowlist_hits: List[Tuple[str, int, str]] = []

    for file_path in iter_code_files(root, args.modules):
        rel_path = rel(file_path, root)
        data = file_path.read_bytes()

        check_ascii(rel_path, data, violations)

        text = data.decode("utf-8", errors="replace")
        sanitized_lines = strip_comments_and_strings(text)
        comment_only_lines = strip_comments_only(text)

        check_forbidden_tokens(rel_path, sanitized_lines, violations, allowlist_hits)
        check_alloc_tokens(rel_path, sanitized_lines, violations)
        # Include checks keep string literals so INCLUDE_PATTERN can see the header path.
        check_heavy_includes(rel_path, comment_only_lines, violations, allowlist_hits)
        if is_core_path(rel_path):
            check_core_only_tokens(rel_path, sanitized_lines, violations, allowlist_hits)
            check_relative_includes_core(rel_path, comment_only_lines, violations)

    exit_code = 0

    if violations:
        for path, lineno, message in sorted(violations):
            print(f"{path}:{lineno}: {message}")
        print(f"\n{len(violations)} violation(s) found.")
        exit_code = 1

    if args.strict:
        unblessed = [(path, lineno, reason) for path, lineno, reason in allowlist_hits if (path, reason) not in STRICT_BLESSED_HITS]
        if unblessed:
            print("Strict mode: unblessed allowlist entries were used:")
            for path, lineno, reason in sorted(unblessed):
                print(f"  {path}:{lineno}: allowlisted {reason}")
            exit_code = 1
        elif allowlist_hits:
            print("Strict mode: only blessed allowlist entries were used.")
    elif allowlist_hits:
        print("Warning: allowlist entries were used (non-strict mode):")
        for path, lineno, reason in sorted(allowlist_hits):
            print(f"  {path}:{lineno}: allowlisted {reason}")

    if exit_code != 0:
        return exit_code

    print("policy_lint: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
