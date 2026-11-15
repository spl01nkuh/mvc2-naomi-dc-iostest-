#!/usr/bin/env python3
"""Verify native-only source guards preserve arcade/DC and existing host builds.

All selected game C files are preprocessed against a pinned original checkout,
with HOST_NATIVE64 absent. This is not a replacement for the matching SHC ROM
build, the working 32-bit ports, or native gameplay validation. Git history for
the reference revision is required (use checkout fetch-depth: 0 in CI).
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import difflib
import io
import os
from pathlib import Path, PurePosixPath
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[3]
REFERENCE = "6d9e0928395f5e8cc97935408909eea5e7d36c87"


def reference_tree(revision: str, destination: Path) -> None:
    result = subprocess.run(["git", "archive", "--format=tar", revision,
                             "src", "include", "ports/port/include"], cwd=ROOT,
                            capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError("Cannot read the reference revision; fetch its history first.\n" +
                           result.stderr.decode(errors="replace"))
    with tarfile.open(fileobj=io.BytesIO(result.stdout), mode="r:") as archive:
        for member in archive:
            path = PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts:
                raise RuntimeError("Unsafe reference path: " + member.name)
            if not member.isfile():
                continue
            source = archive.extractfile(member)
            if source is None:
                raise RuntimeError("Cannot read reference member: " + member.name)
            target = destination.joinpath(*path.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.read())


def sources(tree: Path, dc: bool) -> list[str]:
    selected = {path.name: path for path in (tree / "src").glob("*.c")}
    if dc:
        for path in (tree / "src/dc").glob("*.c"):
            if path.name != "strt2.c":
                selected[path.name] = path
    # These original C library modules are also compiled by the host port.
    libraries = [tree / "src/lib/libnaomi" / name for name in ("nlmath.c", "nlproj.c")]
    return sorted(str(path.relative_to(tree)) for path in [*selected.values(), *libraries])


def preprocess(compiler: list[str], tree: Path, source: str, dc: bool, host: bool) -> tuple[int, str, str]:
    command = [*compiler, "-E", "-P", "-nostdinc", "-Iinclude",
               "-D_STRICT_UNION_=1", "-D__DEV_TYPE_DC__=1"]
    command += ["-DBUILD_DATE=20000529", "-DTARGET_DC=1"] if dc else ["-DBUILD_DATE=20000225"]
    if host:
        command += ["-include", "ports/port/include/port_prefix.h"]
    result = subprocess.run(command + [source], cwd=tree, text=True,
                            capture_output=True, check=False)
    return result.returncode, result.stdout, result.stderr


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--ref", default=REFERENCE)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 6))
    args = parser.parse_args(argv)
    compiler = shlex.split(args.cc)
    if not compiler or not shutil.which(compiler[0]):
        parser.error("the requested C preprocessor is not available")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        with tempfile.TemporaryDirectory(prefix="mvc2-native64-reference-") as temporary:
            before = Path(temporary)
            reference_tree(args.ref, before)
            failures = []
            for dc, host in ((False, False), (False, True), (True, False), (True, True)):
                label = ("dc" if dc else "arcade") + ("-host32-guards" if host else "-original-guards")
                selected = sources(before, dc)

                def compare(source: str) -> tuple[str, str]:
                    old = preprocess(compiler, before, source, dc, host)
                    new = preprocess(compiler, ROOT, source, dc, host)
                    if old[0] or new[0]:
                        return source, "Preprocessing failed.\n" + old[2] + new[2]
                    if old[1] != new[1]:
                        difference = difflib.unified_diff(old[1].splitlines(), new[1].splitlines(),
                                                         fromfile="reference/" + source,
                                                         tofile="working/" + source, n=2)
                        return source, "\n".join(list(difference)[:40])
                    return source, ""

                with ThreadPoolExecutor(max_workers=args.jobs) as pool:
                    results = list(pool.map(compare, selected))
                changed = [(source, error) for source, error in results if error]
                for source, error in changed:
                    failures.append((label, source))
                    print(f"FAIL {label} {source}\n{error}", file=sys.stderr)
                print(f"{label}: {len(selected) - len(changed)}/{len(selected)} original C sources unchanged.")
            print("HOST_NATIVE64 remained disabled; this verifies guards, not gameplay or the matching ROM SHA-1.")
            return int(bool(failures))
    except (OSError, RuntimeError, tarfile.TarError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
