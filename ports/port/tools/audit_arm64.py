#!/usr/bin/env python3
"""Inventory native ARM64/iOS blockers without bypassing the port's ABI guard.

This compiles header-only layout probes, never game code or cartridge assets.
The corrected-word probe is hypothetical: it demonstrates that changing `long`
to a 32-bit type alone does not make native pointers or serialized records safe.
No game is linked or run, and no source file is rewritten.
"""
from __future__ import annotations

import argparse
import ast
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[3]
EXPRESSIONS = {
    "word_size": "sizeof(Sint32)",
    "pointer_size": "sizeof(void *)",
    "player_work_size": "sizeof(PLAYER_WORK)",
    "effect_slot_size": "sizeof(WORK_BUFF)",
    "effect_prefix_size": "__builtin_offsetof(PLAYER_WORK, act_st)",
    "character_part_copy_size": "sizeof(CHAR_PART)",
    "character_part_field_span": "__builtin_offsetof(PLAYER_WORK, atck_flag) - __builtin_offsetof(PLAYER_WORK, tile_top)",
    "animation_copy_size": "sizeof(CHAR_ANIM)",
    "animation_field_span": "__builtin_offsetof(PLAYER_WORK, cg_adrs) - __builtin_offsetof(PLAYER_WORK, cg_prm)",
    "texture_header_size": "sizeof(NLthd)",
    "texture_global_index_offset": "__builtin_offsetof(NLthd, globl_index)",
    "object_file_size": "sizeof(OBJ_FILE)",
    "switch_size": "sizeof(SW_WORK)",
    "cpu_data_size": "sizeof(CPU_DATA)",
    "hit_data_size": "sizeof(HIT_DATA)",
    "frame_box_size": "sizeof(FRAME_BOX)",
    "script_record_size": "sizeof(CPU_SCRIPT)",
}

FIXED_WORDS = """
#define TYPES_H
typedef signed char Sint8;
typedef unsigned char Uint8;
typedef signed short Sint16;
typedef unsigned short Uint16;
typedef signed int Sint32;
typedef unsigned int Uint32;
typedef float Float;
#ifndef NULL
#define NULL ((void *)0)
#endif
"""


def probe_source(fixed_words: bool) -> str:
    prefix = FIXED_WORDS if fixed_words else ""
    declarations = "\n".join(
        f"unsigned int mvc2_abi_{name} = {expression};"
        for name, expression in EXPRESSIONS.items())
    return prefix + '#include "port_prefix.h"\n#include "player.h"\n' + declarations + "\n"


def parse_assembly(text: str) -> dict[str, int]:
    """Read constant globals emitted by GCC/Clang, including Mach-O underscores.

    No target executable is run. A missing or ambiguous constant is an explicit
    probe error, rather than silently assuming the desired layout.
    """
    values: dict[str, int] = {}
    current = None
    for line in text.splitlines():
        label = re.match(r"^_?mvc2_abi_([a-z_]+):(?:\s*(?:[#;]|//).*)?\s*$", line)
        if label:
            current = label[1]
            continue
        if current:
            word = re.match(r"\s*\.(?:long|word|4byte)\s+((?:0x[0-9a-fA-F]+)|(?:[0-9]+))(?:\s|$)", line)
            zero = re.match(r"\s*\.(?:zero|space)\s+4(?:\s|$|,)", line)
            if word or zero:
                if current in values:
                    raise ValueError("duplicate layout constant: " + current)
                values[current] = int(word[1], 0) if word else 0
                current = None
            elif line and not line[0].isspace() and not line.startswith(("#", ";", "//")):
                current = None
    missing = EXPRESSIONS.keys() - values.keys()
    if missing:
        raise ValueError("compiler assembly lacks layout constants: " + ", ".join(sorted(missing)))
    return values


