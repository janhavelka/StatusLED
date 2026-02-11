#!/usr/bin/env python3
"""Fail if tracked text files contain a UTF-8 BOM."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

BOM = b"\xEF\xBB\xBF"

TEXT_SUFFIXES = {
    ".c",
    ".cc",
    ".cmake",
    ".conf",
    ".cpp",
    ".css",
    ".csv",
    ".h",
    ".hpp",
    ".htm",
    ".html",
    ".ini",
    ".ino",
    ".js",
    ".json",
    ".md",
    ".properties",
    ".ps1",
    ".py",
    ".sh",
    ".toml",
    ".txt",
    ".xml",
    ".yaml",
    ".yml",
}

TEXT_FILENAMES = {
    ".clang-format",
    ".editorconfig",
    ".gitignore",
    "AGENTS.md",
    "CHANGELOG.md",
    "CODEOWNERS",
    "CONTRIBUTING.md",
    "LICENSE",
    "README.md",
    "SECURITY.md",
    "Makefile",
}

SKIP_PREFIXES = (".git/", ".pio/")


def tracked_files(repo_root: Path) -> list[str]:
    result = subprocess.run(
        ["git", "ls-files"],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError("Failed to list tracked files via git ls-files.")
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def is_text_file(path: Path) -> bool:
    return path.name in TEXT_FILENAMES or path.suffix.lower() in TEXT_SUFFIXES


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    bom_files: list[str] = []

    try:
        files = tracked_files(repo_root)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    for rel_path in files:
        if rel_path.startswith(SKIP_PREFIXES):
            continue

        file_path = repo_root / rel_path
        if not file_path.is_file() or not is_text_file(file_path):
            continue

        content = file_path.read_bytes()
        if content.startswith(BOM):
            bom_files.append(rel_path)

    if bom_files:
        print("UTF-8 BOM detected in tracked text files:")
        for rel_path in bom_files:
            print(f"- {rel_path}")
        return 1

    print("OK: no UTF-8 BOM found in tracked text files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
