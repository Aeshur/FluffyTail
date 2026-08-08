#!/usr/bin/env python3
"""Build one calibrated overlay folder for each supported tail color."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    from .colorize_tail import colorize, texture_chunk, verify
    from .tool_utils import add_force, dat_files, ensure_output
except ImportError:  # Direct ``python tools/build_colorset.py`` execution.
    from colorize_tail import colorize, texture_chunk, verify
    from tool_utils import add_force, dat_files, ensure_output

# Calibrated texture RGB and the corresponding Mithra face reference.
PALETTE: dict[str, tuple[str, str]] = {
    "black": ("000000", "-"),
    "white": ("d8d8ca", "M6A"),
    "silver": ("cdc2d4", "M2B"),
    "blonde": ("c3a261", "M8B"),
    "red": ("652708", "M6B"),
    "rose": ("814231", "M5A"),
    "brunette": ("3e2412", "M8A"),
}


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build the full calibrated FluffyTail color set.")
    parser.add_argument(
        "rom",
        nargs="?",
        type=Path,
        default=Path("ROM"),
        help="source ROM directory (default: ROM)",
    )
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path("work/colorset"),
        help="output directory (default: work/colorset)",
    )
    add_force(parser)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        paths = dat_files(args.rom)
        ensure_output(args.output, force=args.force)
    except (OSError, ValueError) as error:
        print(f"error: {error}")
        return 2

    failures: list[tuple[Path, str]] = []
    output_count = 0
    for color_name, (hex_color, face_reference) in PALETTE.items():
        rgb = tuple(int(hex_color[index : index + 2], 16) for index in (0, 2, 4))
        texture = texture_chunk(rgb)
        color_outputs = 0
        for source_path in paths:
            try:
                data = colorize(source_path.read_bytes(), texture)
                verify(data)
                destination = args.output / color_name / "ROM" / source_path.relative_to(args.rom)
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(data)
                color_outputs += 1
                output_count += 1
            except (OSError, ValueError) as error:
                failures.append((source_path, f"{color_name}: {error}"))
        print(f"{color_name}: #{hex_color} ref={face_reference} outputs={color_outputs}")

    print(
        f"inputs={len(paths)} outputs={output_count} skipped=0 failures={len(failures)} "
        f"output={args.output}"
    )
    for path, reason in failures[:40]:
        print(f"FAIL {path}: {reason}")
    return 1 if failures or output_count == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
