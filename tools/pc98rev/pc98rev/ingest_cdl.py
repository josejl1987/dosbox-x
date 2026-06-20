"""Binary .p98cdl importer for the PC-98 Code/Data Logger format."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple


@dataclass(frozen=True)
class CDLModule:
    id: str
    base: int
    size: int
    source_file: str
    source_offset: int
    entry_points: Tuple[int, ...]
    bytes: List[Tuple[int, int, int, int]]  # offset, access, origin, analysis


def _read_string(f) -> str:
    (length,) = struct.unpack("<H", f.read(2))
    return f.read(length).decode("utf-8", errors="replace")


def parse_cdl(path: Path) -> Tuple[str, Dict[str, CDLModule]]:
    """Parse a .p98cdl file and return (session_name, {module_id: CDLModule})."""
    with path.open("rb") as f:
        magic = f.read(7)
        if magic != b"P98CDL\x00":
            raise ValueError(f"bad CDL magic: {magic!r}")
        (version,) = struct.unpack("<H", f.read(2))
        if version != 1:
            raise ValueError(f"unsupported CDL version: {version}")

        session = _read_string(f)
        (module_count,) = struct.unpack("<I", f.read(4))
        modules: Dict[str, CDLModule] = {}
        for _ in range(module_count):
            mid = _read_string(f)
            (base,) = struct.unpack("<I", f.read(4))
            (size,) = struct.unpack("<I", f.read(4))
            source_file = _read_string(f)
            (source_offset,) = struct.unpack("<I", f.read(4))
            (entry_count,) = struct.unpack("<I", f.read(4))
            entry_points = tuple(
                struct.unpack("<I", f.read(4))[0] for _ in range(entry_count)
            )
            (ev_count,) = struct.unpack("<I", f.read(4))
            bytes_data = [
                struct.unpack("<IIHH", f.read(12)) for _ in range(ev_count)
            ]
            modules[mid] = CDLModule(
                id=mid,
                base=base,
                size=size,
                source_file=source_file,
                source_offset=source_offset,
                entry_points=entry_points,
                bytes=bytes_data,
            )
    return session, modules
