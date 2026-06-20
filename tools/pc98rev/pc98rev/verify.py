"""Byte-perfect roundtrip verification between original and rebuilt files."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from pc98rev.model import sha256_file


class VerifyError(Exception):
    pass


def _list_files(path: Path) -> Dict[str, Path]:
    if not path.is_dir():
        raise VerifyError(f"Not a directory: {path}")
    result = {}
    for p in sorted(path.rglob("*")):
        if p.is_file():
            rel = p.relative_to(path).as_posix()
            result[rel] = p
    return result


def verify_directory(original_dir: Path, rebuilt_dir: Path) -> List[dict]:
    """Return mismatch records; empty list means byte-identical."""
    originals = _list_files(original_dir)
    rebuilt = _list_files(rebuilt_dir)

    mismatches: List[dict] = []
    only_in_original = set(originals) - set(rebuilt)
    only_in_rebuilt = set(rebuilt) - set(originals)

    for rel in sorted(only_in_original):
        mismatches.append({"type": "missing_in_rebuilt", "path": rel})
    for rel in sorted(only_in_rebuilt):
        mismatches.append({"type": "missing_in_original", "path": rel})

    for rel in sorted(set(originals) & set(rebuilt)):
        orig = originals[rel]
        reb = rebuilt[rel]
        orig_size = orig.stat().st_size
        reb_size = reb.stat().st_size
        if orig_size != reb_size:
            mismatches.append({
                "type": "size_mismatch",
                "path": rel,
                "original_size": orig_size,
                "rebuilt_size": reb_size,
            })
            continue

        # Compare byte by byte to report first mismatch offset.
        with orig.open("rb") as o, reb.open("rb") as r:
            offset = 0
            while True:
                ob = o.read(65536)
                rb = r.read(65536)
                if ob != rb:
                    # find first differing byte inside this chunk
                    first = next((i for i, (a, b) in enumerate(zip(ob, rb)) if a != b), None)
                    if first is None:
                        # chunks are different lengths (shouldn't happen due to size check)
                        first = min(len(ob), len(rb))
                    mismatch_offset = offset + first
                    mismatches.append({
                        "type": "byte_mismatch",
                        "path": rel,
                        "offset": mismatch_offset,
                        "original_byte": ob[first] if first < len(ob) else None,
                        "rebuilt_byte": rb[first] if first < len(rb) else None,
                    })
                    break
                if not ob:
                    break
                offset += len(ob)

    return mismatches


def format_report(mismatches: List[dict]) -> str:
    if not mismatches:
        return "Verify: all files byte-identical."
    lines = [f"Verify: {len(mismatches)} mismatch(es)."]
    for m in mismatches:
        if m["type"] == "byte_mismatch":
            lines.append(
                f"  {m['path']}: byte mismatch at 0x{m['offset']:X} "
                f"(orig=0x{m['original_byte']:02X}, rebuilt=0x{m['rebuilt_byte']:02X})"
            )
        elif m["type"] == "size_mismatch":
            lines.append(
                f"  {m['path']}: size mismatch "
                f"({m['original_size']} vs {m['rebuilt_size']})"
            )
        else:
            lines.append(f"  {m['path']}: {m['type']}")
    return "\n".join(lines)
