#!/usr/bin/env python3
"""Regenerate the currently covered overlay paths from a vanilla FFXI install."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from . import splice_tail as splice_tool
    from .tool_utils import add_force, ensure_output
except ImportError:  # Direct ``python tools/regen_existing.py`` execution.
    import splice_tail as splice_tool
    from tool_utils import add_force, ensure_output


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Regenerate each covered body DAT for comparison with the local overlay."
    )
    parser.add_argument("install", type=Path, help="FFXI installation root containing ROM")
    parser.add_argument("output", type=Path, help="fresh output directory for regenerated DATs")
    add_force(parser)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    repository = Path(__file__).resolve().parent.parent
    source_root = repository / "ROM"
    if not args.install.is_dir():
        print(f"error: install root does not exist: {args.install}")
        return 2
    if not source_root.is_dir():
        print(f"error: covered ROM directory does not exist: {source_root}")
        return 2
    covered = sorted(source_root.glob("*/*.DAT"))
    if not covered:
        print(f"error: covered ROM directory contains no DAT files: {source_root}")
        return 2
    try:
        ensure_output(args.output, force=args.force)
    except ValueError as error:
        print(f"error: {error}")
        return 2

    failures: list[tuple[str, str]] = []
    output_count = 0
    for covered_path in covered:
        relative = covered_path.relative_to(repository)
        vanilla_path = args.install / relative
        try:
            data = splice_tool.splice(vanilla_path.read_bytes())
            splice_tool.verify(data)
            destination = args.output / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            output_count += 1
        except (OSError, ValueError, struct.error) as error:
            failures.append((relative.as_posix(), str(error)))

    print(
        f"inputs={len(covered)} outputs={output_count} skipped=0 failures={len(failures)} "
        f"output={args.output}"
    )
    for relative, reason in failures[:40]:
        print(f"FAIL {relative}: {reason}")
    return 1 if failures or output_count == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
