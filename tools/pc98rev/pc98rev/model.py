"""Canonical SQLite database model for modules, spans, relocations and text."""

from __future__ import annotations

import hashlib
import json
import sqlite3
from dataclasses import asdict, dataclass, is_dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

from pc98rev.ingest_cdl import CDLModule, parse_cdl


@dataclass(frozen=True)
class SourceRef:
    file: str
    offset: int
    length: int = 0

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict) -> SourceRef:
        return cls(**d)


@dataclass(frozen=True)
class Module:
    id: str
    source: SourceRef
    load_destination_segment: Union[str, int]
    load_destination_offset: int
    entrypoints: tuple
    runtime_image_hash: str = ""

    def to_dict(self) -> dict:
        data = asdict(self)
        data["entrypoints"] = list(self.entrypoints)
        data["source"] = self.source.to_dict()
        return data

    @classmethod
    def from_dict(cls, d: dict) -> Module:
        d = dict(d)
        d["source"] = SourceRef.from_dict(d["source"])
        d["entrypoints"] = tuple(d.get("entrypoints", []))
        return cls(**d)


@dataclass(frozen=True)
class Span:
    module_id: str
    start: int
    end: int
    kind: str
    confidence: str
    provenance: List[dict]

    def to_dict(self) -> dict:
        return asdict(self)

    @classmethod
    def from_dict(cls, d: dict) -> Span:
        return cls(**d)


class CanonicalDatabase:
    """SQLite-backed canonical store."""

    def __init__(self, path: Union[str, Path] = ":memory:") -> None:
        self.path = Path(path) if path != ":memory:" else ":memory:"
        self.conn = sqlite3.connect(str(self.path))
        self.conn.row_factory = sqlite3.Row
        self._ensure_schema()

    def _ensure_schema(self) -> None:
        self.conn.executescript(
            """
            CREATE TABLE IF NOT EXISTS modules (
                id TEXT PRIMARY KEY,
                data TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS spans (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                module_id TEXT NOT NULL,
                start INTEGER NOT NULL,
                end INTEGER NOT NULL,
                kind TEXT NOT NULL,
                confidence TEXT NOT NULL,
                provenance TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                type TEXT NOT NULL,
                timestamp REAL DEFAULT (unixepoch()),
                data TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS idx_spans_module ON spans(module_id, start, end);
            CREATE INDEX IF NOT EXISTS idx_events_type ON events(type);

            CREATE TABLE IF NOT EXISTS cdl_sessions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_name TEXT NOT NULL,
                ingested_at REAL DEFAULT (unixepoch())
            );
            CREATE TABLE IF NOT EXISTS cdl_modules (
                session_id INTEGER NOT NULL,
                module_id TEXT NOT NULL,
                base INTEGER NOT NULL,
                size INTEGER NOT NULL,
                source_file TEXT NOT NULL,
                source_offset INTEGER NOT NULL,
                entry_points TEXT NOT NULL,
                PRIMARY KEY (session_id, module_id)
            );
            CREATE TABLE IF NOT EXISTS cdl_bytes (
                session_id INTEGER NOT NULL,
                module_id TEXT NOT NULL,
                offset INTEGER NOT NULL,
                access INTEGER NOT NULL,
                origin INTEGER NOT NULL,
                analysis INTEGER NOT NULL,
                PRIMARY KEY (session_id, module_id, offset)
            ) WITHOUT ROWID;
            CREATE INDEX IF NOT EXISTS idx_cdl_bytes_module
                ON cdl_bytes(session_id, module_id, offset);
            """
        )
        self.conn.commit()

    def add_module(self, module: Module) -> None:
        self.conn.execute(
            "INSERT OR REPLACE INTO modules (id, data) VALUES (?, ?)",
            (module.id, json.dumps(module.to_dict())),
        )
        self.conn.commit()

    def get_module(self, module_id: str) -> Optional[Module]:
        row = self.conn.execute(
            "SELECT data FROM modules WHERE id = ?", (module_id,)
        ).fetchone()
        if row is None:
            return None
        return Module.from_dict(json.loads(row["data"]))

    def add_span(self, span: Span) -> None:
        self.conn.execute(
            "INSERT INTO spans (module_id, start, end, kind, confidence, provenance) VALUES (?, ?, ?, ?, ?, ?)",
            (span.module_id, span.start, span.end, span.kind, span.confidence, json.dumps(span.provenance)),
        )
        self.conn.commit()

    def add_event(self, event_type: str, data: dict) -> None:
        self.conn.execute(
            "INSERT INTO events (type, data) VALUES (?, ?)",
            (event_type, json.dumps(data, sort_keys=True)),
        )
        self.conn.commit()

    def ingest_cdl(self, path: Union[str, Path]) -> int:
        """Ingest a binary .p98cdl file into cdl_* tables."""
        path = Path(path)
        session, modules = parse_cdl(path)
        cur = self.conn.cursor()
        cur.execute("INSERT INTO cdl_sessions (session_name) VALUES (?)", (session,))
        session_id = cur.lastrowid

        rows: List[Tuple[int, str, int, int, int, int, str, int, str]] = []
        byte_rows: List[Tuple[int, str, int, int, int, int]] = []
        for m in modules.values():
            rows.append(
                (
                    session_id,
                    m.id,
                    m.base,
                    m.size,
                    m.source_file,
                    m.source_offset,
                    json.dumps(list(m.entry_points)),
                )
            )
            for offset, access, origin, analysis in m.bytes:
                byte_rows.append((session_id, m.id, offset, access, origin, analysis))

        cur.executemany(
            "INSERT OR IGNORE INTO cdl_modules "
            "(session_id, module_id, base, size, source_file, source_offset, entry_points) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            rows,
        )
        cur.executemany(
            "INSERT OR REPLACE INTO cdl_bytes "
            "(session_id, module_id, offset, access, origin, analysis) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            byte_rows,
        )
        self.conn.commit()
        return session_id

    def modules(self) -> List[Module]:
        rows = self.conn.execute("SELECT data FROM modules").fetchall()
        return [Module.from_dict(json.loads(r["data"])) for r in rows]

    def events(self, event_type: Optional[str] = None) -> List[dict]:
        if event_type:
            rows = self.conn.execute(
                "SELECT data FROM events WHERE type = ? ORDER BY id", (event_type,)
            ).fetchall()
        else:
            rows = self.conn.execute("SELECT data FROM events ORDER BY id").fetchall()
        return [json.loads(r["data"]) for r in rows]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()
