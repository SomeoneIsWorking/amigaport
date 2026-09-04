#!/usr/bin/env python3
"""Focused local verifier for amigaport."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from source_policy import ROOT, iter_sources


BUILD_DIR = ROOT / "build" / "verify"


def run(*command: str) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    run(sys.executable, str(ROOT / "tools" / "policy.py"))
    run(sys.executable, str(ROOT / "tests" / "test_source_policy.py"))
    run(sys.executable, str(ROOT / "tests" / "test_link_audit.py"))
    native_sources = [
        str(path.relative_to(ROOT))
        for path in iter_sources()
        if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"}
    ]
    run("clang-format", "--dry-run", "--Werror", *native_sources)
    run(
        "cmake",
        "-S",
        str(ROOT),
        "-B",
        str(BUILD_DIR),
        "-G",
        "Ninja",
        "-DCMAKE_C_COMPILER=clang",
        "-DCMAKE_CXX_COMPILER=clang++",
    )
    run("cmake", "--build", str(BUILD_DIR))
    run("ctest", "--test-dir", str(BUILD_DIR), "--output-on-failure")
    run(
        sys.executable,
        str(ROOT / "tools" / "link_audit.py"),
        str(BUILD_DIR / "amigaport_tests"),
    )
    translation_units = [
        path
        for path in native_sources
        if Path(path).suffix in {".c", ".cc", ".cpp"}
    ]
    run("clang-tidy", "-p", str(BUILD_DIR), *translation_units)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
