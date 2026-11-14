#!/usr/bin/env python3
"""Check native C sprite counts against the original SH-4 assembly source."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[4]
assembly = (ROOT / "src/spr_tbl.src").read_text()
native = (ROOT / "ports/port/native64/game_tables.c").read_text()

for name in ("spr_cnt_tbl", "spr_set_cnt_tbl"):
    source = assembly.split("_" + name + ":", 1)[1]
    source = re.split(r"\n\s*(?:_\w+:|\.END)", source, maxsplit=1)[0]
    expected = []
    for line in source.splitlines():
        match = re.search(r"\.DATA\.W\s+([\d,\s]+)", line)
        if match:
            expected.extend(int(token) for token in match[1].split(",") if token.strip())
    body = re.search(r"\b" + name + r"\[[^\]]+\]\s*=\s*\{([^}]*)\}", native, re.S)
    if body is None:
        raise SystemExit("Missing native table: " + name)
    actual = [int(token) for token in body[1].split(",") if token.strip()]
    if len(expected) != 59 or actual != expected:
        raise SystemExit("Native table differs from src/spr_tbl.src: " + name)
    print(name + ": all 59 entries match the reconstructed assembly table.")
