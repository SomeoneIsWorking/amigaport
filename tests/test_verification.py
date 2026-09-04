#!/usr/bin/env python3
"""Controlled tests for hosted device selection."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import verification  # noqa: E402


class FakeAndroidContract:
    def __init__(self, devices: tuple[str, ...]) -> None:
        self.devices = devices

    def adb_devices(self, _: str) -> tuple[str, ...]:
        return self.devices


class AndroidDeviceSelectionTests(unittest.TestCase):
    def test_selects_only_the_expected_online_emulator(self) -> None:
        contract = FakeAndroidContract(("emulator-5554",))
        self.assertEqual(
            verification.select_android_device(contract, "adb", "emulator-5554"),
            ["adb", "-s", "emulator-5554"],
        )

    def test_refuses_no_online_device(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "online devices: none"):
            verification.select_android_device(FakeAndroidContract(()), "adb", "emulator-5554")

    def test_refuses_an_ambiguous_online_device_set(self) -> None:
        contract = FakeAndroidContract(("emulator-5554", "emulator-5556"))
        with self.assertRaisesRegex(RuntimeError, "emulator-5554, emulator-5556"):
            verification.select_android_device(contract, "adb", "emulator-5554")


class NativeProfileTests(unittest.TestCase):
    def test_macos_uses_one_homebrew_llvm_toolchain(self) -> None:
        def tool(name: str) -> str:
            return str(Path("/opt/homebrew/opt/llvm@20/bin") / name)

        with (
            mock.patch.object(verification.platform, "system", return_value="Darwin"),
            mock.patch.object(verification.platform, "machine", return_value="arm64"),
            mock.patch.object(
                verification, "find_homebrew_llvm_tool", side_effect=tool
            ),
        ):
            profile = verification.native_profile()

        self.assertEqual(profile.compiler_id, "Clang")
        self.assertEqual(profile.name, "macos-arm64-clang")
        tool_roots = {
            Path(profile.c_compiler).parent,
            Path(profile.cxx_compiler).parent,
            Path(profile.formatter).parent,
            Path(profile.linter).parent,
        }
        self.assertEqual(len(tool_roots), 1)

    def test_macos_refuses_mixed_llvm_toolchains(self) -> None:
        def tool(name: str) -> str:
            version = "llvm@21" if name == "clang-tidy" else "llvm@20"
            return str(Path("/opt/homebrew/opt") / version / "bin" / name)

        with (
            mock.patch.object(verification.platform, "system", return_value="Darwin"),
            mock.patch.object(verification.platform, "machine", return_value="arm64"),
            mock.patch.object(
                verification, "find_homebrew_llvm_tool", side_effect=tool
            ),
        ):
            with self.assertRaisesRegex(RuntimeError, "do not share one toolchain"):
                verification.native_profile()


if __name__ == "__main__":
    unittest.main()
