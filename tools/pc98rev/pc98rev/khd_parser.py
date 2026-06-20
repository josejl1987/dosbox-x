"""Parse KLIB KHD archive header files (corrected from brandish2 source)."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass
class KHDEntry:
    filename: str        # 13-byte space-padded 8.3 name
    compressed: bool     # byte at offset 0x0D
    time: int            # uint16 LE at offset 0x10
    date: int            # uint16 LE at offset 0x12
    offset_in_klb: int   # uint32 LE at offset 0x14
    size_bytes: int      # uint16 LE at offset 0x18
    size_paragraphs: int # uint16 LE at offset 0x1A


def parse_khd(path: Path) -> tuple[str, int, list[KHDEntry]]:
    """Parse a .p98cdl file and return (magic, count, entries)."""
    data = path.read_bytes()
    magic = data[0:8].rstrip(b"\x00").decode("ascii", errors="replace")
    count = struct.unpack_from("<H", data, 9)[0]
    entries: list[KHDEntry] = []
    for i in range(count):
        base = 16 + i * 32
        if base + 32 > len(data):
            break
        if data[base] == 0 or data[base] == 0xFF:
            continue
        raw_name = data[base : base + 13]
        name = raw_name.decode("ascii", errors="replace").rstrip("\x00 \x20")
        compressed = data[base + 0x0D] != 0
        time_val = struct.unpack_from("<H", data, base + 0x10)[0]
        date_val = struct.unpack_from("<H", data, base + 0x12)[0]
        offset_in_klb = struct.unpack_from("<I", data, base + 0x14)[0]
        size_bytes = struct.unpack_from("<H", data, base + 0x18)[0]
        size_paragraphs = struct.unpack_from("<H", data, base + 0x1A)[0]
        entries.append(KHDEntry(
            filename=name,
            compressed=compressed,
            time=time_val,
            date=date_val,
            offset_in_klb=offset_in_klb,
            size_bytes=size_bytes,
            size_paragraphs=size_paragraphs,
        ))
    return magic, count, entries


def main() -> None:
    import sys
    files = sorted(Path(sys.argv[1] if len(sys.argv) > 1 else "C:/br1jp/BR1_98").glob("*.KHD"))
    for khd_path in files:
        magic, count, entries = parse_khd(khd_path)
        klb_path = khd_path.with_suffix(".KLB")
        klb_size = klb_path.stat().st_size if klb_path.exists() else 0

        print(f"\n{'='*80}")
        print(f"{khd_path.name}  magic={magic}  entries={count}  klb_size={klb_size}")
        print(f"{'='*80}")
        print(f"  {'#':>3}  {'Name':16s}  {'Comp':>4}  {'Offset':>8s}  {'SizeBytes':>9s}  {'SizePara':>8s}  {'PaddedSize':>10s}")
        print(f"  {'---':>3}  {'----':16s}  {'----':>4}  {'------':>8s}  {'---------':>9s}  {'--------':>8s}  {'----------':>10s}")

        for i, e in enumerate(entries):
            padded = e.size_paragraphs * 16
            print(
                f"  {i:>3}  {e.filename:16s}  {'BZH' if e.compressed else 'raw':>4}"
                f"  0x{e.offset_in_klb:06X}  {e.size_bytes:>9}  {e.size_paragraphs:>8}"
                f"  {padded:>10}"
            )


if __name__ == "__main__":
    main()
