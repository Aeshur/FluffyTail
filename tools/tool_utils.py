"""Small, shared command-line helpers for the private DAT release tools."""

from __future__ import annotations

import argparse
from collections.abc import Iterable
from pathlib import Path


def dat_files(root: Path) -> list[Path]:
    """Return the supported ``<folder>/*.DAT`` inputs, rejecting an empty set."""

    if not root.is_dir():
        raise ValueError(f"input directory does not exist: {root}")
    paths = sorted(root.glob("*/*.DAT"))
    if not paths:
        raise ValueError(f"input directory contains no DAT files: {root}")
    return paths


def parse_rgb(value: str) -> tuple[int, int, int]:
    """Parse exactly six hexadecimal digits, with or without a leading ``#``."""

    text = value.removeprefix("#")
    if len(text) != 6:
        raise argparse.ArgumentTypeError("color must be exactly six hex digits (RRGGBB)")
    try:
        return tuple(int(text[index : index + 2], 16) for index in (0, 2, 4))  # type: ignore[return-value]
    except ValueError as error:
        raise argparse.ArgumentTypeError("color must be exactly six hex digits (RRGGBB)") from error


def texture_name(value: str) -> bytes:
    """Encode an ASCII DAT texture name and reject values that would be truncated."""

    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as error:
        raise argparse.ArgumentTypeError("texture name must contain ASCII bytes") from error
    if not encoded or len(encoded) > 8:
        raise argparse.ArgumentTypeError("texture name must contain one to eight bytes")
    return encoded


def ensure_output(path: Path, *, force: bool = False) -> None:
    """Allow a new or empty output directory, requiring ``--force`` to reuse files."""

    if path.exists() and not path.is_dir():
        raise ValueError(f"output path is not a directory: {path}")
    if path.is_dir() and any(path.iterdir()) and not force:
        raise ValueError(
            f"output directory is not empty; choose another path or pass --force: {path}"
        )
    path.mkdir(parents=True, exist_ok=True)


def add_force(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--force",
        action="store_true",
        help="reuse an output directory that already contains files",
    )


def format_summary(
    *,
    input_count: int,
    output_count: int,
    skipped_count: int,
    failure_count: int,
    output: Path,
) -> str:
    return (
        f"inputs={input_count} outputs={output_count} skipped={skipped_count} "
        f"failures={failure_count} output={output}"
    )


def print_failures(failures: Iterable[tuple[Path | str, str]]) -> None:
    for path, reason in failures:
        print(f"FAIL {path}: {reason}")
