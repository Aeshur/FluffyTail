#!/usr/bin/env python3
"""Build neutral runtime DATs and the ten calibrated fixed-actor DATs."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from .colorize_tail import colorize, texture_chunk
    from .dat_chunks import CHUNK_TYPE_MODEL_SECTION, parse_chunks, validate_chunks
    from .splice_tail import tail_section
    from .tool_utils import add_force, dat_files, ensure_output
except ImportError:  # Direct ``python tools/build_runtime_overlay.py`` execution.
    from colorize_tail import colorize, texture_chunk
    from dat_chunks import CHUNK_TYPE_MODEL_SECTION, parse_chunks, validate_chunks
    from splice_tail import tail_section
    from tool_utils import add_force, dat_files, ensure_output

BASELINE = 0x241C1A
FIXED_MODELS: dict[Path, tuple[int, bytes]] = {
    Path("4/45.DAT"): (0x652708, b"ftx000c7"),
    Path("4/64.DAT"): (0x3E2412, b"ftx000da"),
    Path("4/65.DAT"): (0xD8D8CA, b"ftx000db"),
    Path("4/66.DAT"): (0x814231, b"ftx000dc"),
    Path("4/67.DAT"): (0xC3A261, b"ftx000dd"),
    Path("4/79.DAT"): (0xC3A261, b"ftx000e9"),
    Path("4/80.DAT"): (0x652708, b"ftx000ea"),
    Path("4/94.DAT"): (0xCDC2D4, b"ftx000f8"),
    Path("4/95.DAT"): (0xD8D8CA, b"ftx000f9"),
    Path("4/97.DAT"): (0xD8D8CA, b"ftx000fb"),
}


def rgb_tuple(rgb: int) -> tuple[int, int, int]:
    """Expand a packed RGB integer used by the fixed-model table."""

    return ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF)


def body_texture_offset(data: bytes) -> int:
    """Place a fixed actor texture at the start of its tail-bearing model section."""

    chunks = parse_chunks(data)
    _, skeleton_index = tail_section(chunks)
    section_index = next(
        (
            index
            for index in range(skeleton_index - 1, -1, -1)
            if chunks[index].chunk_type == CHUNK_TYPE_MODEL_SECTION
        ),
        None,
    )
    if section_index is None:
        raise ValueError("no model-section header precedes the body skeleton")
    section = chunks[section_index]
    return section.offset + section.length


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build the neutral runtime overlay and fixed-actor color variants."
    )
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
        default=Path("work/runtime-overlay"),
        help="output directory (default: work/runtime-overlay)",
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

    textures: dict[tuple[int, bytes], bytes] = {}
    counts: dict[int, int] = {}
    failures: list[tuple[Path, str]] = []
    output_count = 0
    for source_path in paths:
        relative = source_path.relative_to(args.rom)
        fixed = FIXED_MODELS.get(relative)
        rgb, name = fixed if fixed is not None else (BASELINE, b"fluftail")
        key = (rgb, name)
        if key not in textures:
            textures[key] = texture_chunk(rgb_tuple(rgb), name=name)
        try:
            source_data = source_path.read_bytes()
            insert_at = body_texture_offset(source_data) if fixed is not None else None
            data = colorize(source_data, textures[key], name=name, insert_at=insert_at)
            validation = validate_chunks(data)
            if not validation.ok:
                raise ValueError(validation.error or "invalid output chunk stream")
            destination = args.output / "ROM" / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            output_count += 1
            counts[rgb] = counts.get(rgb, 0) + 1
        except (OSError, ValueError, struct.error) as error:
            failures.append((source_path, str(error)))

    summary = ", ".join(f"#{rgb:06X}={count}" for rgb, count in sorted(counts.items()))
    print(
        f"inputs={len(paths)} outputs={output_count} skipped=0 failures={len(failures)} "
        f"output={args.output} ({summary})"
    )
    for path, reason in failures[:40]:
        print(f"FAIL {path}: {reason}")
    return 1 if failures or output_count == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
