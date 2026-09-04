#!/usr/bin/env python3
"""Canonical local and hosted verifier for amigaport."""

from __future__ import annotations

import argparse
from pathlib import Path

from verification import verify_android, verify_native


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", choices=("native", "android"), default="native")
    parser.add_argument("--android-port-dir", type=Path)
    arguments = parser.parse_args()
    if arguments.target == "android":
        verify_android(arguments.android_port_dir)
    else:
        verify_native()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
