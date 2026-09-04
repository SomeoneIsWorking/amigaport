#!/usr/bin/env python3
"""Positive and controlled-negative tests for repository policy."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from source_policy import (  # noqa: E402
    MAX_LINES,
    analyze_build_manifest,
    analyze_source,
    analyze_workflow,
)


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

    def test_accepts_pinned_complete_workflow_shape(self) -> None:
        commit = "a" * 40
        workflow = f"""permissions:
  contents: read
jobs:
  desktop:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@{commit}
      - run: python tools/verify.py
  windows:
    runs-on: windows-2025
  macos:
    runs-on: macos-26
  android:
    steps:
      - run: python tools/verify.py --target android
submodules: recursive
fetch-depth: 0
fetch-depth: 0
persist-credentials: false
persist-credentials: false
"""
        self.assertEqual(analyze_workflow(workflow), [])

    def test_rejects_mutable_action_and_incomplete_matrix(self) -> None:
        findings = analyze_workflow("- uses: actions/checkout@v4\ncontinue-on-error: true\n")
        messages = [finding.message for finding in findings]
        self.assertIn("action is not pinned to a full commit: v4", messages)
        self.assertIn("required hosted runner is missing: macos-26", messages)
        self.assertIn("CI may not hide a failed platform job", messages)


if __name__ == "__main__":
    unittest.main()
