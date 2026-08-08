#!/usr/bin/env python3
"""Splice the matched FluffyTail bone and mesh chunks into one vanilla Mithra DAT.

Existing body chunks remain byte for byte identical except where merged tail triangles
must be collapsed.
Each skeleton rig uses its matched private ``tail_assets/<rig>.bone`` and ``.mesh`` pair.
"""

import argparse
import struct
from pathlib import Path

try:
    from .dat_chunks import CHUNK_TYPE_MESH, CHUNK_TYPE_SKELETON, Chunk, parse_chunks
except ImportError:  # Direct ``python tools/splice_tail.py`` execution.
    from dat_chunks import CHUNK_TYPE_MESH, CHUNK_TYPE_SKELETON, Chunk, parse_chunks

ASSETS = Path(__file__).resolve().parent / "tail_assets"

# Mithra skeleton tail chain, verified empirically. Bones 29-35 occur together in
# every tail mesh. Bones 20 and 66 are the tail base and tip attachment points.
TAIL_BONES = set(range(29, 36))
TAIL_BASE = {20, 66}


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def mesh_bone_set(chunk: bytes) -> set[int]:
    """Skeleton bone indices a type-42 (DAT2A) mesh is weighted to.

    Layout per galkareeve/ffxi TDWCharacter/TDWAnalysis: DAT2A data starts at
    chunk offset 0x10. Its header carries offsets in WORD units to a bone table and an
    optional indirection table (used when header.type & 0x80)."""
    d = chunk[0x10:]
    typ = _u16(d, 0x02)
    off_tbl = _u32(d, 0x0C) * 2
    n_tbl = _u16(d, 0x10)
    off_bone = _u32(d, 0x18) * 2
    n_bone = _u16(d, 0x1C)
    tbl = [_u16(d, off_tbl + 2 * i) for i in range(n_tbl)] if off_tbl + 2 * n_tbl <= len(d) else []
    bones = set()
    # BoneSuu counts the WORDs in pBone, with 2 per vertex. Reading n_bone*2
    # overruns into vertex data and yields phantom bones.
    for i in range(n_bone):
        o = off_bone + 2 * i
        if o + 2 > len(d):
            break
        w = _u16(d, o) & 0x7F
        if typ & 0x80:
            if w < len(tbl):
                bones.add(tbl[w])
        else:
            bones.add(w)
    return bones


def separate_tail_indices(chunks: list[Chunk]) -> list[int]:
    """Indices of type-42 chunks that are a *pure* tail mesh (tail bones only,
    <=2 stray bones) - safe to delete. Empty if the tail is fused into a body
    mesh (merged case), handled instead by degenerating tail triangles."""
    out = []
    for i, c in enumerate(chunks):
        if c.chunk_type != CHUNK_TYPE_MESH:
            continue
        bs = mesh_bone_set(c.data)
        if len(bs & TAIL_BONES) >= 3 and len(bs - TAIL_BONES - TAIL_BASE) <= 2:
            out.append(i)
    return out


def _vertex_bones(d: bytes) -> dict[int, tuple[int, int]]:
    """Return each DAT2A mesh vertex's ``(bone0, bone1)`` pair.

    Vertex count is weight1+weight2, not the VertexSuu field. pBone holds two
    WORDs per vertex.
    """
    typ = _u16(d, 0x02)
    ow = _u32(d, 0x12) * 2
    n_v = _u16(d, ow) + _u16(d, ow + 2)
    off_tbl = _u32(d, 0x0C) * 2
    n_tbl = _u16(d, 0x10)
    off_bone = _u32(d, 0x18) * 2
    tbl = [_u16(d, off_tbl + 2 * i) for i in range(n_tbl)] if off_tbl + 2 * n_tbl <= len(d) else []

    def resolve_bone(word: int) -> int:
        word &= 0x7F
        return tbl[word] if (typ & 0x80 and word < len(tbl)) else word

    vertex_bones = {}
    for vertex_index in range(n_v):
        bone_offset = off_bone + vertex_index * 4
        if bone_offset + 4 > len(d):
            break
        vertex_bones[vertex_index] = (
            resolve_bone(_u16(d, bone_offset)),
            resolve_bone(_u16(d, bone_offset + 2)),
        )
    return vertex_bones


def _tlist_triangles(d: bytes):
    """Yield (i1_byte_offset, (i1,i2,i3)) for every triangle list ('T') triangle.
    Strip primitives are skipped for stepping only (they never carry tail geometry
    in this model set). Offsets are relative to the DAT2A body `d`."""
    p = _u32(d, 0x06) * 2
    while p + 4 <= len(d):
        wf = _u16(d, p)
        ws = _u16(d, p + 2)
        if (wf & 0x80F0) == 0x8010:
            p += 0x2E
        elif (wf & 0x80F0) == 0x8000:
            p += 0x12
        elif wf == 0x5453:  # ST strip
            p += ws * 10 + 0x18
        elif wf == 0x4353:
            p += ws * 20 + 0x0C
        elif wf == 0x0043:
            p += ws * 10 + 0x04
        elif wf == 0x0054:  # T list: ws triangles, TEXLIST = i1,i2,i3 then UVs
            for triangle_index in range(ws):
                index_offset = p + 4 + triangle_index * 30
                yield (
                    index_offset,
                    (
                        _u16(d, index_offset),
                        _u16(d, index_offset + 2),
                        _u16(d, index_offset + 4),
                    ),
                )
            p += ws * 30 + 0x04
        else:
            break


