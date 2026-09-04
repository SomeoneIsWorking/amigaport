"""Pure scanning rules shared by the policy CLI and its self-tests."""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "include", ROOT / "src", ROOT / "tests", ROOT / "tools")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".py"}
MAX_LINES = 1_200
CONFIG_OWNERS = {Path("src/config.cpp")}
LOGGER_OWNERS = {Path("src/logging.cpp")}
FORBIDDEN_DEPENDENCY_SOURCES = ("libretro-core.c", "libretro-glue.c", "sources/src/memory.c")
REQUIRED_CI_RUNNERS = (
    "ubuntu-24.04",
    "windows-2025",
    "macos-26",
    "macos-15-intel",
)
ACTION_USE = re.compile(r"^\s*-?\s*uses:\s*[^\s@]+@([^\s]+)\s*$", re.MULTILINE)
FULL_COMMIT = re.compile(r"[0-9a-f]{40}")


@dataclass(frozen=True)
class Finding:
    path: Path
    line: int
    message: str


def iter_sources(root: Path = ROOT) -> list[Path]:
    source_roots = tuple(root / path.relative_to(ROOT) for path in SOURCE_ROOTS)
    return sorted(
        path
        for source_root in source_roots
        for path in source_root.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
    )


def analyze_source(relative_path: Path, text: str) -> list[Finding]:
    lines = text.splitlines()
    findings: list[Finding] = []
    if len(lines) > MAX_LINES:
        findings.append(Finding(relative_path, 1, f"{len(lines)} lines exceeds {MAX_LINES}"))

    if relative_path == Path("tools/source_policy.py") or relative_path.parts[0] == "tests":
        return findings

    for number, line in enumerate(lines, start=1):
        if relative_path not in CONFIG_OWNERS and re.search(
            r"\b(?:getenv|putenv|setenv)\s*\(", line
        ):
            findings.append(
                Finding(relative_path, number, "environment access outside config owner")
            )
        if relative_path not in LOGGER_OWNERS and re.search(
            r"\b(?:fprintf|printf)\s*\(\s*stderr|std::(?:cerr|clog)", line
        ):
            findings.append(
                Finding(relative_path, number, "direct process output outside logger")
            )
        if re.search(
            r"(?:generated guest|offline translator|static dispatcher)", line, re.I
        ):
            findings.append(Finding(relative_path, number, "retired static execution vocabulary"))
    return findings


def analyze_build_manifest(text: str) -> list[Finding]:
    findings: list[Finding] = []
    for source in FORBIDDEN_DEPENDENCY_SOURCES:
        for number, line in enumerate(text.splitlines(), start=1):
            if source in line:
                findings.append(
                    Finding(
                        Path("CMakeLists.txt"),
                        number,
                        f"generic PUAE frontend/device source is not a runtime dependency: {source}",
                    )
                )
    return findings


def analyze_workflow(text: str) -> list[Finding]:
    findings: list[Finding] = []
    path = Path(".github/workflows/ci.yml")
    for reference in ACTION_USE.findall(text):
        if not FULL_COMMIT.fullmatch(reference):
            findings.append(Finding(path, 1, f"action is not pinned to a full commit: {reference}"))
    for runner in REQUIRED_CI_RUNNERS:
        if runner not in text:
            findings.append(Finding(path, 1, f"required hosted runner is missing: {runner}"))
    if text.count("tools/verify.py") < 2:
        findings.append(Finding(path, 1, "desktop and Android jobs must call the canonical verifier"))
    if "submodules: recursive" not in text:
        findings.append(Finding(path, 1, "CI must initialize the maintained CPU submodule"))
    checkout_count = text.count("uses: actions/checkout@")
    if text.count("fetch-depth: 0") < checkout_count:
        findings.append(Finding(path, 1, "each repository checkout must retain full history"))
    if text.count("persist-credentials: false") < checkout_count:
        findings.append(Finding(path, 1, "each repository checkout must discard credentials"))
    if "permissions:\n  contents: read" not in text:
        findings.append(Finding(path, 1, "CI permissions must be read-only"))
    if "continue-on-error" in text:
        findings.append(Finding(path, 1, "CI may not hide a failed platform job"))
    if "--android-serial emulator-5554" not in text:
        findings.append(Finding(path, 1, "Android runtime must select the hosted emulator by serial"))
    return findings


def scan(root: Path = ROOT) -> list[Finding]:
    findings: list[Finding] = []
    for path in iter_sources(root):
        relative = path.relative_to(root)
        findings.extend(analyze_source(relative, path.read_text(encoding="utf-8")))

    for shell_path in sorted(root.rglob("*.sh")):
        if shell_path == root / "run.sh" or "third_party" in shell_path.parts:
            continue
        findings.append(
            Finding(shell_path.relative_to(root), 1, "non-launcher shell automation is forbidden")
        )
    findings.extend(analyze_build_manifest((root / "CMakeLists.txt").read_text(encoding="utf-8")))
    workflow = root / ".github" / "workflows" / "ci.yml"
    if not workflow.is_file():
        findings.append(Finding(workflow.relative_to(root), 1, "hosted runtime CI is missing"))
    else:
        findings.extend(analyze_workflow(workflow.read_text(encoding="utf-8")))
    return findings
