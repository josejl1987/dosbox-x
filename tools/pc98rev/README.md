# pc98rev

A PC-98 reverse-engineering and lossless-rebuild toolchain designed to work with the DOSBox-X Lua integration.

This is an incremental milestone. The current phase provides:

- A canonical SQLite model for modules, spans, and text references.
- A baseline verifier that checks whether rebuilt files are byte-identical to originals.
- A JSONL event consumer for the native DOSBox-X `lua-re-hooks` data stream.

Later phases will add:

- Recursive 16-bit x86 analysis using `iced-x86` plus NEC V20/V30 support if needed.
- IDA importer/exporter.
- Shift-JIS text extraction and an external translation bank.
- Lossless NASM source emission.

## Layout

```
model.py      - canonical module/span/relocation/text database
verify.py     - byte-perfect roundtrip checks
ingest.py     - consume DOSBox-X JSONL event logs and memory dumps
cli.py        - command-line entry point
pc98rev.lua   - Lua orchestration bridge loaded inside DOSBox-X
```

## Installation

This package does not participate in the DOSBox-X autotools build. Install it directly with pip:

```bash
cd tools/pc98rev
pip install -e .
```

## Usage

```bash
# Verify rebuilt files match originals
pc98rev verify --original original/ --rebuilt build/

# Ingest DOSBox-X event stream and memory dumps
pc98rev ingest --incoming tools/pc98rev/incoming/
```
