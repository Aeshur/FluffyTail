#!/usr/bin/env python3
"""Validate tail asset manifest lengths and SHA-1 prefixes without editing assets."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def _sha1_prefix(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()[:8]


def validate_manifest(manifest_path: Path, assets_dir: Path) -> list[str]:
    """Return validation errors for the manifest and assets. An empty list is valid."""

    errors: list[str] = []
    if not manifest_path.is_file():
        return [f"{manifest_path}: manifest does not exist"]
    if not assets_dir.is_dir():
        return [f"{assets_dir}: asset directory does not exist"]
    try:
        payload: Any = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        return [f"{manifest_path}: invalid JSON: {error}"]
    if not isinstance(payload, dict) or not payload:
        return [f"{manifest_path}: manifest must contain a non-empty object"]

    for rig, entry in payload.items():
        prefix = f"{manifest_path}:{rig}"
        if not isinstance(rig, str) or not isinstance(entry, dict):
            errors.append(f"{prefix}: entry must be an object")
            continue
        for kind in ("bone", "mesh"):
            path = assets_dir / f"{rig}.{kind}"
            if not path.is_file():
                errors.append(f"{prefix}: missing asset {path}")
                continue
            length_key = f"{kind}_len"
            hash_key = f"{kind}_sha1_prefix"
            expected_length = entry.get(length_key)
            expected_hash = entry.get(hash_key)
            actual_length = path.stat().st_size
            actual_hash = _sha1_prefix(path)
            if expected_length != actual_length:
                errors.append(
                    f"{prefix}: {length_key}={expected_length!r}, actual {actual_length} ({path})"
                )
            if expected_hash != actual_hash:
                errors.append(
                    f"{prefix}: {hash_key}={expected_hash!r}, actual {actual_hash} ({path})"
                )
        samples = entry.get("samples")
        if not isinstance(samples, int) or samples < 0:
            errors.append(f"{prefix}: samples must be a non-negative integer")
    return errors


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Validate the private tail asset manifest.")
    parser.add_argument(
        "manifest",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent / "tail_assets" / "manifest.json",
        help="manifest JSON path",
    )
    parser.add_argument(
        "assets",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parent / "tail_assets",
        help="directory containing <rig>.bone and <rig>.mesh",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    errors = validate_manifest(args.manifest, args.assets)
    if errors:
        for error in errors:
            print(f"FAIL {error}")
        return 1
    print(f"validated manifest={args.manifest} assets={args.assets}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
