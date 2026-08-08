#!/usr/bin/env python3
"""Embed a solid DXT3 tail texture and retarget every ``oth_l`` tail material.

The pixel payload starts at DAT offset ``0x55`` and uses distinct DXT3 endpoints.
These details preserve the tested retail overlay fingerprint.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from .dat_chunks import validate_chunks
    from .tool_utils import add_force, dat_files, ensure_output, parse_rgb, texture_name
except ImportError:  # Direct ``python tools/colorize_tail.py`` execution.
    from dat_chunks import validate_chunks
    from tool_utils import add_force, dat_files, ensure_output, parse_rgb, texture_name

# This captured stock DXT3 texture header has 85 bytes. Only its tag and name change.
HEADER = bytes.fromhex(
    "63686169200308000000000000000000a174696d202020202063686169725f3031"
    "28000000000100000001000001000800000000000000000000000000000000000000000020000000335458440000010000040000"
)
TRAIL = b"\x00" * 11
DXT_OFFSET = 0x55
TEXTURE_WIDTH = 256
TEXTURE_HEIGHT = 256
PIXEL_COUNT = TEXTURE_WIDTH * TEXTURE_HEIGHT
DXT3_BLOCK_SIZE = 16
DXT3_BLOCK_COUNT = PIXEL_COUNT // 16
TIM = b"tim" + b"\x20" * 5
OTH_L = b"oth_l" + b"\x20" * 3


def rgb565(red: int, green: int, blue: int) -> int:
    """Pack 8-bit RGB into a little-endian RGB565 endpoint."""

    return ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)


def solid_dxt3(red: int, green: int, blue: int) -> bytes:
    """Return an opaque 256x256 DXT3 surface filled with one colour."""

    if any(not 0 <= channel <= 255 for channel in (red, green, blue)):
        raise ValueError("RGB channels must be between 0 and 255")
    color_endpoint = rgb565(red, green, blue)
    other_endpoint = 0xFFFF if color_endpoint != 0xFFFF else 0x0000
    block = b"\xff" * 8 + struct.pack("<HH", color_endpoint, other_endpoint) + b"\x00" * 4
    return block * DXT3_BLOCK_COUNT


def texture_chunk(
    rgb: tuple[int, int, int], name: bytes = b"fluftail", tag: bytes = b"flft"
) -> bytes:
    """Build the embedded texture chunk without truncating its name."""

    if len(HEADER) != DXT_OFFSET:
        raise ValueError(f"unexpected DXT3 header length: {len(HEADER)}")
    if len(name) == 0 or len(name) > 8:
        raise ValueError("texture name must contain one to eight bytes")
    if len(tag) != 4:
        raise ValueError("texture chunk tag must contain exactly four bytes")
    header = bytearray(HEADER)
    header[0:4] = tag
    header[0x19:0x21] = name + b"\x20" * (8 - len(name))
    return bytes(header) + solid_dxt3(*rgb) + TRAIL


def colorize(
    dat: bytes,
    tex_chunk: bytes,
    name: bytes = b"fluftail",
    insert_at: int | None = None,
) -> bytes:
    """Retarget tail materials and insert *tex_chunk* at the requested boundary."""

    if len(name) == 0 or len(name) > 8:
        raise ValueError("texture name must contain one to eight bytes")
    name8 = name + b"\x20" * (8 - len(name))
    output = bytearray(dat)
    search_from = 0
    while True:
        marker_at = output.find(TIM, search_from)
        if marker_at < 0:
            break
        if output[marker_at + 8 : marker_at + 16] == OTH_L:
            output[marker_at + 8 : marker_at + 16] = name8
        search_from = marker_at + 8

    if insert_at is None:
        if len(output) < 8:
            raise ValueError("DAT is too short to contain an equipment header")
        header = struct.unpack_from("<I", output, 4)[0]
        insertion_at = ((header >> 7) & 0x1FFFF) * 16
    else:
        insertion_at = insert_at
        if insertion_at < 0 or insertion_at > len(output) or insertion_at % 16:
            raise ValueError(f"invalid texture insertion offset: {insertion_at}")
    return bytes(output[:insertion_at]) + tex_chunk + bytes(output[insertion_at:])


def verify(data: bytes) -> None:
    """Raise with the chunk-walk reason when *data* is not a complete DAT."""

    result = validate_chunks(data)
    if not result.ok:
        raise ValueError(result.error or "invalid DAT chunk stream")
    if not result.chunks or result.chunks[-1].tag.rstrip("\x00") != "end":
        raise ValueError("output does not terminate on an 'end' chunk")


def walk_ok(data: bytes) -> bool:
    """Compatibility predicate for callers migrating to :func:`verify`."""

    return validate_chunks(data).ok


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Embed a solid DXT3 tail texture in tailed body DAT files."
    )
    parser.add_argument("input", type=Path, help="directory containing tailed ROM DAT files")
    parser.add_argument("output", type=Path, help="output directory receiving ROM/<folder>/*.DAT")
    parser.add_argument("color", type=parse_rgb, help="tail color as RRGGBB or #RRGGBB")
    parser.add_argument(
        "--name", default="fluftail", help="embedded texture name (1-8 ASCII bytes)"
    )
    add_force(parser)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        paths = dat_files(args.input)
        name = texture_name(args.name)
        ensure_output(args.output, force=args.force)
        texture = texture_chunk(args.color, name=name)
    except (ValueError, OSError) as error:
        print(f"error: {error}")
        return 2

    failures: list[tuple[Path, str]] = []
    output_count = 0
    for source_path in paths:
        try:
            data = colorize(source_path.read_bytes(), texture, name=name)
            verify(data)
            destination = args.output / "ROM" / source_path.relative_to(args.input)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            output_count += 1
        except (OSError, ValueError) as error:
            failures.append((source_path, str(error)))
    print(
        f"colorized inputs={len(paths)} outputs={output_count} skipped=0 "
        f"failures={len(failures)} output={args.output} "
        f"color=#{''.join(f'{c:02X}' for c in args.color)} "
        f"name={args.name}"
    )
    for path, reason in failures[:40]:
        print(f"FAIL {path}: {reason}")
    return 1 if failures or output_count == 0 else 0


if __name__ == "__main__":
    raise SystemExit(main())
