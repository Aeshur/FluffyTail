#!/usr/bin/env python3
"""Verify the checked-in Windower ABI fixture against official PE binaries."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_FIXTURE = ROOT / "tests" / "windower_abi_fixture.json"
EXPECTED_TABLE_COUNTS = {
    ".?AVConsole@@": (6, 6),
    ".?AVPluginManager@@": (9, 9),
    # WindowerPlugin derives from the 18-slot IPlugin host interface, then adds
    # 16 helper virtuals used by first-party plugin implementations.
    ".?AVWindowerPlugin@@": (18, 34),
}


def parse_int(value: str) -> int:
    return int(value, 0)


class PeImage:
    def __init__(self, data: bytes) -> None:
        self.data = data
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise ValueError("not a PE image")
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe_offset : pe_offset + 4] != b"PE\0\0":
            raise ValueError("invalid PE signature")
        machine, section_count, _, _, _, optional_size, _ = struct.unpack_from(
            "<HHIIIHH", data, pe_offset + 4
        )
        optional = pe_offset + 24
        if machine != 0x014C or struct.unpack_from("<H", data, optional)[0] != 0x010B:
            raise ValueError("artifact is not x86 PE32")
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        sections = optional + optional_size
        self.sections: list[tuple[int, int, int, int, int]] = []
        for index in range(section_count):
            offset = sections + index * 40
            virtual_size, virtual_address, raw_size, raw_address = struct.unpack_from(
                "<IIII", data, offset + 8
            )
            characteristics = struct.unpack_from("<I", data, offset + 36)[0]
            self.sections.append(
                (
                    virtual_address,
                    max(virtual_size, raw_size),
                    raw_address,
                    raw_size,
                    characteristics,
                )
            )

    def offset(self, rva: int) -> int:
        for virtual_address, mapped_size, raw_address, raw_size, _ in self.sections:
            relative = rva - virtual_address
            if 0 <= relative < mapped_size and relative < raw_size:
                return raw_address + relative
        raise ValueError(f"RVA 0x{rva:08X} is not file-backed")

    def bytes_at(self, rva: int, size: int) -> bytes:
        offset = self.offset(rva)
        return self.data[offset : offset + size]

    def pointer_at(self, rva: int) -> int:
        return struct.unpack("<I", self.bytes_at(rva, 4))[0]

    def c_string_at(self, rva: int) -> str:
        offset = self.offset(rva)
        end = self.data.index(b"\0", offset)
        return self.data[offset:end].decode("ascii")

    def is_executable_pointer(self, pointer: int) -> bool:
        rva = pointer - self.image_base
        return any(
            virtual_address <= rva < virtual_address + mapped_size
            and (characteristics & 0x20000000) != 0
            for virtual_address, mapped_size, _, _, characteristics in self.sections
        )

    def rtti_bases(self, vtable_rva: int) -> list[str]:
        locator_rva = self.pointer_at(vtable_rva - 4) - self.image_base
        hierarchy_rva = self.pointer_at(locator_rva + 16) - self.image_base
        base_count = self.pointer_at(hierarchy_rva + 8)
        base_array_rva = self.pointer_at(hierarchy_rva + 12) - self.image_base
        names = []
        for index in range(base_count):
            descriptor_rva = self.pointer_at(base_array_rva + index * 4) - self.image_base
            type_rva = self.pointer_at(descriptor_rva) - self.image_base
            names.append(self.c_string_at(type_rva + 8))
        return names

    def first_return(self, function_rva: int, limit: int = 0x400) -> tuple[int, bytes]:
        code = self.bytes_at(function_rva, limit)
        position = 0
        while position < len(code):
            opcode = code[position]
            if opcode == 0xC3:
                return function_rva + position, b"\xc3"
            if opcode == 0xC2 and position + 3 <= len(code):
                return function_rva + position, code[position : position + 3]
            position += instruction_length(code, position)
        raise ValueError(f"function at 0x{function_rva:08X} has no return within 0x{limit:X} bytes")


def instruction_length(code: bytes, position: int) -> int:
    """Return a conservative x86 instruction length for fixture return scanning."""

    start = position
    while position < len(code) and code[position] in {
        0x26,
        0x2E,
        0x36,
        0x3E,
        0x64,
        0x65,
        0x66,
        0x67,
        0xF0,
        0xF2,
        0xF3,
    }:
        position += 1
    if position >= len(code):
        return max(1, position - start)

    opcode = code[position]
    position += 1
    if opcode == 0x0F and position < len(code):
        opcode = 0x100 | code[position]
        position += 1

    relative_sizes = {
        0x68: 4,
        0x6A: 1,
        0xA0: 4,
        0xA1: 4,
        0xA2: 4,
        0xA3: 4,
        0xB8: 4,
        0xB9: 4,
        0xBA: 4,
        0xBB: 4,
        0xBC: 4,
        0xBD: 4,
        0xBE: 4,
        0xBF: 4,
        0xC2: 2,
        0xC6: 1,
        0xC7: 4,
        0xE8: 4,
        0xE9: 4,
        0xEB: 1,
    }
    if 0x70 <= opcode <= 0x7F:
        return position - start + 1
    if 0x180 <= opcode <= 0x18F:
        return position - start + 4
    if opcode in relative_sizes and opcode not in {0xC6, 0xC7}:
        return position - start + relative_sizes[opcode]

    has_modrm = (
        opcode
        in {
            0x01,
            0x03,
            0x0B,
            0x21,
            0x23,
            0x29,
            0x2B,
            0x31,
            0x32,
            0x33,
            0x39,
            0x3B,
            0x63,
            0x69,
            0x6B,
            0x80,
            0x81,
            0x83,
            0x84,
            0x85,
            0x88,
            0x89,
            0x8A,
            0x8B,
            0x8D,
            0x8F,
            0xC0,
            0xC1,
            0xC6,
            0xC7,
            0xD0,
            0xD1,
            0xD2,
            0xD3,
            0xF6,
            0xF7,
            0xFE,
            0xFF,
        }
        or 0x110 <= opcode <= 0x117
        or 0x128 <= opcode <= 0x12F
    )
    if not has_modrm or position >= len(code):
        return max(1, position - start)

    modrm = code[position]
    position += 1
    mod = modrm >> 6
    rm = modrm & 7
    if mod != 3 and rm == 4 and position < len(code):
        sib = code[position]
        position += 1
        if mod == 0 and (sib & 7) == 5:
            position += 4
    if mod == 0 and rm == 5:
        position += 4
    elif mod == 1:
        position += 1
    elif mod == 2:
        position += 4
    if opcode in {0x69, 0x81, 0xC7}:
        position += 4
    elif opcode in {0x6B, 0x80, 0x83, 0xC0, 0xC1, 0xC6}:
        position += 1
    return max(1, position - start)


def return_bytes(stack_bytes: int) -> bytes:
    if stack_bytes == 0:
        return b"\xc3"
    return b"\xc2" + struct.pack("<H", stack_bytes)


def verify_table(image: PeImage, table: dict[str, Any]) -> None:
    vtable_rva = parse_int(table["vtable_rva"])
    locator_rva = image.pointer_at(vtable_rva - 4) - image.image_base
    type_descriptor_rva = image.pointer_at(locator_rva + 12) - image.image_base
    rtti_name = image.c_string_at(type_descriptor_rva + 8)
    if rtti_name != table["rtti_name"]:
        raise ValueError(f"vtable RTTI is {rtti_name}, expected {table['rtti_name']}")
    if "rtti_bases" in table and image.rtti_bases(vtable_rva) != table["rtti_bases"]:
        raise ValueError(f"{table['rtti_name']} RTTI inheritance changed")

    host_slots = table["slots"]
    host_slot_count = table.get("host_slot_count", len(host_slots))
    if len(host_slots) != host_slot_count:
        raise ValueError(f"{table['rtti_name']} host slot fixture is truncated")
    helper_slots = table.get("helper_slots", [])
    all_slots = host_slots + helper_slots
    concrete_slot_count = table.get("concrete_slot_count", len(all_slots))
    expected_counts = EXPECTED_TABLE_COUNTS.get(table["rtti_name"])
    if expected_counts != (host_slot_count, concrete_slot_count):
        raise ValueError(f"{table['rtti_name']} declared slot counts changed")
    if len(all_slots) != concrete_slot_count:
        raise ValueError(f"{table['rtti_name']} concrete slot fixture is truncated")

    for index, slot in enumerate(all_slots):
        function_rva = parse_int(slot["function_rva"])
        observed_pointer = image.pointer_at(vtable_rva + index * 4)
        observed = observed_pointer - image.image_base
        if observed != function_rva:
            raise ValueError(
                f"{table['rtti_name']} slot {index} {slot['name']} points to "
                f"0x{observed:08X}, expected 0x{function_rva:08X}"
            )
        return_rva = parse_int(slot["return_rva"])
        if not image.is_executable_pointer(observed_pointer):
            raise ValueError(f"{table['rtti_name']} slot {index} is not executable")
        expected_return = (
            bytes.fromhex(slot["return_bytes"])
            if "return_bytes" in slot
            else return_bytes(slot["stack_bytes"])
        )
        observed_return_rva, observed_return = image.first_return(function_rva)
        if observed_return_rva != return_rva or observed_return != expected_return:
            raise ValueError(f"{table['rtti_name']} slot {index} has different stack cleanup")

    next_pointer = image.pointer_at(vtable_rva + concrete_slot_count * 4)
    if image.is_executable_pointer(next_pointer):
        raise ValueError(f"{table['rtti_name']} concrete vtable has undeclared executable slots")


def download(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "FluffyTail-ABI-Verifier/1.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read()


def load_artifact(name: str, artifact: dict[str, str], directory: Path | None) -> bytes:
    if directory is None:
        data = download(artifact["url"])
    else:
        data = (directory / f"{name.capitalize()}-live.dll").read_bytes()
    digest = hashlib.sha256(data).hexdigest().upper()
    if digest != artifact["sha256"]:
        raise ValueError(f"{name} SHA-256 is {digest}, expected {artifact['sha256']}")
    return data


def verify(fixture_path: Path, directory: Path | None = None) -> None:
    fixture = json.loads(fixture_path.read_text(encoding="ascii"))
    manifest = fixture["manifest"]
    manifest_data = (
        download(manifest["url"])
        if directory is None
        else (directory / "manifest-live.xml").read_bytes()
    )
    manifest_digest = hashlib.sha256(manifest_data).hexdigest().upper()
    if manifest_digest != manifest["sha256"]:
        raise ValueError(f"manifest SHA-256 is {manifest_digest}, expected {manifest['sha256']}")
    manifest_root = ET.fromstring(manifest_data)
    if manifest_root.findtext("hook/version") != manifest["hook_version"]:
        raise ValueError("manifest Hook version changed")
    config_version = next(
        (
            plugin.findtext("version")
            for plugin in manifest_root.findall("plugins/plugin")
            if plugin.findtext("name") == "Config"
        ),
        None,
    )
    if config_version != manifest["config_version"]:
        raise ValueError("manifest Config version changed")

    images = {
        name: PeImage(load_artifact(name, artifact, directory))
        for name, artifact in fixture["artifacts"].items()
    }
    version = fixture["interface_version"]
    version_image = images[version["artifact"]]
    expected = bytes.fromhex(version["bytes"])
    if version_image.bytes_at(parse_int(version["function_rva"]), len(expected)) != expected:
        raise ValueError("GetInterfaceVersion implementation changed")
    for key in ("console", "plugin_manager", "plugin_base"):
        table = fixture[key]
        verify_table(images[table["artifact"]], table)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    parser.add_argument(
        "--directory",
        type=Path,
        help="directory containing Hook-live.dll and Config-live.dll; downloads otherwise",
    )
    args = parser.parse_args(argv)
    verify(args.fixture, args.directory)
    print("Windower 4.7.9 ABI fixture verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
