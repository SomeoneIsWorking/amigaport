"""Keep checked-out dependencies and their executable pin contracts atomic."""

from __future__ import annotations

import re
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from verification import ANDROID_PORT_REVISION


class DependencyPinTests(unittest.TestCase):
    def test_puae_submodule_matches_cmake_contract(self) -> None:
        manifest = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        match = re.search(r'set\(AMIGAPORT_PUAE_REVISION "([0-9a-f]{40})"\)', manifest)
        self.assertIsNotNone(match, "CMake PUAE revision is not an exact commit")
        head = subprocess.run(
            ["git", "-C", str(ROOT / "third_party/libretro-uae"), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        self.assertEqual(match.group(1), head)

    def test_android_checkout_matches_verifier_contract(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        self.assertIn(f"ref: {ANDROID_PORT_REVISION}", workflow)


if __name__ == "__main__":
    unittest.main()
