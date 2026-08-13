#!/usr/bin/env python3
"""Build deterministic Ashita and Windower release archives."""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

try:
    from .dat_chunks import validate_chunks
    from .tool_utils import add_force, ensure_output
except ImportError:  # Direct ``python tools/build_packages.py`` execution.
    from dat_chunks import validate_chunks
    from tool_utils import add_force, ensure_output


EXPECTED_DAT_COUNT = 408
PROJECT_ROOT = Path(__file__).resolve().parent.parent
PACKAGE_DOCUMENTS = ("README.md", "LICENSES.md", "LICENSE.GPL.txt", "LICENSE.md")
DOCUMENT_PREFIX = PurePosixPath("FluffyTail")
ARCHIVES = {
    "ashita": (
        "fluffytail-ashita.zip",
        PurePosixPath("polplugins/DATs/FluffyTail/ROM"),
    ),
    "windower": (
        "fluffytail-windower.zip",
        PurePosixPath("addons/XIPivot/data/DATs/FluffyTail/ROM"),
    ),
}
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
ZIP_MODE = 0o100644 << 16


@dataclass(frozen=True)
class PackageResult:
    """One completed host package."""

    host: str
    path: Path
    sha256: str
    file_count: int


def validate_dll(path: Path) -> bytes:
    """Read and minimally validate the required 32-bit Windows plugin DLL."""

    try:
        data = path.read_bytes()
    except OSError as error:
        raise ValueError(f"cannot read plugin DLL {path}: {error}") from error

    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError(f"plugin is not a PE DLL: {path}")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset > len(data) - 26 or data[pe_offset : pe_offset + 4] != b"PE\x00\x00":
        raise ValueError(f"plugin has an invalid PE header: {path}")

    machine, _, _, _, _, optional_size, characteristics = struct.unpack_from(
        "<HHIIIHH", data, pe_offset + 4
    )
    optional_offset = pe_offset + 24
    if optional_size < 2 or optional_offset + optional_size > len(data):
        raise ValueError(f"plugin has an invalid PE optional header: {path}")
    optional_magic = struct.unpack_from("<H", data, optional_offset)[0]
    if machine != 0x014C or optional_magic != 0x010B or characteristics & 0x2000 == 0:
        raise ValueError(f"plugin must be an x86 PE32 DLL: {path}")
    return data


def validate_overlay(path: Path) -> list[tuple[PurePosixPath, bytes]]:
    """Return the exact validated ``ROM/<folder>/<file>.DAT`` payload."""

    if path.is_symlink() or not path.is_dir():
        raise ValueError(f"overlay directory does not exist: {path}")

    entries = list(path.iterdir())
    if (
        len(entries) != 1
        or entries[0].name != "ROM"
        or entries[0].is_symlink()
        or not entries[0].is_dir()
    ):
        raise ValueError(f"overlay must contain only one ROM directory: {path}")

    rom = entries[0]
    # Inspect every descendant before collecting files.  In particular, do not
    # allow a symlinked directory to make files outside the overlay part of the
    # package (or let a consumer follow one after extraction).
    descendants = list(rom.rglob("*"))
    for entry in descendants:
        if entry.is_symlink():
            raise ValueError(f"overlay must not contain symbolic links: {entry}")

    source_files = sorted((entry for entry in descendants if entry.is_file()), key=str)
    if len(source_files) != EXPECTED_DAT_COUNT:
        raise ValueError(
            f"overlay must contain exactly {EXPECTED_DAT_COUNT} DAT files; "
            f"found {len(source_files)}: {rom}"
        )

    payload: list[tuple[PurePosixPath, bytes]] = []
    casefolded: set[str] = set()
    for source in source_files:
        relative = source.relative_to(rom)
        if (
            len(relative.parts) != 2
            or not relative.parts[0].isdecimal()
            or not relative.stem.isdecimal()
            or relative.suffix != ".DAT"
        ):
            raise ValueError(f"invalid overlay path; expected <folder>/<file>.DAT: {relative}")

        archive_path = PurePosixPath(*relative.parts)
        folded = archive_path.as_posix().casefold()
        if folded in casefolded:
            raise ValueError(f"case-insensitive duplicate overlay path: {archive_path}")
        casefolded.add(folded)

        try:
            data = source.read_bytes()
        except OSError as error:
            raise ValueError(f"cannot read overlay DAT {source}: {error}") from error
        validation = validate_chunks(data)
        if not validation.ok:
            raise ValueError(f"invalid overlay DAT {relative}: {validation.error}")
        payload.append((archive_path, data))

    return payload


