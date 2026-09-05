"""Controlled tests for hosted device selection."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import verification


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
            verification.select_android_device(
                FakeAndroidContract(()), "adb", "emulator-5554"
            )

    def test_refuses_an_ambiguous_online_device_set(self) -> None:
        contract = FakeAndroidContract(("emulator-5554", "emulator-5556"))
        with self.assertRaisesRegex(RuntimeError, "emulator-5554, emulator-5556"):
            verification.select_android_device(contract, "adb", "emulator-5554")


class NativeProfileTests(unittest.TestCase):
    def test_macos_builds_with_appleclang_and_uses_its_cpp_headers(self) -> None:
        def tool(name: str) -> str:
            return str(Path("/opt/homebrew/opt/llvm@20/bin") / name)

        with (
            mock.patch.object(verification.platform, "system", return_value="Darwin"),
            mock.patch.object(verification.platform, "machine", return_value="arm64"),
            mock.patch.object(
                verification, "find_homebrew_llvm_tool", side_effect=tool
            ),
            mock.patch.object(
                verification,
                "apple_clang_tool",
                side_effect=lambda name: f"/Xcode/usr/bin/{name}",
            ),
            mock.patch.object(
                verification,
                "apple_cpp_include_directories",
                return_value=("/SDKs/MacOSX.sdk/usr/include/c++/v1",),
            ),
            mock.patch.object(
                verification,
                "discover_directory",
                side_effect=(
                    "/SDKs/MacOSX.sdk",
                    "/opt/homebrew/opt/llvm@20/lib/clang/20",
                ),
            ),
        ):
            profile = verification.native_profile()

        self.assertEqual(profile.compiler_id, "AppleClang")
        self.assertEqual(profile.name, "macos-arm64-appleclang")
        self.assertEqual(profile.cxx_compiler, "/Xcode/usr/bin/clang++")
        tool_roots = {
            Path(profile.formatter).parent,
            Path(profile.linter).parent,
        }
        self.assertEqual(len(tool_roots), 1)
        self.assertEqual(profile.sdk_root, "/SDKs/MacOSX.sdk")
        self.assertEqual(profile.resource_dir, "/opt/homebrew/opt/llvm@20/lib/clang/20")
        self.assertEqual(
            profile.cpp_include_dirs, ("/SDKs/MacOSX.sdk/usr/include/c++/v1",)
        )

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
            self.assertRaisesRegex(RuntimeError, "do not share one toolchain"),
        ):
            verification.native_profile()

    def test_directory_discovery_refuses_missing_toolchain_surface(self) -> None:
        completed = mock.Mock(returncode=1, stdout="", stderr="SDK unavailable")
        with (
            mock.patch.object(verification.subprocess, "run", return_value=completed),
            self.assertRaisesRegex(RuntimeError, "SDK unavailable"),
        ):
            verification.discover_directory(["xcrun"], "macOS SDK")

    def test_directory_discovery_refuses_empty_success(self) -> None:
        completed = mock.Mock(returncode=0, stdout="", stderr="")
        with (
            mock.patch.object(verification.subprocess, "run", return_value=completed),
            self.assertRaisesRegex(RuntimeError, "no output"),
        ):
            verification.discover_directory(["clang++"], "Clang resource directory")

    def test_native_verifier_forwards_sdk_and_resource_contract(self) -> None:
        profile = verification.NativeProfile(
            "macos-arm64-clang",
            "clang",
            "clang++",
            "Clang",
            formatter="clang-format",
            linter="clang-tidy",
            sdk_root="/SDKs/MacOSX.sdk",
            resource_dir="/llvm/lib/clang/20",
            cpp_include_dirs=("/SDKs/MacOSX.sdk/usr/include/c++/v1",),
        )
        with (
            mock.patch.object(verification, "native_profile", return_value=profile),
            mock.patch.object(verification, "common_checks"),
            mock.patch.object(verification, "configure") as configure,
            mock.patch.object(verification, "assert_compiler"),
            mock.patch.object(
                verification,
                "build_and_audit",
                return_value=Path("build/amigaport_tests"),
            ),
            mock.patch.object(verification, "run"),
            mock.patch.object(verification, "lint") as lint,
        ):
            verification.verify_native()

        configure.assert_called_once_with(
            verification.ROOT / "build" / "verify-macos-arm64-clang",
            [
                "-DCMAKE_C_COMPILER=clang",
                "-DCMAKE_CXX_COMPILER=clang++",
                "-DCMAKE_OSX_SYSROOT=/SDKs/MacOSX.sdk",
            ],
        )
        lint.assert_called_once_with(
            verification.ROOT / "build" / "verify-macos-arm64-clang",
            "clang-tidy",
            (
                "--extra-arg-before=-resource-dir=/llvm/lib/clang/20",
                "--extra-arg-before=-isysroot/SDKs/MacOSX.sdk",
                "--extra-arg-before=-nostdinc++",
                "--extra-arg-before=-isystem/SDKs/MacOSX.sdk/usr/include/c++/v1",
            ),
        )


if __name__ == "__main__":
    unittest.main()
