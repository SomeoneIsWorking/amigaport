#!/usr/bin/env python3
"""Controlled positive and negative cases for linked-runtime auditing."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import mock_open, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from link_audit import (
    EXPECTED_CPU_HANDLERS,
    audit_symbols,
    normalize_symbol,
    read_symbols,
)


class LinkAuditTests(unittest.TestCase):
    def test_accepts_reached_cpu_handler_without_frontend(self) -> None:
        result = audit_symbols({"main", "op_7000_12_ff"})
        self.assertEqual(result.cpu_handlers, ("op_7000_12_ff",))
        self.assertEqual(result.forbidden, ())

    def test_declares_complete_expected_handler_count(self) -> None:
        self.assertEqual(EXPECTED_CPU_HANDLERS, 1_540)

    def test_normalizes_macho_c_symbols_without_touching_cxx_names(self) -> None:
        self.assertEqual(normalize_symbol("_op_7000_12_ff"), "op_7000_12_ff")
        self.assertEqual(normalize_symbol("_retro_run"), "retro_run")
        self.assertEqual(normalize_symbol("__Z3foov"), "__Z3foov")

    def test_reports_frontend_and_empty_cpu_evidence(self) -> None:
        result = audit_symbols({"main", "retro_run"})
        self.assertEqual(result.cpu_handlers, ())
        self.assertEqual(result.forbidden, ("retro_run",))

    def test_pe_dispatch_reads_linker_pdb_not_empty_coff_table(self) -> None:
        binary = Path("build/runtime.exe")
        with (
            patch("link_audit.Path.open", mock_open(read_data=b"MZ")),
            patch("link_audit.read_pe_symbols", return_value={"_op_7000_12_ff"}) as pe,
            patch("link_audit.find_symbol_tool") as nm,
        ):
            self.assertEqual(read_symbols(binary), {"op_7000_12_ff"})
        pe.assert_called_once_with(binary)
        nm.assert_not_called()


if __name__ == "__main__":
    unittest.main()
