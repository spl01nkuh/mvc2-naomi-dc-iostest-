#!/usr/bin/env python3
"""Checks for the ARM64 inventory; synthetic records and compiler probes only."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import audit_arm64 as audit


class AssemblyReaderTests(unittest.TestCase):
    def test_linux_and_macho_constant_spelling(self):
        expected = {name: index for index, name in enumerate(audit.EXPRESSIONS)}
        for prefix in ("", "_"):
            lines = []
            for name, value in expected.items():
                lines += [f".globl {prefix}mvc2_abi_{name}",
                          f"{prefix}mvc2_abi_{name}: # comment",
                          "  .zero 4" if value == 0 else f"  .long 0x{value:x} # constant"]
            self.assertEqual(audit.parse_assembly("\n".join(lines)), expected)

    def test_missing_or_duplicate_fields_are_not_assumed(self):
        with self.assertRaisesRegex(ValueError, "lacks layout constants"):
            audit.parse_assembly("mvc2_abi_word_size:\n .long 4\n")
        source = "\n".join(f"mvc2_abi_{name}:\n .long 4" for name in audit.EXPRESSIONS)
        source += "\nmvc2_abi_pointer_size:\n .long 8\n"
        with self.assertRaisesRegex(ValueError, "duplicate"):
            audit.parse_assembly(source)


class PointerInventoryTests(unittest.TestCase):
    def test_only_in_data_pointer_words_are_callback_candidates(self):
        with tempfile.TemporaryDirectory(prefix="mvc2-pointer-inventory-") as temporary:
            root = Path(temporary)
            tools = root / "ports/port/tools"
            tools.mkdir(parents=True)
            # This statement would fail if the inventory executed source to get
            # PROGRAMS rather than reading its literal AST.
            (tools / "gen_host.py").write_text(
                "raise RuntimeError('source must not execute')\n"
                "PROGRAMS = {'arcade': Program('test', 'symbols.txt', 'pointers.tsv', 'test.sub',"
                " [(0x100,0x200)], [(0x200,0x300)], 0x100, 0x400)}\n")
            (root / "pointers.tsv").write_text(
                "00000110\t00000120\n"  # Located in code, not data.
                "00000200\t00000124\n"  # A callback candidate.
                "00000204\t00000218\n"  # Data reference.
                "00000208\t00000500\n")  # Outside known ranges.
            result = audit.pointer_inventory(root)["arcade"]
            self.assertEqual(result["all_pointer_words"], 4)
            self.assertEqual(result["words_in_program_data"], 3)
            self.assertEqual(result["data_words_targeting_code_regions"], 1)
            self.assertEqual(result["data_words_targeting_data_regions"], 1)

    def test_source_evidence_keeps_exact_line_or_explicit_missing(self):
        with tempfile.TemporaryDirectory(prefix="mvc2-source-evidence-") as temporary:
            root = Path(temporary)
            (root / "source.c").write_text("/* first */\n    uint32_t value;\n")
            result = audit.source_location(root, "source.c", r"uint32_t")
            self.assertEqual(result["line"], 2)
            self.assertEqual(result["text"], "uint32_t value;")
            missing = audit.source_location(root, "source.c", r"uint64_t")
            self.assertIsNone(missing["line"])
            self.assertIn("changed", missing["note"])


@unittest.skipUnless(shutil.which("cc"), "C compiler is not installed")
class CompilerEvidenceTests(unittest.TestCase):
    def test_actual_headers_retain_serialized_vs_native_layout_evidence(self):
        report = audit.audit(["cc"], audit.ROOT, ["-m32"])
        self.assertEqual(report["status"], "arm64_gameplay_not_ready")
        self.assertIn("not an Apple ARM64 compilation", report["layout_scope"])
        for program in ("arcade", "dc"):
            corrected = report["layouts"][program]["hypothetical_fixed_words"]
            self.assertTrue(corrected["available"], corrected.get("error"))
            layout = corrected["layout"]
            self.assertEqual(layout["word_size"], 4)
            self.assertEqual(layout["switch_size"], 20)
            self.assertEqual(layout["cpu_data_size"], 12)
            if layout["pointer_size"] == 8:
                # The actual error the audit must expose: scalar typedef fixes
                # do not repair texture overlays, effect slots or copied spans.
                self.assertGreater(layout["texture_header_size"], 16)
                self.assertGreater(layout["effect_prefix_size"], layout["effect_slot_size"])
                self.assertGreater(layout["character_part_field_span"], layout["character_part_copy_size"])
            native = report["layouts"][program]["experimental_native_records"]
            self.assertTrue(native["available"], native.get("error"))
            self.assertEqual(native["layout"]["word_size"], 4)
            self.assertGreaterEqual(native["layout"]["effect_slot_size"],
                                    native["layout"]["player_work_size"])
            self.assertEqual(native["layout"]["character_part_copy_size"],
                             native["layout"]["character_part_field_span"])
            self.assertEqual(native["layout"]["animation_copy_size"],
                             native["layout"]["animation_field_span"])
            reference = report["layouts"][program]["reference_32_bit"]
            if reference["available"]:
                self.assertEqual(reference["layout"]["pointer_size"], 4)
                self.assertEqual(reference["layout"]["texture_header_size"], 16)
                self.assertEqual(reference["layout"]["effect_prefix_size"], 464)
        for blocker in report["source_blockers"]:
            self.assertIsNotNone(blocker["evidence"]["line"], blocker)
        self.assertGreater(report["pointer_inventory"]["arcade"]["words_in_program_data"], 0)
        self.assertGreater(report["pointer_inventory"]["dc"]["data_words_targeting_code_regions"], 0)

    def test_wrong_reference_target_is_explicitly_unavailable(self):
        report = audit.audit(["cc"], audit.ROOT, [])
        for program in report["layouts"].values():
            host = program["hypothetical_fixed_words"]
            reference = program["reference_32_bit"]
            if host["available"] and host["layout"]["pointer_size"] != 4:
                self.assertFalse(reference["available"])
                self.assertIn("did not select", reference["error"])

    def test_command_never_reports_gameplay_ready(self):
        command = ["python3", str(audit.ROOT / "ports/port/tools/audit_arm64.py")]
        ordinary = subprocess.run(command, capture_output=True, text=True, check=False)
        self.assertEqual(ordinary.returncode, 1)
        self.assertIn("not ready", ordinary.stdout)
        report_only = subprocess.run(command + ["--report-only"], capture_output=True, text=True, check=False)
        self.assertEqual(report_only.returncode, 0)
        self.assertIn("does not build or run", report_only.stdout)


if __name__ == "__main__":
    unittest.main()