def compile_probe(compiler: list[str], root: Path, flags: list[str],
                  fixed_words: bool, program: str) -> dict:
    with tempfile.TemporaryDirectory(prefix="mvc2-abi-probe-") as temporary:
        source = Path(temporary) / "layout.c"
        output = Path(temporary) / "layout.s"
        source.write_text(probe_source(fixed_words))
        arguments = [*compiler, *flags, "-std=c11", "-fno-builtin", "-S",
                     "-I", str(root / "include"), "-I", str(root / "ports/port/include")]
        if program == "dc":
            arguments += ["-DTARGET_DC=1", "-DBUILD_DATE=20000529"]
        arguments += [str(source), "-o", str(output)]
        result = subprocess.run(arguments, text=True, capture_output=True, check=False)
        if result.returncode:
            return {"available": False, "flags": flags, "error": result.stderr.strip()}
        try:
            layout = parse_assembly(output.read_text())
        except (OSError, ValueError) as error:
            return {"available": False, "flags": flags, "error": str(error),
                    "diagnostics": result.stderr.strip()}
        return {"available": True, "flags": flags, "layout": layout,
                "diagnostics": result.stderr.strip()}


def source_location(root: Path, path: str, pattern: str) -> dict:
    compiled = re.compile(pattern)
    for number, line in enumerate((root / path).read_text().splitlines(), 1):
        if compiled.search(line):
            return {"path": path, "line": number, "text": line.strip()}
    return {"path": path, "line": None, "text": None,
            "note": "expected source pattern changed; inspect the implementation"}


def source_blockers(root: Path) -> list[dict]:
    specs = [
        ("explicit_32_bit_guard", "The existing build rejects pointers wider than four bytes.",
         "ports/CMakeLists.txt", r"NOT CMAKE_SIZEOF_VOID_P EQUAL 4"),
        ("host_pointer_truncation", "A native function/data pointer is reduced to a serialized 32-bit value.",
         "ports/port/naomi/naomi_board.c", r"uint32_t v = \(uint32_t\)\(uintptr_t\)r->to"),
        ("four_byte_relocation_store", "Relocations write four bytes into program data; widening the store corrupts neighboring fields.",
         "ports/port/naomi/naomi_board.c", r"memcpy\(r->at, &v, 4\)"),
        ("absolute_data_aliases", "Generated data/buffer symbols use GNU linker aliases at original RAM addresses.",
         "ports/port/tools/gen_host.py", r"--defsym"),
        ("fixed_ram", "The game depends on its original address space instead of owned native allocations.",
         "ports/port/include/naomi_host.h", r"#define NAOMI_RAM_BASE"),
        ("gnu_relocatable_pipeline", "GNU relocatable-object localization and aliases require a deliberate Mach-O build replacement.",
         "ports/CMakeLists.txt", r"--redefine-syms=.*-G"),
        ("arm32_coroutine", "This stack switch saves ARM32 registers and cannot execute as an ARM64 coroutine.",
         "ports/port/sys/coro_arm.c", r"push \{r4-r11, lr\}"),
        ("arm32_division_helpers", "ARM32 AEABI wrappers do not define ARM64 C division/remainder-by-zero behavior.",
         "ports/port/sys/divzero_arm.c", r"__wrap___aeabi_idiv\("),
        ("serialized_texture_pointer", "The host texture descriptor contains a native pointer but is overlaid on 16-byte game records.",
         "ports/port/nl/nl_port.h", r"void\* address"),
    ]
    return [{"id": identifier, "issue": issue,
             "evidence": source_location(root, path, pattern)}
            for identifier, issue, path, pattern in specs]


def read_program_definitions(root: Path) -> dict:
    """Read gen_host's literal definitions without executing generator code."""
    tree = ast.parse((root / "ports/port/tools/gen_host.py").read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(n, ast.Name) and n.id == "PROGRAMS"
                                              for n in node.targets):
            if not isinstance(node.value, ast.Dict):
                break
            result = {}
            for key, call in zip(node.value.keys, node.value.values):
                if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Name) or call.func.id != "Program":
                    raise ValueError("gen_host PROGRAMS has a nonliteral Program definition")
                values = [ast.literal_eval(argument) for argument in call.args]
                if len(values) != 8:
                    raise ValueError("gen_host Program definition has changed")
                result[ast.literal_eval(key)] = dict(zip(
                    ("name", "symbols", "pointers", "sub", "code", "data", "start", "end"), values))
            return result
    raise ValueError("cannot find gen_host's literal PROGRAMS definitions")


