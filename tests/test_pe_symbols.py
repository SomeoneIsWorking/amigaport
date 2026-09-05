#!/usr/bin/env python3
"""Strict final-image PDB identity and public-record discriminators."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from link_audit import audit_symbols
from pe_symbols import parse_pdb_publics, parse_pe_identity, read_pe_symbols

# Minimal verbatim llvm-readobj / llvm-pdbutil records from LLVM's shipped
# llvm-symbolizer/pdb/Inputs/test.exe and test.pdb fixture (also retained by DXC).
# No binary or compiler input is synthesized by these parser tests.
PE_DUMP = """
    PDBInfo {
      PDBSignature: 0x53445352
      PDBGUID: {DD357D5B-3BB6-48A6-86FA-8ADE1F48EBBA}
      PDBAge: 3
    }
"""
PDB_DUMP = """
  Age: 3
  GUID: {DD357D5B-3BB6-48A6-86FA-8ADE1F48EBBA}
  Records
   96800 | S_PUB32 [size = 28] `__cfltcvt_l`
           flags = function, addr = 0001:74097
"""


class PeSymbolTests(unittest.TestCase):
    def test_accepts_matching_linker_records(self) -> None:
        self.assertEqual(
            parse_pdb_publics(PDB_DUMP, parse_pe_identity(PE_DUMP)),
            {"__cfltcvt_l"},
        )

    def test_rejects_foreign_pdb_or_stale_incremental_age(self) -> None:
        for text in (
            PDB_DUMP.replace("DD357D5B", "AD357D5B"),
            PDB_DUMP.replace("Age: 3", "Age: 4"),
        ):
            with (
                self.subTest(text=text),
                self.assertRaisesRegex(RuntimeError, "does not match final PE"),
            ):
                parse_pdb_publics(text, parse_pe_identity(PE_DUMP))

    def test_refuses_absent_duplicate_or_invalid_pe_identity(self) -> None:
        for text in (
            "",
            PE_DUMP + PE_DUMP,
            PE_DUMP.replace("0x53445352", "0x3031424E"),
            PE_DUMP.replace("DD357D5B", "ZZ357D5B"),
            PE_DUMP.replace("PDBAge: 3", "PDBAge: 0"),
        ):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                parse_pe_identity(text)

    def test_refuses_empty_unknown_truncated_or_unmapped_records(self) -> None:
        for text in (
            PDB_DUMP.split("  Records")[0],
            PDB_DUMP.replace("S_PUB32", "S_UNKNOWN"),
            PDB_DUMP.replace(" | ", " : "),
            PDB_DUMP.split("           flags")[0],
            PDB_DUMP.replace("0001:74097", "0000:74097"),
        ):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                parse_pdb_publics(text, parse_pe_identity(PE_DUMP))

    def test_policy_sees_injected_handler_and_forbidden_owner(self) -> None:
        # Controlled mutations prove policy receives the parsed public names.
        for symbol, handler, forbidden in (
            ("op_7000_12_ff", ("op_7000_12_ff",), ()),
            ("retro_run", (), ("retro_run",)),
        ):
            result = audit_symbols(
                parse_pdb_publics(
                    PDB_DUMP.replace("__cfltcvt_l", symbol),
                    parse_pe_identity(PE_DUMP),
                )
            )
            self.assertEqual(result.cpu_handlers, handler)
            self.assertEqual(result.forbidden, forbidden)

    def test_reads_only_final_exe_and_sibling_pdb(self) -> None:
        binary = Path("build/runtime.exe")
        with (
            patch("pe_symbols.Path.is_file", return_value=True),
            patch("pe_symbols.tool_output", side_effect=[PE_DUMP, PDB_DUMP]) as tool,
        ):
            self.assertEqual(read_pe_symbols(binary), {"__cfltcvt_l"})
        self.assertEqual(
            [call.args for call in tool.call_args_list],
            [
                ("llvm-readobj", "--coff-debug-directory", str(binary)),
                (
                    "llvm-pdbutil",
                    "dump",
                    "--summary",
                    "--publics",
                    str(binary.with_suffix(".pdb")),
                ),
            ],
        )

    def test_refuses_missing_pdb_without_trying_archive_tools(self) -> None:
        with (
            patch("pe_symbols.Path.is_file", return_value=False),
            patch("pe_symbols.tool_output") as tool,
            self.assertRaisesRegex(RuntimeError, "requires its linker PDB"),
        ):
            read_pe_symbols(Path("build/runtime.exe"))
        tool.assert_not_called()


if __name__ == "__main__":
    unittest.main()
