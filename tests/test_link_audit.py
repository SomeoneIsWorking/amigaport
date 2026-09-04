#!/usr/bin/env python3
"""Controlled positive and negative cases for linked-runtime auditing."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from link_audit import audit_symbols  # noqa: E402


class LinkAuditTests(unittest.TestCase):
    def test_accepts_reached_cpu_handler_without_frontend(self) -> None:
        result = audit_symbols({"main", "op_7000_0_ff"})
        self.assertEqual(result.cpu_handlers, ("op_7000_0_ff",))
        self.assertEqual(result.forbidden, ())

    def test_reports_frontend_and_empty_cpu_evidence(self) -> None:
        result = audit_symbols({"main", "retro_run"})
        self.assertEqual(result.cpu_handlers, ())
        self.assertEqual(result.forbidden, ("retro_run",))


if __name__ == "__main__":
    unittest.main()
