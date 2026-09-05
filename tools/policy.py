#!/usr/bin/env python3
"""Command-line repository policy for first-party amigaport sources."""

from __future__ import annotations

from source_policy import iter_sources, scan


def main() -> int:
    sources = iter_sources()
    findings = scan()
    for finding in findings:
        print(f"{finding.path}:{finding.line}: {finding.message}")
    print(
        f"policy: scanned {len(sources)} first-party source files; findings={len(findings)}"
    )
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
