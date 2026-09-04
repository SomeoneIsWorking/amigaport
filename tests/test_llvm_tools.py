#!/usr/bin/env python3
"""Controlled tests for hosted LLVM tool discovery."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import llvm_tools  # noqa: E402


class LlvmToolTests(unittest.TestCase):
    def test_finds_versioned_homebrew_llvm(self) -> None:
        results = {
            "llvm@20": SimpleNamespace(returncode=0, stdout="/opt/homebrew/opt/llvm@20\n"),
            "llvm": SimpleNamespace(returncode=1, stdout=""),
        }

        def completed(command: list[str], **_: object) -> SimpleNamespace:
            return results[command[-1]]

        with (
            mock.patch.object(llvm_tools.shutil, "which", return_value=None),
            mock.patch.object(llvm_tools.platform, "system", return_value="Darwin"),
            mock.patch.object(llvm_tools, "_homebrew", return_value="/opt/homebrew/bin/brew"),
            mock.patch.object(llvm_tools.subprocess, "run", side_effect=completed),
            mock.patch.object(Path, "is_file", return_value=True),
        ):
            resolved = llvm_tools.find_llvm_tool("clang-format")

        self.assertEqual(resolved, str(Path("/opt/homebrew/opt/llvm@20/bin/clang-format")))

    def test_refuses_missing_tool(self) -> None:
        with (
            mock.patch.object(llvm_tools.shutil, "which", return_value=None),
            mock.patch.object(llvm_tools.platform, "system", return_value="Linux"),
        ):
            with self.assertRaisesRegex(RuntimeError, "required LLVM tool is unavailable"):
                llvm_tools.find_llvm_tool("clang-format")


if __name__ == "__main__":
    unittest.main()
