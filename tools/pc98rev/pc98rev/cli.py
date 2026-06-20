"""Command-line entry point for pc98rev."""

from __future__ import annotations

import sys
from pathlib import Path

import click

from pc98rev.model import CanonicalDatabase
from pc98rev.verify import format_report, verify_directory
from pc98rev.khd_parser import parse_khd as parse_khd_file
from pc98rev.decompressor import Decompressor
from pc98rev.memmap import build_memory_map, print_memory_map


def _fmt_size(num: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if num < 1024:
            return f"{num:.1f}{unit}"
        num /= 1024
    return f"{num:.1f}TB"


@click.group()
@click.option("--db", type=click.Path(), default="pc98rev.db")
@click.pass_context
def cli(ctx: click.Context, db: str) -> None:
    """PC-98 reverse-engineering toolchain."""
    ctx.ensure_object(dict)
    ctx.obj["db"] = CanonicalDatabase(Path(db))


@cli.command()
@click.argument("original", type=click.Path(exists=True, file_okay=False, path_type=Path))
@click.argument("rebuilt", type=click.Path(exists=True, file_okay=False, path_type=Path))
def verify(original: Path, rebuilt: Path) -> None:
    """Compare rebuilt files to originals and report mismatches."""
    mismatches = verify_directory(original, rebuilt)
    click.echo(format_report(mismatches))
    if mismatches:
        sys.exit(1)


@cli.command()
@click.argument("incoming", type=click.Path(exists=True, file_okay=False, path_type=Path))
@click.option("--game-dir", type=click.Path(exists=True, file_okay=False, path_type=Path),
              default=None, help="Game directory with .KHD/.KLB files for entry-name lookup")
@click.pass_context
def ingest(ctx: click.Context, incoming: Path, game_dir: Path | None) -> None:
    """Ingest a directory of DOSBox-X JSONL event files."""
    db = ctx.obj["db"]

    # Pre-parse all KHD files from the game directory for KLB entry lookup
    khd_map: dict[str, list] = {}  # archive_name -> [(offset, padded_size, name, compressed, size_bytes)]
    if game_dir:
        for khd_path in sorted(game_dir.glob("*.KHD")):
            archive = khd_path.stem.upper()
            magic, count, entries = parse_khd_file(khd_path)
            khd_map[archive] = [(e.offset_in_klb, e.size_paragraphs * 16, e.filename,
                                 e.compressed, e.size_bytes) for e in entries]
            click.echo(f"  KHD: {archive} ({count} entries)")

    def lookup_klb_entry(filename: str, offset: int) -> dict | None:
        base = Path(filename).name.upper()
        archive = base.replace(".KLB", "")
        entries = khd_map.get(archive)
        if not entries:
            return None
        for klb_off, padded, name, comp, size in entries:
            if offset >= klb_off and offset < klb_off + padded:
                return {"entry_name": name, "compressed": comp, "size_bytes": size,
                        "klb_offset": klb_off}
        return None

    for path in sorted(incoming.glob("*.jsonl")):
        click.echo(f"Ingesting {path.name}")
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                import json
                try:
                    data = json.loads(line)
                    # Enrich KLB reads with entry names from KHD
                    if game_dir and data.get("type") == "file_read_complete":
                        fname = data.get("filename", "")
                        if fname.upper().endswith(".KLB"):
                            entry = lookup_klb_entry(fname, data.get("file_offset", 0))
                            if entry:
                                data["klb_entry"] = entry
                    db.add_event(data.get("type", "unknown"), data)
                except json.JSONDecodeError as e:
                    click.echo(f"  skipped bad line: {e}", err=True)
    click.echo("Ingest complete.")


@cli.command("ingest-cdl")
@click.argument("cdl_file", type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.pass_context
def ingest_cdl(ctx: click.Context, cdl_file: Path) -> None:
    """Ingest a binary .p98cdl file into the canonical database."""
    db = ctx.obj["db"]
    session_id = db.ingest_cdl(cdl_file)

    stats = db.conn.execute(
        """
        SELECT
            (SELECT COUNT(DISTINCT module_id) FROM cdl_modules WHERE session_id = ?) AS modules,
            (SELECT COUNT(*) FROM cdl_bytes WHERE session_id = ?) AS bytes,
            (SELECT COUNT(DISTINCT offset) FROM cdl_bytes WHERE session_id = ? AND (access & 3) != 0) AS executed
        """,
        (session_id, session_id, session_id),
    ).fetchone()

    click.echo(f"Ingested CDL session {session_id} from {cdl_file.name}")
    click.echo(f"  modules: {stats['modules']}")
    click.echo(f"  evidence bytes: {stats['bytes']}")
    click.echo(f"  executed offsets: {stats['executed']}")


@cli.command("cdl-query")
@click.option("--session", type=int, default=None, help="CDL session ID (default: latest)")
@click.option("--module", type=str, default=None, help="Filter to a specific module")
@click.pass_context
def cdl_query(ctx: click.Context, session: int | None, module: str | None) -> None:
    """Query CDL evidence from the database."""
    db = ctx.obj["db"]

    if session is None:
        row = db.conn.execute(
            "SELECT id FROM cdl_sessions ORDER BY id DESC LIMIT 1"
        ).fetchone()
        if row is None:
            click.echo("No CDL sessions found.")
            return
        session = row["id"]

    click.echo(f"CDL session {session}")
    click.echo()

    mod_filter = "AND module_id = ?" if module else ""
    mod_params = [module] if module else []

    mods = db.conn.execute(
        f"""
        SELECT module_id, base, size, source_file,
               (SELECT COUNT(*) FROM cdl_bytes
                WHERE session_id = ? AND module_id = m.module_id) AS evidence,
               (SELECT COUNT(*) FROM cdl_bytes
                WHERE session_id = ? AND module_id = m.module_id AND (access & 1) != 0) AS insn_start,
               (SELECT COUNT(*) FROM cdl_bytes
                WHERE session_id = ? AND module_id = m.module_id AND (access & 4) != 0) AS data_read,
               (SELECT COUNT(*) FROM cdl_bytes
                WHERE session_id = ? AND module_id = m.module_id AND (access & 8) != 0) AS data_write,
               (SELECT COUNT(*) FROM cdl_bytes
                WHERE session_id = ? AND module_id = m.module_id AND (access & 16) != 0) AS stack_read,
               (SELECT COUNT(*) FROM cdl_bytes
                WHERE session_id = ? AND module_id = m.module_id AND (access & 32) != 0) AS stack_write
        FROM cdl_modules m
        WHERE session_id = ? {mod_filter}
        ORDER BY base
        """,
        (session, session, session, session, session, session, session, *mod_params),
    ).fetchall()

    for m in mods:
        pct = (m["evidence"] / m["size"] * 100) if m["size"] else 0
        click.echo(
            f"  {m['module_id']:16s}  base=0x{m['base']:05X}  size=0x{m['size']:X}"
            f"  evidence={m['evidence']}/{m['size']} ({pct:.1f}%)"
            f"  code={m['insn_start']}  dR={m['data_read']}  dW={m['data_write']}"
            f"  sR={m['stack_read']}  sW={m['stack_write']}"
        )
        if m["source_file"]:
            click.echo(f"    source: {m['source_file']}")

    # Entry points for BR1.EXE-like modules
    if module:
        eps = db.conn.execute(
            """
            SELECT entry_points FROM cdl_modules
            WHERE session_id = ? AND module_id = ?
            """,
            (session, module),
        ).fetchone()
        if eps and eps["entry_points"]:
            import json
            ep_list = json.loads(eps["entry_points"])
            click.echo(f"\n  Entry points ({len(ep_list)}):")
            for ep in sorted(ep_list)[:20]:
                click.echo(f"    0x{ep:04X}")
            if len(ep_list) > 20:
                click.echo(f"    ... {len(ep_list) - 20} more")

    # Conflicts: bytes marked as both code and data
    conflicts = db.conn.execute(
        f"""
        SELECT module_id, offset, access
        FROM cdl_bytes
        WHERE session_id = ? AND (access & 3) != 0 AND (access & 24) != 0
        {'AND module_id = ?' if module else ''}
        ORDER BY module_id, offset
        LIMIT 50
        """,
        (session, *mod_params) if module else (session,),
    ).fetchall()

    if conflicts:
        click.echo(f"\n  Code/data conflicts ({len(conflicts)} shown):")
        for c in conflicts:
            flags = []
            if c["access"] & 1: flags.append("InsnStart")
            if c["access"] & 2: flags.append("InsnByte")
            if c["access"] & 4: flags.append("DataRead")
            if c["access"] & 8: flags.append("DataWrite")
            if c["access"] & 16: flags.append("StackRead")
            if c["access"] & 32: flags.append("StackWrite")
            click.echo(f"    {c['module_id']:16s} +0x{c['offset']:04X}  access=0x{c['access']:04X} ({', '.join(flags)})")
    else:
        click.echo("\n  No code/data conflicts.")


def main() -> None:
    cli(obj={})


@cli.command("memmap")
@click.argument("game_dir", type=click.Path(exists=True, file_okay=False, path_type=Path))
@click.option("--incoming", type=click.Path(exists=True, file_okay=False, path_type=Path),
              default=Path("incoming"), help="Incoming events directory")
@click.pass_context
def memmap_cmd(ctx: click.Context, game_dir: Path, incoming: Path) -> None:
    """Display the memory map: archive entries mapped to memory addresses."""
    blocks = build_memory_map(incoming, game_dir)

    # Also pull CDL modules from the database
    cdl_modules = []
    try:
        db = ctx.obj["db"]
        rows = db.conn.execute(
            "SELECT module_id, base, size FROM cdl_modules ORDER BY base"
        ).fetchall()
        cdl_modules = [dict(r) for r in rows]
    except Exception:
        pass

    print_memory_map(blocks, cdl_modules)

    # Summary
    click.echo(f"\n{'='*90}")
    click.echo("Write blocks summary:")
    for b in blocks:
        comp = "BZH" if any(r[1] - r[0] > 1 for r in b.ranges) else "raw"
        click.echo(
            f"  {b.archive:10s}/{b.entry_name:16s}  "
            f"offset=0x{b.file_offset:X}  ret={b.ret_cs:04X}:{b.ret_ip:04X}  "
            f"ranges={len(b.ranges)}  bytes={b.total_bytes}  "
            f"addr=0x{b.min_addr:05X}-0x{b.max_addr:05X}"
        )


@cli.command("klb-extract")
@click.argument("game_dir", type=click.Path(exists=True, file_okay=False, path_type=Path))
@click.option("--archive", type=str, default=None, help="Archive name (e.g. BR_MAIN). Default: all.")
@click.option("--entry", type=str, default=None, help="Specific entry name to extract. Default: all.")
@click.option("--output-dir", type=click.Path(file_okay=False, path_type=Path), default=Path("extracted"))
@click.option("--raw", is_flag=True, help="Skip decompression, write raw compressed data.")
def klb_extract(game_dir: Path, archive: str | None, entry: str | None,
                output_dir: Path, raw: bool) -> None:
    """Extract and decompress entries from KHD/KLB archives."""
    output_dir.mkdir(parents=True, exist_ok=True)

    khd_files = sorted(game_dir.glob("*.KHD"))
    if archive:
        khd_files = [p for p in khd_files if p.stem.upper() == archive.upper()]

    total = 0
    for khd_path in khd_files:
        klb_path = khd_path.with_suffix(".KLB")
        if not klb_path.exists():
            continue
        magic, count, entries = parse_khd_file(khd_path)
        klb_data = klb_path.read_bytes()
        arch_name = khd_path.stem.upper()

        for e in entries:
            if entry and e.filename.upper().strip() != entry.upper().strip():
                continue

            raw_data = klb_data[e.offset_in_klb : e.offset_in_klb + e.size_bytes]

            if raw or not e.compressed:
                out_data = raw_data
                suffix = ".raw"
            else:
                dec = Decompressor(raw_data)
                out_data = dec.decompress()
                suffix = ".bin"
                if not out_data:
                    click.echo(f"  FAIL: {arch_name}/{e.filename} (decompression error)", err=True)
                    continue

            safe_name = e.filename.strip().replace(" ", "_")
            out_path = output_dir / f"{arch_name}_{safe_name}{suffix}"
            out_path.write_bytes(out_data)
            comp_str = f"compressed {e.size_bytes}B" if e.compressed else f"raw {e.size_bytes}B"
            click.echo(f"  {arch_name}/{e.filename:16s}  {comp_str} -> {len(out_data)}B  {out_path.name}")
            total += 1

    click.echo(f"\nExtracted {total} entries to {output_dir}/")


if __name__ == "__main__":
    main()
