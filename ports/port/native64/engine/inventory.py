#!/usr/bin/env python3
"""Inventory all compiled native engine dependencies without manufacturing code."""
import argparse
import json
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--objects", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    definitions = {}
    references = {}
    objects = args.objects.read_text().splitlines()
    for name in objects:
        if not name:
            continue
        output = subprocess.run([args.nm, "-g", name], check=True, text=True,
                                capture_output=True).stdout
        for line in output.splitlines():
            columns = line.split()
            if len(columns) < 2:
                continue
            kind, symbol = columns[-2:]
            if kind == "U":
                references.setdefault(symbol, []).append(name)
            elif kind.upper() == kind:
                definitions.setdefault(symbol, []).append(name)
    missing = {name: sources for name, sources in sorted(references.items())
               if name not in definitions}
    duplicates = {name: sources for name, sources in sorted(definitions.items())
                  if len(sources) > 1}
    result = dict(objects=len(objects), definitions=len(definitions),
                  missing=missing, duplicate_definitions=duplicates)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{len(objects)} objects; {len(missing)} external dependencies; "
          f"{len(duplicates)} multiply defined symbols")
    for name, sources in missing.items():
        print(f"  {name}: {', '.join(Path(source).name for source in sources)}")
    if duplicates:
        raise SystemExit("Native engine contains multiply defined symbols")


if __name__ == "__main__":
    main()
