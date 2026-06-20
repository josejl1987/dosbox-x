"""Parse and display KLIB KHD archive header files."""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass
class KHDEntry:
    name: str
    flags: int
    key: int
    offset: int
    extra: int


def parse_khd(path: Path) -> tuple[str, int, list[KHDEntry]]:
    data = path.read_bytes()
    magic = data[0:8].rstrip(b"\x00").decode("ascii", errors="replace")
    count = data[9]  # uint8 at offset 9
    entries: list[KHDEntry] = []
    for i in range(count):
        base = 16 + i * 32
        chunk = data[base : base + 32]
        name = chunk[0:12].rstrip(b"\x00 \x20").decode("ascii", errors="replace")
        flags = struct.unpack_from("<H", chunk, 12)[0]
        key = struct.unpack_from("<I", chunk, 16)[0]
        offset = struct.unpack_from("<I", chunk, 20)[0]
        extra = struct.unpack_from("<I", chunk, 24)[0]
        entries.append(KHDEntry(name, flags, key, offset, extra))
    return magic, count, entries


def main() -> None:
    files = sorted(Path(sys.argv[1] if len(sys.argv) > 1 else "C:/br1jp/BR1_98").glob("*.KHD"))
    for khd_path in files:
        magic, count, entries = parse_khd(khd_path)
        klb_path = khd_path.with_suffix(".KLB")
        klb_size = klb_path.stat().st_size if klb_path.exists() else 0

        print(f"\n{'='*72}")
        print(f"{khd_path.name}  magic={magic}  entries={count}  klb_size={klb_size}")
        print(f"{'='*72}")
        print(f"  {'#':>2}  {'Name':16s}  {'Flags':>5}  {'Key':>10s}  {'Offset':>8s}  {'Extra':>10s}  {'CSize':>8s}")
        print(f"  {'--':>2}  {'----':16s}  {'-----':>5}  {'---':>10s}  {'------':>8s}  {'-----':>10s}  {'-----':>8s}")

        for i, e in enumerate(entries):
            # Calculate compressed size from offset difference
            if i + 1 < count:
                csize = entries[i + 1].offset - e.offset
            else:
                csize = klb_size - e.offset
            print(
                f"  {i:>2}  {e.name:16s}  0x{e.flags:04X}  0x{e.key:08X}  "
                f"0x{e.offset:06X}  0x{e.extra:08X}  0x{csize:06X}"
            )


if __name__ == "__main__":
    main()
