#!/usr/bin/env python3
"""Audit a linked runtime for maintained CPU evidence and forbidden frontend symbols."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


CPU_HANDLER = re.compile(r"^op_[0-9a-f]{4}_0_ff$")
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


def read_symbols(binary: Path) -> set[str]:
    nm = shutil.which("llvm-nm") or shutil.which("nm")
    if nm is None:
        raise RuntimeError("link audit requires llvm-nm or nm")
    completed = subprocess.run(
        [nm, "--defined-only", str(binary)],
        check=True,
        capture_output=True,
        text=True,
    )
    return {
        fields[-1]
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
    if not result.cpu_handlers:
        print("link-audit: no maintained PUAE CPU handler reached the linked artifact")
        return 1
    if result.forbidden:
        print("link-audit: forbidden symbols: " + ", ".join(result.forbidden))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
