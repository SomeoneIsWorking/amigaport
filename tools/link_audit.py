#!/usr/bin/env python3
"""Audit a linked runtime for maintained CPU evidence and forbidden frontend symbols."""

from __future__ import annotations

import argparse
import os
import platform
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


CPU_HANDLER = re.compile(r"^op_[0-9a-f]{4}_12_ff$")
EXPECTED_CPU_HANDLERS = 1_540
FORBIDDEN_SYMBOLS = {"retro_init", "retro_load_game", "retro_run", "memory_init"}


@dataclass(frozen=True)
class AuditResult:
    symbol_count: int
    cpu_handlers: tuple[str, ...]
    forbidden: tuple[str, ...]


def audit_symbols(symbols: set[str]) -> AuditResult:
    handlers = tuple(sorted(symbol for symbol in symbols if CPU_HANDLER.fullmatch(symbol)))
    forbidden = tuple(sorted(symbols & FORBIDDEN_SYMBOLS))
    return AuditResult(len(symbols), handlers, forbidden)


def normalize_symbol(symbol: str) -> str:
    """Remove the Mach-O C symbol prefix only for names owned by this audit."""
    if symbol.startswith("_") and (
        CPU_HANDLER.fullmatch(symbol[1:]) or symbol[1:] in FORBIDDEN_SYMBOLS
    ):
        return symbol[1:]
    return symbol


def find_symbol_tool() -> str:
    llvm_nm = shutil.which("llvm-nm")
    if llvm_nm:
        return llvm_nm
    if platform.system() == "Darwin" and (brew := shutil.which("brew")):
        completed = subprocess.run(
            [brew, "--prefix", "llvm"], check=True, capture_output=True, text=True
        )
        candidate = Path(completed.stdout.strip()) / "bin" / "llvm-nm"
        if candidate.is_file():
            return str(candidate)
    if platform.system() == "Windows" and (program_files := os.environ.get("ProgramFiles")):
        candidate = Path(program_files) / "LLVM" / "bin" / "llvm-nm.exe"
        if candidate.is_file():
            return str(candidate)
    nm = shutil.which("nm")
    if nm:
        return nm
    raise RuntimeError("link audit requires llvm-nm or nm")


def read_symbols(binary: Path) -> set[str]:
    nm = find_symbol_tool()
    defined_only = (
        "-U"
        if platform.system() == "Darwin" and Path(nm).name == "nm"
        else "--defined-only"
    )
    completed = subprocess.run(
        [nm, defined_only, str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    return {
        normalize_symbol(fields[-1])
        for line in completed.stdout.splitlines()
        if len(fields := line.split()) >= 2
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    arguments = parser.parse_args()
    if not arguments.binary.is_file():
        parser.error(f"linked binary does not exist: {arguments.binary}")

    result = audit_symbols(read_symbols(arguments.binary))
    print(
        f"link-audit: scanned {result.symbol_count} defined symbols; "
        f"PUAE CPU handlers={len(result.cpu_handlers)}; forbidden frontend/device symbols="
        f"{len(result.forbidden)}"
    )
    if len(result.cpu_handlers) != EXPECTED_CPU_HANDLERS:
        print(
            "link-audit: incomplete maintained PUAE 68000 table: "
            f"expected {EXPECTED_CPU_HANDLERS}, found {len(result.cpu_handlers)}"
        )
        return 1
    if result.forbidden:
        print("link-audit: forbidden symbols: " + ", ".join(result.forbidden))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
