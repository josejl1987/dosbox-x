"""Build a unified memory map from CDL evidence, KLB write ranges, and KHD archives."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

from pc98rev.khd_parser import parse_khd as parse_khd_file
from pc98rev.decompressor import Decompressor


@dataclass
class WriteBlock:
    archive: str
    entry_name: str
    file_offset: int
    ret_cs: int
    ret_ip: int
    ranges: list[tuple[int, int]] = field(default_factory=list)

    @property
    def total_bytes(self) -> int:
        return sum(hi - lo + 1 for lo, hi in self.ranges)

    @property
    def min_addr(self) -> int:
        return min(lo for lo, _ in self.ranges) if self.ranges else 0

    @property
    def max_addr(self) -> int:
        return max(hi for _, hi in self.ranges) if self.ranges else 0


def parse_writes_file(path: Path) -> WriteBlock | None:
    """Parse a klb_<handle>_<offset>_<cs>_<ip>_writes.txt file."""
    m = re.match(r"klb_\d+_(\d+)_([0-9A-Fa-f]+)_([0-9A-Fa-f]+)_writes\.txt", path.name)
    if not m:
        return None
    file_offset = int(m.group(1))
    ret_cs = int(m.group(2), 16)
    ret_ip = int(m.group(3), 16)

    ranges: list[tuple[int, int]] = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line:
            continue
        m2 = re.match(r"0x([0-9A-Fa-f]+)-0x([0-9A-Fa-f]+)", line)
        if m2:
            ranges.append((int(m2.group(1), 16), int(m2.group(2), 16)))
    return WriteBlock(
        archive="", entry_name="", file_offset=file_offset,
        ret_cs=ret_cs, ret_ip=ret_ip, ranges=ranges,
    )


def build_memory_map(incoming_dir: Path, game_dir: Path) -> list[WriteBlock]:
    """Build the full memory map from captured write ranges and KHD directories."""
    import json

    # Parse all KHD files for entry lookup
    # Map: archive_name -> [(klb_offset, padded_size, entry_name, compressed, size_bytes)]
    khd_lookup: dict[str, list[tuple[int, int, str, bool, int]]] = {}
    for khd_path in sorted(game_dir.glob("*.KHD")):
        archive = khd_path.stem.upper()
        _, _, entries = parse_khd_file(khd_path)
        khd_lookup[archive] = [
            (e.offset_in_klb, e.size_paragraphs * 16, e.filename, e.compressed, e.size_bytes)
            for e in entries
        ]

    # Parse event logs to build (handle, file_offset, ret_cs, ret_ip) -> filename mapping
    # from klb_read_return events
    ret_key_to_filename: dict[tuple[int, int, int, int], str] = {}
    for evf in sorted(incoming_dir.glob("events_*.jsonl")):
        with evf.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    ev = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if ev.get("type") == "klb_read_return":
                    key = (
                        ev.get("handle", 0),
                        ev.get("file_offset", 0),
                        ev.get("ret_cs", 0),
                        ev.get("ret_ip", 0),
                    )
                    ret_key_to_filename[key] = ev.get("filename", "")
                elif ev.get("type") == "file_read_complete":
                    # Also build a mapping from (filename, file_offset) -> klb_entry
                    # via the klb_entry field added during ingestion
                    pass

    # Parse all writes files
    writes_dir = incoming_dir / "modules"
    blocks: list[WriteBlock] = []
    for wf in sorted(writes_dir.glob("klb_*_writes.txt")):
        block = parse_writes_file(wf)
        if not block or not block.ranges:
            continue

        # Look up the KLB filename from the event log
        key = (5, block.file_offset, block.ret_cs, block.ret_ip)  # handle=5 is BR1's
        # Try all possible handle values
        for h in range(256):
            key = (h, block.file_offset, block.ret_cs, block.ret_ip)
            if key in ret_key_to_filename:
                break
        filename = ret_key_to_filename.get(key, "")

        # Extract archive name from filename (e.g. "BR_MAIN.KLB" -> "BR_MAIN")
        archive = ""
        if filename:
            import os
            base = os.path.basename(filename).upper()
            archive = base.replace(".KLB", "")

        # Look up KHD entry
        entries = khd_lookup.get(archive, [])
        for klb_off, padded, name, comp, size in entries:
            if block.file_offset >= klb_off and block.file_offset < klb_off + padded:
                block.archive = archive
                block.entry_name = name
                break

        if not block.archive and filename:
            block.archive = archive or "???"

        blocks.append(block)

    return blocks


def print_memory_map(blocks: list[WriteBlock], cdl_modules: list[dict] | None = None) -> None:
    """Print a human-readable memory map."""
    # Collect all addresses into 256-byte blocks
    addr_map: dict[int, dict] = {}  # block_addr -> {entries, code, data, stack}

    for block in blocks:
        for lo, hi in block.ranges:
            addr = lo & ~0xFF  # align to 256-byte block
            while addr <= hi:
                key = addr
                if key not in addr_map:
                    addr_map[key] = {"writes": [], "entries": set()}
                addr_map[key]["writes"].append((lo, hi))
                addr_map[key]["entries"].add(block.entry_name or f"0x{block.file_offset:X}")
                addr += 256

    # CDL module boundaries
    modules = cdl_modules or []
    mod_for_addr = lambda a: next(
        (m for m in modules if a >= m["base"] and a < m["base"] + m["size"]), None
    )

    print("=" * 90)
    print(f"{'Addr':>10s}  {'Module':16s}  {'Archive Entries':40s}  {'Write Sources'}")
    print("=" * 90)

    for addr in sorted(addr_map.keys()):
        info = addr_map[addr]
        mod = mod_for_addr(addr)
        mod_name = f"{mod['module_id']}@0x{mod['base']:05X}" if mod else "---"
        entries = ", ".join(sorted(info["entries"])[:3])
        n_writes = len(info["writes"])
        print(f"  0x{addr:05X}  {mod_name:16s}  {entries:40s}  {n_writes} range(s)")
