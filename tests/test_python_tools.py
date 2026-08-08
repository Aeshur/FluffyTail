from __future__ import annotations

import struct
from pathlib import Path

import pytest
from tools import build_colorset, build_runtime_overlay, colorize_tail, dat_chunks, splice_tail


def make_chunk(tag: bytes, chunk_type: int, payload: bytes = b"") -> bytes:
    length = ((8 + len(payload) + 15) // 16) * 16
    header = struct.pack("<I", (length // 16) << 7 | chunk_type)
    return tag + header + payload.ljust(length - 8, b"\x00")


def make_end() -> bytes:
    return b"end\x00" + struct.pack("<I", 0x80) + b"\x00" * 8


def make_mesh(bones: list[int]) -> bytes:
    payload = bytearray(128)
    dat2a = 8
    struct.pack_into("<H", payload, dat2a + 0x02, 0)
    struct.pack_into("<I", payload, dat2a + 0x18, 32)
    struct.pack_into("<H", payload, dat2a + 0x1C, len(bones))
    for index, bone in enumerate(bones):
        struct.pack_into("<H", payload, dat2a + 64 + index * 2, bone)
    return make_chunk(b"hh_b", 42, bytes(payload))


def make_dat(*chunks: bytes) -> bytes:
    return b"".join((*chunks, make_end()))


def test_chunk_parser_reports_truncation() -> None:
    with pytest.raises(dat_chunks.ChunkParseError, match="exceeds remaining"):
        dat_chunks.parse_chunks(make_chunk(b"info", 5)[:-1])

    result = dat_chunks.validate_chunks(b"not-a-dat")
    assert not result.ok
    assert result.error and "chunk walk failed" in result.error


def test_chunk_parser_accepts_intermediate_end_sections() -> None:
    # The final end is required. Intermediate end chunks are legal in fixed actors.
    data = make_chunk(b"info", 5) + make_end() + make_chunk(b"info", 5) + make_end()
    chunks = dat_chunks.parse_chunks(data)
    assert [chunk.tag for chunk in chunks].count("end\x00") == 2


def test_dxt3_surface_has_expected_offset_and_distinct_endpoints() -> None:
    texture = colorize_tail.texture_chunk((0x24, 0x1C, 0x1A))
    assert len(texture) == 65632
    assert len(texture) % 16 == 0
    endpoint_offset = 0x55 + 8
    endpoint0, endpoint1 = struct.unpack_from("<HH", texture, endpoint_offset)
    assert texture[:4] == b"flft"
    assert endpoint0 != endpoint1
    assert texture[0x55 : 0x55 + 8] == b"\xff" * 8


def test_material_retarget_and_equipment_insertion() -> None:
    material = make_chunk(b"info", 5, b"prefix" + colorize_tail.TIM + colorize_tail.OTH_L)
    source = make_dat(make_chunk(b"0mt_", 1, b""), material)
    texture = colorize_tail.texture_chunk((1, 2, 3))
    output = colorize_tail.colorize(source, texture)
    assert output[16:20] == b"flft"
    assert colorize_tail.OTH_L not in output
    colorize_tail.verify(output)


def test_fixed_actor_insertion_offset_is_checked() -> None:
    section = make_chunk(b"0mt_", 1, b"")
    skeleton = make_chunk(b"mt_b", 32)
    source = make_dat(section, skeleton, make_mesh([29, 30, 31]))
    texture = colorize_tail.texture_chunk((1, 2, 3))
    insert_at = build_runtime_overlay.body_texture_offset(source)
    assert insert_at == len(section)
    output = colorize_tail.colorize(source, texture, insert_at=insert_at)
    assert output[16:20] == b"flft"
    with pytest.raises(ValueError, match="insertion offset"):
        colorize_tail.colorize(source, texture, insert_at=3)


def test_tail_classification_covers_hidden_separate_and_merged() -> None:
    skeleton = make_chunk(b"mt_b", 32)
    hidden = make_dat(skeleton, make_mesh([0, 1]))
    separate = make_dat(skeleton, make_mesh([29, 30, 31]))
    merged = make_dat(skeleton, make_mesh([29, 30, 31, 0, 1, 2]))
    assert splice_tail.tail_case(hidden) == "hidden"
    assert splice_tail.tail_case(separate) == "separate"
    assert splice_tail.tail_case(merged) == "merged"


def test_palette_and_fixed_model_tables_preserve_calibrated_values() -> None:
    assert build_colorset.PALETTE == {
        "black": ("000000", "-"),
        "white": ("d8d8ca", "M6A"),
        "silver": ("cdc2d4", "M2B"),
        "blonde": ("c3a261", "M8B"),
        "red": ("652708", "M6B"),
        "rose": ("814231", "M5A"),
        "brunette": ("3e2412", "M8A"),
    }
    assert build_runtime_overlay.BASELINE == 0x241C1A
    assert build_runtime_overlay.FIXED_MODELS == {
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


def test_cpp_face_map_preserves_all_sixteen_hair_values() -> None:
    source = (Path(__file__).parents[1] / "src" / "fluffytail.cpp").read_text(encoding="ascii")
    table = source.split("FACE_COLOURS[16] = {", 1)[1].split("};", 1)[0]
    values = [value.strip().strip('"') for value in table.split(",") if value.strip()]
    assert values == [
        "red",
        "brunette",
        "white",
        "silver",
        "silver",
        "red",
        "blonde",
        "red",
        "rose",
        "brunette",
        "white",
        "red",
        "blonde",
        "brunette",
        "brunette",
        "blonde",
    ]


def test_cli_contracts_reject_bad_inputs(tmp_path: Path) -> None:
    with pytest.raises(SystemExit):
        colorize_tail.main([str(tmp_path / "missing"), str(tmp_path / "out"), "xyz"])
    assert (
        colorize_tail.main([str(tmp_path), str(tmp_path / "out"), "010203", "--name", "toolong9x"])
        == 2
    )
    assert build_runtime_overlay.main([str(tmp_path), str(tmp_path / "out")]) == 2
