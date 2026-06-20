"""List file-read/write events for real game files (excluding CON/PRN)."""
from __future__ import annotations

import json
import sqlite3
from collections import defaultdict
from pathlib import Path


def fmt_cs_ip(ev: dict) -> str:
    return f"{ev.get('caller_cs', 0):04X}:{ev.get('caller_ip', 0):04X}"


def main(db_path: str = "pc98rev.db") -> None:
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    rows = conn.execute(
        "SELECT data FROM events WHERE type IN ("
        "'file_open_complete','file_read_complete','file_write_complete',"
        "'file_seek_complete','file_close') ORDER BY id"
    ).fetchall()

    by_file: dict[str, list[dict]] = defaultdict(list)
    for row in rows:
        ev = json.loads(row["data"])
        name = ev.get("filename", "")
        if not name or name in ("CON", "PRN", "AUX", "NUL"):
            continue
        by_file[name].append(ev)

    print(f"Distinct game files touched: {len(by_file)}\n")

    for filename, events in sorted(by_file.items()):
        reads = [e for e in events if e["type"] == "file_read_complete"]
        writes = [e for e in events if e["type"] == "file_write_complete"]
        seeks = [e for e in events if e["type"] == "file_seek_complete"]
        total_read = sum(r.get("actual", 0) for r in reads)
        total_write = sum(w.get("actual", 0) for w in writes)
        callers = sorted(set(fmt_cs_ip(e) for e in events))

        print(f"{filename}")
        print(f"  opens/closes: {len([e for e in events if e['type']=='file_open_complete'])} / {len([e for e in events if e['type']=='file_close'])}")
        print(f"  reads: {len(reads)}  bytes: {total_read}")
        print(f"  writes: {len(writes)}  bytes: {total_write}")
        print(f"  seeks: {len(seeks)}")
        print(f"  unique callers: {', '.join(callers[:5])}{' ...' if len(callers) > 5 else ''}")

        # Show read pattern (offset -> size) first few to guess compression
        if reads:
            print("  read pattern (offset:requested:actual):")
            for r in reads[:10]:
                print(f"    0x{r.get('file_offset', 0):05X} : {r.get('requested', 0):6} : {r.get('actual', 0):6}  @{fmt_cs_ip(r)}")
            if len(reads) > 10:
                print(f"    ... {len(reads) - 10} more reads")
        print()


if __name__ == "__main__":
    main()
