#!/usr/bin/env python3
"""Controlled tests for hosted device selection."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from verification import select_android_device  # noqa: E402


class FakeAndroidContract:
    def __init__(self, devices: tuple[str, ...]) -> None:
        self.devices = devices

    def adb_devices(self, _: str) -> tuple[str, ...]:
        return self.devices


class AndroidDeviceSelectionTests(unittest.TestCase):
    def test_selects_only_the_expected_online_emulator(self) -> None:
        contract = FakeAndroidContract(("emulator-5554",))
        self.assertEqual(
            select_android_device(contract, "adb", "emulator-5554"),
            ["adb", "-s", "emulator-5554"],
        )

    def test_refuses_no_online_device(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "online devices: none"):
            select_android_device(FakeAndroidContract(()), "adb", "emulator-5554")

    def test_refuses_an_ambiguous_online_device_set(self) -> None:
        contract = FakeAndroidContract(("emulator-5554", "emulator-5556"))
        with self.assertRaisesRegex(RuntimeError, "emulator-5554, emulator-5556"):
            select_android_device(contract, "adb", "emulator-5554")


if __name__ == "__main__":
    unittest.main()
