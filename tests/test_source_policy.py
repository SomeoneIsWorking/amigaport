#!/usr/bin/env python3
"""Positive and controlled-negative tests for repository policy."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from source_policy import MAX_LINES, analyze_build_manifest, analyze_source  # noqa: E402


class SourcePolicyTests(unittest.TestCase):
    def test_accepts_clean_product_source(self) -> None:
        findings = analyze_source(Path("src/executor.cpp"), "void execute() {}\n")
        self.assertEqual(findings, [])

    def test_rejects_environment_and_process_output(self) -> None:
        findings = analyze_source(
            Path("src/executor.cpp"),
            'const char* value = getenv("BAD");\nstd::cerr << value;\n',
        )
        self.assertEqual(
            [finding.message for finding in findings],
            [
                "environment access outside config owner",
                "direct process output outside logger",
            ],
        )

    def test_rejects_oversized_source(self) -> None:
        text = "int value;\n" * (MAX_LINES + 1)
        findings = analyze_source(Path("src/monolith.cpp"), text)
        self.assertEqual(findings[0].message, f"{MAX_LINES + 1} lines exceeds {MAX_LINES}")

    def test_rejects_generic_emulator_frontend_dependency(self) -> None:
        findings = analyze_build_manifest("add_library(bad libretro/libretro-core.c)\n")
        self.assertEqual(len(findings), 1)
        self.assertIn("frontend/device", findings[0].message)


if __name__ == "__main__":
    unittest.main()
