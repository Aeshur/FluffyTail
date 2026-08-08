"""Checked readers for the flat chunk format used by FFXI DAT files.

The parser deliberately owns only chunk framing. Policies for textures, meshes,
and command line behavior stay in the release tools that use this module.
"""

from __future__ import annotations

import struct
from collections.abc import Iterator
from dataclasses import dataclass

CHUNK_HEADER_SIZE = 8
CHUNK_ALIGNMENT = 16
CHUNK_TYPE_END = 0
CHUNK_TYPE_SKELETON = 32
CHUNK_TYPE_MESH = 42
CHUNK_TYPE_MODEL_SECTION = 1


@dataclass(frozen=True)
class Chunk:
    """One framed DAT chunk, including its original bytes."""

    offset: int
    tag: str
    chunk_type: int
    length: int
    data: bytes

    @property
    def type(self) -> int:
        """Compatibility spelling for callers that use ``chunk.type``."""

        return self.chunk_type


@dataclass(frozen=True)
class ChunkValidation:
    """Result of a non-throwing chunk walk."""

    ok: bool
    chunks: tuple[Chunk, ...]
    error: str | None = None
    offset: int | None = None


class ChunkParseError(ValueError):
    """Raised when a DAT chunk stream is truncated or internally inconsistent."""


def _failure(message: str, offset: int) -> ChunkParseError:
    return ChunkParseError(f"DAT chunk walk failed at offset 0x{offset:X}: {message}")


def iter_chunks(data: bytes, *, strict: bool = True) -> Iterator[Chunk]:
    """Yield chunks from *data*, validating framing and the terminal ``end`` chunk."""

    offset = 0
    found_end = False
    last_tag: str | None = None
    while offset < len(data):
        remaining = len(data) - offset
        if remaining < CHUNK_HEADER_SIZE:
            if strict:
                raise _failure("truncated chunk header", offset)
            return

        tag_bytes = data[offset : offset + 4]
        tag = tag_bytes.decode("latin1")
        header = struct.unpack_from("<I", data, offset + 4)[0]
        chunk_type = header & 0x7F
        units = (header >> 7) & 0x1FFFF
        length = units * CHUNK_ALIGNMENT

        if length == 0:
            length = CHUNK_ALIGNMENT
            if remaining < length:
                if strict:
                    raise _failure("truncated end chunk", offset)
                return
            chunk = Chunk(offset, tag, chunk_type, length, data[offset : offset + length])
            yield chunk
            last_tag = tag
            offset += length
            found_end = found_end or (chunk_type == CHUNK_TYPE_END and tag.rstrip("\x00") == "end")
            continue

        if length < CHUNK_ALIGNMENT or length % CHUNK_ALIGNMENT:
            if strict:
                raise _failure(f"invalid chunk length {length}", offset)
            return
        if length > remaining:
            if strict:
                raise _failure(f"chunk length {length} exceeds remaining {remaining} bytes", offset)
            return

        chunk = Chunk(offset, tag, chunk_type, length, data[offset : offset + length])
        yield chunk
        last_tag = tag
        offset += length
        if chunk_type == CHUNK_TYPE_END and tag.rstrip("\x00") == "end":
            found_end = True

    if strict:
        if not found_end:
            raise _failure("missing terminal end chunk", offset)
        if last_tag is None or last_tag.rstrip("\x00") != "end":
            raise _failure("terminal end chunk is not the final chunk", offset)


def parse_chunks(data: bytes) -> list[Chunk]:
    """Return a fully checked chunk list or raise :class:`ChunkParseError`."""

    return list(iter_chunks(data))


def validate_chunks(data: bytes) -> ChunkValidation:
    """Check a chunk stream and retain an actionable error when it is invalid."""

    try:
        chunks = tuple(iter_chunks(data))
    except ChunkParseError as error:
        message = str(error)
        marker = "offset 0x"
        offset = None
        if marker in message:
            text = message.split(marker, 1)[1].split(":", 1)[0]
            try:
                offset = int(text, 16)
            except ValueError:
                pass
        return ChunkValidation(False, (), message, offset)
    return ChunkValidation(True, chunks)


def walk_ok(data: bytes) -> bool:
    """Compatibility predicate. Use :func:`validate_chunks` for diagnostics."""

    return validate_chunks(data).ok
