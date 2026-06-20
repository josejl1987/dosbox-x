"""Transparent zstd compression helpers for capture files.

Captures are written raw by DOSBox-X during runtime.  This module lets the
Python side compress them for archival and read them back transparently,
regardless of whether the file is raw or zstd-compressed (``.zst`` suffix).
"""

from __future__ import annotations

import io
from pathlib import Path
from typing import BinaryIO, Iterable, Union

import zstandard as zstd

PathLike = Union[str, Path]


ZSTD_MAGIC = b"\x28\xB5\x2F\xFD"


def is_zstd(path: PathLike) -> bool:
    """Return True if the file starts with the zstd magic bytes."""
    p = Path(path)
    if not p.is_file():
        return False
    if p.suffix == ".zst":
        return True
    with p.open("rb") as f:
        return f.read(4) == ZSTD_MAGIC


def open_read(path: PathLike) -> BinaryIO:
    """Open *path* for reading, transparently decompressing zstd."""
    p = Path(path)
    if not p.is_file():
        # Allow callers to attempt reading and get a normal FileNotFoundError.
        return p.open("rb")
    if is_zstd(p):
        return io.BytesIO(decompress_file(p))
    return p.open("rb")


def compress_file(src: PathLike, dst: PathLike | None = None, level: int = 12) -> Path:
    """Compress *src* to zstd and return the output path.

    If *dst* is omitted, writes to ``src`` with an added ``.zst`` suffix.
    """
    src_path = Path(src)
    dst_path = Path(dst) if dst else src_path.with_suffix(src_path.suffix + ".zst")
    cctx = zstd.ZstdCompressor(level=level)
    with src_path.open("rb") as ifh, dst_path.open("wb") as ofh:
        cctx.copy_stream(ifh, ofh)
    return dst_path


def decompress_file(src: PathLike, dst: PathLike | None = None) -> Path | bytes:
    """Decompress *src* and write to *dst* or return bytes."""
    src_path = Path(src)
    dctx = zstd.ZstdDecompressor()
    with src_path.open("rb") as ifh:
        if dst is None:
            return dctx.stream_reader(ifh).read()
        dst_path = Path(dst)
        with dst_path.open("wb") as ofh:
            dctx.copy_stream(ifh, ofh)
        return dst_path


def iter_compressed_files(root: PathLike, patterns: Iterable[str] = ()) -> list[Path]:
    """Find uncompressed capture files matching *patterns* under *root*."""
    root_path = Path(root)
    candidates: list[Path] = []
    if patterns:
        for pat in patterns:
            candidates.extend(root_path.rglob(pat))
    else:
        candidates = [p for p in root_path.rglob("*") if p.is_file()]
    return [p for p in candidates if not is_zstd(p) and not p.name.endswith(".zst")]
