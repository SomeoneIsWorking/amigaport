"""Controlled tests for hosted LLVM tool discovery."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import llvm_tools


class LlvmToolTests(unittest.TestCase):
    def test_apple_compiler_keeps_cpp_driver_invocation_name(self) -> None:
        compiler = Path("/Xcode/usr/bin/clang++")
        with (
            mock.patch.object(
                llvm_tools.subprocess,
                "run",
                return_value=SimpleNamespace(stdout=str(compiler) + "\n"),
            ),
            mock.patch.object(Path, "is_file", return_value=True),
            mock.patch.object(
                Path, "resolve", return_value=compiler.with_name("clang")
            ),
        ):
            selected = llvm_tools.apple_clang_tool("clang++")
        self.assertEqual(selected, str(compiler))

    def test_apple_compiler_refuses_missing_driver(self) -> None:
        with (
            mock.patch.object(
                llvm_tools.subprocess,
                "run",
                return_value=SimpleNamespace(stdout="/Xcode/usr/bin/clang++\n"),
            ),
            mock.patch.object(Path, "is_file", return_value=False),
            self.assertRaisesRegex(RuntimeError, "Xcode compiler is unavailable"),
        ):
            llvm_tools.apple_clang_tool("clang++")

    def test_cpp_headers_follow_appleclang_search_order(self) -> None:
        output = (
            "Apple clang version 17\n"
            "#include <...> search starts here:\n"
            " /Xcode/SDK/usr/include/c++/v1\n"
            " /Xcode/Toolchain/usr/lib/clang/17/include\n"
            " /Xcode/SDK/usr/include\n"
            "End of search list.\n"
        )
        with (
            mock.patch.object(
                llvm_tools.subprocess,
                "run",
                return_value=SimpleNamespace(stderr=output),
            ) as run,
            mock.patch.object(Path, "is_dir", return_value=True),
            mock.patch.object(Path, "resolve", lambda path: path),
        ):
            paths = llvm_tools.apple_cpp_include_directories(
                "/Xcode/clang++", "/Xcode/SDK"
            )
        self.assertEqual(paths, (str(Path("/Xcode/SDK/usr/include/c++/v1")),))
        self.assertEqual(
            run.call_args.args[0][:3], ["/Xcode/clang++", "-isysroot", "/Xcode/SDK"]
        )

    def test_cpp_headers_refuse_unrecognized_success(self) -> None:
        with (
            mock.patch.object(
                llvm_tools.subprocess, "run", return_value=SimpleNamespace(stderr="")
            ),
            self.assertRaisesRegex(RuntimeError, "did not report"),
        ):
            llvm_tools.apple_cpp_include_directories("clang++", "/SDK")

    def test_cpp_headers_refuse_missing_reported_directory(self) -> None:
        output = "#include <...> search starts here:\n /SDK/usr/include/c++/v1\nEnd of search list.\n"
        with (
            mock.patch.object(
                llvm_tools.subprocess,
                "run",
                return_value=SimpleNamespace(stderr=output),
            ),
            mock.patch.object(Path, "is_dir", return_value=False),
            self.assertRaisesRegex(RuntimeError, "headers are unavailable"),
        ):
            llvm_tools.apple_cpp_include_directories("clang++", "/SDK")

    def test_finds_versioned_homebrew_llvm(self) -> None:
        results = {
            "llvm@20": SimpleNamespace(
                returncode=0, stdout="/opt/homebrew/opt/llvm@20\n"
            ),
            "llvm": SimpleNamespace(returncode=1, stdout=""),
        }

        def completed(command: list[str], **_: object) -> SimpleNamespace:
            return results[command[-1]]

        with (
            mock.patch.object(llvm_tools.shutil, "which", return_value=None),
            mock.patch.object(llvm_tools.platform, "system", return_value="Darwin"),
            mock.patch.object(
                llvm_tools, "_homebrew", return_value="/opt/homebrew/bin/brew"
            ),
            mock.patch.object(llvm_tools.subprocess, "run", side_effect=completed),
            mock.patch.object(Path, "is_file", return_value=True),
        ):
            resolved = llvm_tools.find_llvm_tool("clang-format")

        self.assertEqual(
            resolved, str(Path("/opt/homebrew/opt/llvm@20/bin/clang-format"))
        )

    def test_refuses_missing_tool(self) -> None:
        with (
            mock.patch.object(llvm_tools.shutil, "which", return_value=None),
            mock.patch.object(llvm_tools.platform, "system", return_value="Linux"),
            self.assertRaisesRegex(RuntimeError, "required LLVM tool is unavailable"),
        ):
            llvm_tools.find_llvm_tool("clang-format")


if __name__ == "__main__":
    unittest.main()