def degenerate_tail_triangles(chunk: bytes) -> bytes:
    """Collapse triangles whose three vertices each have at least one tail-bone weight.

    Each becomes a degenerate triangle with zero area (i2=i3=i1), so it is not drawn.
    This removes a tail fused into a body mesh without touching body triangles,
    vertices, or chunk length.
    """
    block = bytearray(chunk)
    mesh_data = bytes(block[0x10:])
    try:
        vertex_bones = _vertex_bones(mesh_data)
    except (IndexError, struct.error):
        return chunk

    def is_tail(vertex_index: int) -> bool:
        bones = vertex_bones.get(vertex_index)
        return bones is not None and (bones[0] in TAIL_BONES or bones[1] in TAIL_BONES)

    for index_offset, indices in _tlist_triangles(mesh_data):
        if all(is_tail(vertex_index) for vertex_index in indices):
            struct.pack_into("<HH", block, 0x10 + index_offset + 2, indices[0], indices[0])
    return bytes(block)


def load_rig(rig: str) -> tuple[bytes, bytes]:
    """Return (tail_bone_bytes, tail_mesh_bytes) for a rig tag, or raise."""
    key = rig.replace("\x00", "_")
    bone = ASSETS / f"{key}.bone"
    mesh = ASSETS / f"{key}.mesh"
    if not bone.exists() or not mesh.exists():
        avail = sorted(p.stem for p in ASSETS.glob("*.bone"))
        raise ValueError(f"no tail asset for rig {rig!r}; known rigs: {avail}")
    return bone.read_bytes(), mesh.read_bytes()


def tail_section(chunks: list[Chunk]) -> tuple[int, int]:
    """Return (tail mesh index, preceding body skeleton index).

    Equipment DATs begin with their body skeleton, but fixed NPC DATs contain several
    model sections. Follow the mesh containing the tail back to its own skeleton instead of
    assuming the first type-32 chunk belongs to the body.
    """
    separate = separate_tail_indices(chunks)
    if separate:
        mesh_index = separate[0]
    else:
        mesh_index = next(
            (
                i
                for i, c in enumerate(chunks)
                if c.chunk_type == CHUNK_TYPE_MESH and (mesh_bone_set(c.data) & TAIL_BONES)
            ),
            None,
        )
    if mesh_index is None:
        raise ValueError("no Mithra tail mesh found")

    skeleton_index = next(
        (i for i in range(mesh_index - 1, -1, -1) if chunks[i].chunk_type == CHUNK_TYPE_SKELETON),
        None,
    )
    if skeleton_index is None:
        raise ValueError("no body skeleton precedes the tail mesh")
    return mesh_index, skeleton_index


def rig_of(chunks: list[Chunk]) -> str:
    """Return the skeleton tag for the body section containing the tail."""
    _, skeleton_index = tail_section(chunks)
    return chunks[skeleton_index].tag


def splice(vanilla: bytes) -> bytes:
    chunks = parse_chunks(vanilla)
    if tail_case(vanilla) == "hidden":
        # No tail bones means a costume or transformation model, such as Chocobo Suit.
        # Adding a tail would look wrong because this is not a Mithra body.
        raise ValueError("body has no Mithra tail (costume/special model); skipped")
    tail_mesh_index, skeleton_index = tail_section(chunks)
    rig = chunks[skeleton_index].tag
    tail_bone, tail_mesh = load_rig(rig)

    # Detect our tail bone block before modifying an already tailed body.
    if any(c.data == tail_bone for c in chunks):
        raise ValueError("already tailed (tail-bone block present)")

    section_end_at = next(
        i
        for i, c in enumerate(chunks)
        if i > tail_mesh_index and ((c.tag == "sdam" and c.chunk_type == 61) or c.chunk_type == 0)
    )
    strip = set(separate_tail_indices(chunks))  # drop the vanilla tail mesh if separable

    out = bytearray()
    for i, c in enumerate(chunks):
        if i == section_end_at:
            out += tail_mesh
        if i not in strip:
            out += degenerate_tail_triangles(c.data) if c.chunk_type == CHUNK_TYPE_MESH else c.data
        if i == skeleton_index:
            out += tail_bone
    return bytes(out)


def tail_case(vanilla: bytes) -> str:
    """Classify a vanilla body as 'hidden', 'separate', or 'merged'.

    Hidden bodies have no tail. Separate tails can be deleted. Merged tails are
    fused into a body mesh and need vertex surgery.
    """
    chunks = parse_chunks(vanilla)
    if separate_tail_indices(chunks):
        return "separate"
    if any(
        c.chunk_type == CHUNK_TYPE_MESH and (mesh_bone_set(c.data) & TAIL_BONES) for c in chunks
    ):
        return "merged"
    return "hidden"


def verify(data: bytes) -> None:
    chunks = parse_chunks(data)
    if chunks[-1].tag.rstrip("\x00") != "end":
        raise ValueError("output does not terminate on an 'end' chunk")
    walked = sum(c.length for c in chunks)
    if walked != len(data):
        raise ValueError(f"chunk walk {walked} != file size {len(data)}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Splice the calibrated fluffy tail into one body DAT."
    )
    parser.add_argument("input", type=Path, help="vanilla body DAT")
    parser.add_argument("output", type=Path, help="destination DAT")
    parser.add_argument("--force", action="store_true", help="replace an existing destination")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.input.is_file():
        print(f"error: input DAT does not exist: {args.input}")
        return 2
    if args.output.exists() and not args.force:
        print(f"error: output exists; choose another path or pass --force: {args.output}")
        return 2
    try:
        data = args.input.read_bytes()
        rig = rig_of(parse_chunks(data))
        output = splice(data)
        verify(output)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(output)
    except (OSError, ValueError, struct.error) as error:
        print(f"error: {error}")
        return 1
    print(f"inputs=1 outputs=1 skipped=0 failures=0 output={args.output} rig={rig!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