def pointer_inventory(root: Path) -> dict:
    result = {}
    for name, program in read_program_definitions(root).items():
        path = root / program["pointers"]
        if not path.is_file():
            path = root / "build" / program["pointers"]
        if not path.is_file():
            result[name] = {"available": False, "error": "pointer table is not present"}
            continue
        counts = {"all_pointer_words": 0, "words_in_program_data": 0,
                  "data_words_targeting_code_regions": 0, "data_words_targeting_data_regions": 0}
        for line in path.read_text().splitlines():
            words = line.split()
            if len(words) != 2:
                continue
            at, target = (int(word, 16) for word in words)
            counts["all_pointer_words"] += 1
            if not any(lo <= at < hi for lo, hi in program["data"]):
                continue
            counts["words_in_program_data"] += 1
            if any(lo <= target < hi for lo, hi in program["code"]):
                counts["data_words_targeting_code_regions"] += 1
            if any(lo <= target < hi for lo, hi in program["data"]):
                counts["data_words_targeting_data_regions"] += 1
        result[name] = {"available": True, "source": str(path.relative_to(root)), **counts,
                        "note": "Code-region references are callback candidates, not proof that every word is called."}
    return result


def audit(compiler: list[str], root: Path, flags32: list[str]) -> dict:
    version = subprocess.run([*compiler, "--version"], capture_output=True, text=True, check=False)
    layouts = {}
    for program in ("arcade", "dc"):
        layouts[program] = {
            "current_host_headers": compile_probe(compiler, root, [], False, program),
            "hypothetical_fixed_words": compile_probe(compiler, root, [], True, program),
            "reference_32_bit": compile_probe(compiler, root, flags32, True, program),
        }
        reference = layouts[program]["reference_32_bit"]
        if reference["available"] and reference["layout"]["pointer_size"] != 4:
            reference["available"] = False
            reference["error"] = "the reference flags did not select a 32-bit pointer target"
    return {
        "format_version": 1,
        "status": "arm64_gameplay_not_ready",
        "compiler": version.stdout.splitlines()[0] if version.stdout else " ".join(compiler),
        "scope": "header layouts and source inventory only; no game or IPA built; no ROM data read",
        "layout_scope": "Native host measurements, not an Apple ARM64 compilation. The fixed-word probe only isolates pointer/alignment effects.",
        "layouts": layouts,
        "source_blockers": source_blockers(root),
        "pointer_inventory": pointer_inventory(root),
        "required_adaptations": [
            "Preserve the existing 32-bit build guard until an actual ARM64 engine path is implemented.",
            "Use fixed-width scalar words while keeping native C pointers distinct from serialized 32-bit references.",
            "Replace fixed-address buffer/data aliases with owned native storage and typed address resolution.",
            "Decode pointer-bearing model/texture records into native descriptors; do not widen fields in-place.",
            "Reconstruct native callback tables or resolve typed callback IDs; never truncate native function pointers.",
            "Adapt work-pool slots, fixed-span copies, effect overlays and pointer arithmetic for native structure sizes.",
            "Replace the GNU relocatable/alias build, ARM32 coroutine, RAM mapper and division helpers for Apple ARM64.",
            "Integrate the existing native renderer/audio backend with the iOS lifecycle, then validate gameplay with real user assets.",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--flags-32", default="-m32", help="compiler flags for the optional reference probe")
    parser.add_argument("--json", type=Path, help="write complete layout/source report")
    parser.add_argument("--report-only", action="store_true", help="collect this unfinished-port inventory without failing the command")
    args = parser.parse_args(argv)
    compiler = shlex.split(args.cc)
    if not compiler or not shutil.which(compiler[0]):
        parser.error("requested C compiler is not available")
    try:
        report = audit(compiler, ROOT, shlex.split(args.flags_32))
    except (OSError, ValueError, SyntaxError) as error:
        print("ARM64 audit failed: " + str(error), file=sys.stderr)
        return 2
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    print("ARM64 gameplay is not ready; this report does not build or run the game.")
    for program, probes in report["layouts"].items():
        for name, probe in probes.items():
            if probe["available"]:
                value = probe["layout"]
                print(f"{program} {name}: word={value['word_size']}, pointer={value['pointer_size']}, "
                      f"slot={value['effect_slot_size']}, effect-prefix={value['effect_prefix_size']}, "
                      f"texture-header={value['texture_header_size']}")
            else:
                print(f"{program} {name}: probe unavailable; see JSON diagnostics")
    print(f"Source mechanisms requiring adaptation: {len(report['source_blockers'])}.")
    if args.json:
        print("Full report: " + str(args.json))
    return 0 if args.report_only else 1


if __name__ == "__main__":
    raise SystemExit(main())