def validate_archive_entries(entries: list[tuple[PurePosixPath, bytes]]) -> None:
    """Reject unsafe or duplicate paths before writing a ZIP archive."""

    seen: set[str] = set()
    for archive_path, _ in entries:
        # ZIP names use POSIX separators regardless of the host running this
        # tool.  Reject absolute and traversal names, including names that a
        # Windows extractor could reinterpret through a backslash.
        if not archive_path.parts or archive_path.is_absolute():
            raise ValueError(f"archive path must be relative: {archive_path!s}")
        if any(part in {"", ".", ".."} for part in archive_path.parts):
            raise ValueError(f"archive path escapes package root: {archive_path!s}")
        if any("\\" in part or "\x00" in part or ":" in part for part in archive_path.parts):
            raise ValueError(f"archive path contains an unsafe character: {archive_path!s}")

        folded = archive_path.as_posix().casefold()
        if folded in seen:
            raise ValueError(f"duplicate archive path: {archive_path!s}")
        seen.add(folded)


def zip_info(path: PurePosixPath) -> zipfile.ZipInfo:
    """Create stable metadata for one regular archive file."""

    info = zipfile.ZipInfo(path.as_posix(), ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = ZIP_MODE
    return info


def write_archive(path: Path, entries: list[tuple[PurePosixPath, bytes]]) -> None:
    """Write an ordered archive atomically."""

    validate_archive_entries(entries)

    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    os.close(descriptor)
    temporary = Path(temporary_name)
    try:
        with zipfile.ZipFile(temporary, "w", allowZip64=False) as archive:
            for archive_path, data in sorted(entries, key=lambda item: item[0].as_posix()):
                archive.writestr(zip_info(archive_path), data, compresslevel=9)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def build_packages(
    dll: Path,
    overlay: Path,
    output: Path,
    *,
    force: bool = False,
) -> list[PackageResult]:
    """Validate inputs and create both host-specific archives from one payload."""

    dll_data = validate_dll(dll)
    dat_payload = validate_overlay(overlay)
    documents = []
    for name in PACKAGE_DOCUMENTS:
        path = PROJECT_ROOT / name
        try:
            documents.append((DOCUMENT_PREFIX / name, path.read_bytes()))
        except OSError as error:
            raise ValueError(f"cannot read required package notice {path}: {error}") from error
    ensure_output(output, force=force)

    results = []
    for host, (filename, overlay_prefix) in ARCHIVES.items():
        entries = [*documents, (PurePosixPath("plugins/fluffytail.dll"), dll_data)]
        entries.extend((overlay_prefix / relative, data) for relative, data in dat_payload)
        archive_path = output / filename
        write_archive(archive_path, entries)
        results.append(
            PackageResult(
                host=host,
                path=archive_path,
                sha256=hashlib.sha256(archive_path.read_bytes()).hexdigest().upper(),
                file_count=len(entries),
            )
        )
    return results


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build deterministic Ashita and Windower FluffyTail release archives."
    )
    parser.add_argument(
        "dll",
        nargs="?",
        type=Path,
        default=Path("bin/fluffytail.dll"),
        help="dual-host x86 plugin DLL (default: bin/fluffytail.dll)",
    )
    parser.add_argument(
        "overlay",
        nargs="?",
        type=Path,
        default=Path("work/runtime-overlay"),
        help="runtime overlay directory containing ROM/ (default: work/runtime-overlay)",
    )
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=Path("work/packages"),
        help="archive output directory (default: work/packages)",
    )
    add_force(parser)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        results = build_packages(args.dll, args.overlay, args.output, force=args.force)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"error: {error}")
        return 2

    for result in results:
        print(
            f"host={result.host} files={result.file_count} sha256={result.sha256} "
            f"output={result.path}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
