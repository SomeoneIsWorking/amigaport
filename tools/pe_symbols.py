"""Read final PE definitions from its identity-matched linker PDB.

PE images normally omit the COFF symbol table. PDB public records describe
defined symbols in the linked image, not the unselected archive members.
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from uuid import UUID

from llvm_tools import find_llvm_tool


@dataclass(frozen=True)
class PdbIdentity:
    guid: UUID
    age: int


def single_field(text: str, name: str) -> str:
    matches = re.findall(rf"^\s*{name}:\s*(.*?)\s*$", text, re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError(f"expected one {name} field, found {len(matches)}")
    return matches[0]


def parse_identity(text: str, prefix: str = "") -> PdbIdentity:
    try:
        identity = PdbIdentity(
            UUID(single_field(text, prefix + "GUID")),
            int(single_field(text, prefix + "Age")),
        )
    except ValueError as error:
        raise RuntimeError(f"invalid PDB identity: {error}") from error
    if identity.age < 1:
        raise RuntimeError(f"invalid PDB age: {identity.age}")
    return identity


def parse_pe_identity(output: str) -> PdbIdentity:
    if single_field(output, "PDBSignature") != "0x53445352":
        raise RuntimeError("final PE requires an RSDS CodeView PDB identity")
    return parse_identity(output, "PDB")


def parse_pdb_publics(output: str, expected: PdbIdentity) -> set[str]:
    identity = parse_identity(output)
    if identity != expected:
        raise RuntimeError(
            f"PDB does not match final PE: expected {expected}, found {identity}"
        )
    # Every public record must name an actual image section/address. Refuse
    # changed or truncated dump output instead of silently reducing the corpus.
    lines = output.splitlines()
    headers = [index for index, line in enumerate(lines) if line.strip() == "Records"]
    if len(headers) != 1:
        raise RuntimeError(
            f"expected one PDB public Records header, found {len(headers)}"
        )
    symbols: set[str] = set()
    pending: str | None = None
    for line in lines[headers[0] + 1 :]:
        if not line.strip():
            continue
        if pending is not None:
            address = re.fullmatch(r"\s*flags = .+, addr = ([0-9]+):([0-9]+)\s*", line)
            if address is None or int(address[1]) == 0:
                raise RuntimeError(f"PDB public has no final image address: {pending}")
            symbols.add(pending)
            pending = None
        else:
            record = re.fullmatch(
                r"\s*[0-9]+ \| S_PUB32 \[size = [0-9]+\] `(.+)`\s*", line
            )
            if record is None:
                raise RuntimeError(f"unrecognized PDB public record: {line.strip()}")
            pending = record[1]
    if pending is not None:
        raise RuntimeError(f"truncated PDB public record: {pending}")
    if not symbols:
        raise RuntimeError(
            "scanned PDB public records: 0; cannot see linked definitions"
        )
    return symbols


def tool_output(name: str, *arguments: str) -> str:
    return subprocess.run(
        [find_llvm_tool(name), *arguments],
        check=True,
        capture_output=True,
        text=True,
    ).stdout


def read_pe_symbols(binary: Path) -> set[str]:
    # CMake's linker-PDB contract places it beside the final executable. Never
    # follow an embedded absolute build-machine path or search for another PDB.
    pdb = binary.with_suffix(".pdb")
    if not pdb.is_file():
        raise RuntimeError(f"final PE symbol audit requires its linker PDB: {pdb}")
    identity = parse_pe_identity(
        tool_output("llvm-readobj", "--coff-debug-directory", str(binary))
    )
    return parse_pdb_publics(
        tool_output("llvm-pdbutil", "dump", "--summary", "--publics", str(pdb)),
        identity,
    )
