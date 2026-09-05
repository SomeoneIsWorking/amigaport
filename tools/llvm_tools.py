"""Resolve the LLVM command-line tools used by portable verification."""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
from pathlib import Path


def apple_clang_tool(name: str) -> str:
    """Resolve the active Xcode compiler without accepting a PATH-selected substitute."""
    completed = subprocess.run(
        ["xcrun", "--sdk", "macosx", "--find", name],
        check=True,
        capture_output=True,
        text=True,
    )
    path = Path(completed.stdout.strip())
    if not completed.stdout.strip() or not path.is_file():
        raise RuntimeError(f"Xcode compiler is unavailable: {name}")
    # The invocation name selects the driver: clang++ may be a symlink to clang.
    # Following that symlink drops automatic linkage of the C++ runtime.
    return str(path)


def apple_cpp_include_directories(compiler: str, sdk_root: str) -> tuple[str, ...]:
    """Make clang-tidy consume the C++ headers selected by the product compiler."""
    completed = subprocess.run(
        [compiler, "-isysroot", sdk_root, "-E", "-x", "c++", "-", "-v"],
        input="",
        check=True,
        capture_output=True,
        text=True,
    )
    collecting = False
    directories: list[str] = []
    for line in completed.stderr.splitlines():
        if line == "#include <...> search starts here:":
            collecting = True
        elif line == "End of search list.":
            collecting = False
        elif collecting:
            candidate = Path(line.strip())
            if candidate.parts[-3:] == ("include", "c++", "v1"):
                if not candidate.is_dir():
                    raise RuntimeError(
                        f"AppleClang C++ headers are unavailable: {candidate}"
                    )
                directories.append(str(candidate.resolve()))
    if not directories:
        raise RuntimeError("AppleClang did not report its libc++ include directory")
    return tuple(dict.fromkeys(directories))


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
    if platform.system() == "Windows" and (
        program_files := os.environ.get("ProgramFiles")
    ):
        candidate = Path(program_files) / "LLVM" / "bin" / f"{name}.exe"
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError(f"required LLVM tool is unavailable: {name}")
