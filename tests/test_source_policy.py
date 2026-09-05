#!/usr/bin/env python3
"""Positive and controlled-negative tests for repository policy."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from source_policy import (
    MAX_LINES,
    analyze_build_manifest,
    analyze_source,
    analyze_workflow,
    iter_first_party_shell_files,
)


class SourcePolicyTests(unittest.TestCase):
    def test_shell_inventory_includes_owned_automation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            tools = root / "scripts"
            tools.mkdir()
            script = tools / "bad.sh"
            script.touch()

            self.assertEqual(iter_first_party_shell_files(root), [script])

    def test_shell_inventory_excludes_build_dependencies(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            dependency = root / "build" / "deps" / "android-port"
            dependency.mkdir(parents=True)
            (dependency / "run.sh").touch()

            self.assertEqual(iter_first_party_shell_files(root), [])

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
        self.assertEqual(
            findings[0].message, f"{MAX_LINES + 1} lines exceeds {MAX_LINES}"
        )

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
      - run: sudo chmod 0666 /dev/kvm
      - run: python tools/verify.py --target android --android-serial emulator-5554
submodules: recursive
fetch-depth: 0
fetch-depth: 0
persist-credentials: false
persist-credentials: false
"""
        self.assertEqual(analyze_workflow(workflow), [])

    def test_rejects_mutable_action_and_incomplete_matrix(self) -> None:
        findings = analyze_workflow(
            "- uses: actions/checkout@v4\ncontinue-on-error: true\n"
        )
        messages = [finding.message for finding in findings]
        self.assertIn("action is not pinned to a full commit: v4", messages)
        self.assertIn("required hosted runner is missing: macos-26", messages)
        self.assertIn(
            "Android runtime must select the hosted emulator by serial", messages
        )
        self.assertIn("Android emulator job must enable KVM device access", messages)
        self.assertIn("CI may not hide a failed platform job", messages)


if __name__ == "__main__":
    unittest.main()
