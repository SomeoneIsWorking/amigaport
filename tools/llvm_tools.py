"""Resolve the LLVM command-line tools used by portable verification."""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
from pathlib import Path


def _homebrew() -> str | None:
    discovered = shutil.which("brew")
    if discovered:
        return discovered
    for candidate in (Path("/opt/homebrew/bin/brew"), Path("/usr/local/bin/brew")):
        if candidate.is_file():
            return str(candidate)
    return None


def find_homebrew_llvm_tool(name: str) -> str | None:
    """Find a tool in the maintained Homebrew LLVM installation."""
    brew = _homebrew()
    if brew is None:
        return None
    for formula in ("llvm@20", "llvm"):
        completed = subprocess.run(
            [brew, "--prefix", formula],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            continue
        candidate = Path(completed.stdout.strip()) / "bin" / name
        if candidate.is_file():
            return str(candidate)
    return None


def find_llvm_tool(name: str) -> str:
    """Find a named LLVM tool without changing the selected compiler."""
    if located := shutil.which(name):
        return located
    if platform.system() == "Darwin" and (located := find_homebrew_llvm_tool(name)):
        return located
    if platform.system() == "Windows" and (program_files := os.environ.get("ProgramFiles")):
        candidate = Path(program_files) / "LLVM" / "bin" / f"{name}.exe"
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError(f"required LLVM tool is unavailable: {name}")
