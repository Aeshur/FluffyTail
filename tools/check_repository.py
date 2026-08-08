#!/usr/bin/env python3
"""Run read-only repository hygiene checks with actionable path diagnostics."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path

MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
TEXT_SUFFIXES = {
    ".c",
    ".cpp",
    ".def",
    ".h",
    ".hpp",
    ".ini",
    ".json",
    ".md",
    ".ps1",
    ".py",
    ".toml",
    ".txt",
    ".yaml",
    ".yml",
}
SKIP_PREFIXES = ("docs/ai_agents/",)
SKIP_FILES = {"LICENSE.GPL.txt", "LICENSE.md"}


def tracked_files(root: Path) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=root,
        check=True,
        capture_output=True,
    )
    return [root / item for item in result.stdout.decode().split("\0") if item]


def check_ascii(paths: list[Path], root: Path) -> list[str]:
    errors = []
    for path in paths:
        relative = path.relative_to(root).as_posix()
        if path.suffix.lower() not in TEXT_SUFFIXES or relative in SKIP_FILES:
            continue
        if any(relative.startswith(prefix) for prefix in SKIP_PREFIXES):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        for line_number, line in enumerate(text.splitlines(), 1):
            if not line.isascii():
                errors.append(f"{relative}:{line_number}: non-ASCII authored text")
    return errors


def check_markdown_links(paths: list[Path], root: Path) -> list[str]:
    errors = []
    for path in paths:
        relative = path.relative_to(root).as_posix()
        if path.suffix.lower() != ".md" or relative in SKIP_FILES:
            continue
        if any(relative.startswith(prefix) for prefix in SKIP_PREFIXES):
            continue
        text = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(text.splitlines(), 1):
            for target in MARKDOWN_LINK.findall(line):
                target = target.split("#", 1)[0]
                if not target or "://" in target or target.startswith("mailto:"):
                    continue
                resolved = (path.parent / target).resolve()
                if not resolved.is_file():
                    errors.append(f"{relative}:{line_number}: missing relative link {target}")
    return errors


def check_json(paths: list[Path], root: Path) -> list[str]:
    errors = []
    for path in paths:
        if path.suffix.lower() != ".json":
            continue
        relative = path.relative_to(root).as_posix()
        try:
            json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            errors.append(f"{relative}: invalid JSON: {error}")
    return errors


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Check tracked text, links, and JSON without modifying files."
    )
    parser.add_argument(
        "root", nargs="?", type=Path, default=Path(__file__).resolve().parent.parent
    )
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        paths = tracked_files(root)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"FAIL {root}: cannot enumerate tracked files: {error}")
        return 2
    errors = check_ascii(paths, root) + check_markdown_links(paths, root) + check_json(paths, root)
    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    print(f"checked tracked_files={len(paths)} root={root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
