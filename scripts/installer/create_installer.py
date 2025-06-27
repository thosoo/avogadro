#!/usr/bin/env python3
"""Simple installer helper.
Copies optional xTB runtime files before running NSIS."""
import argparse
import os
import shutil
import subprocess
from pathlib import Path


def copy_xtb(dist):
    xtb_dir = os.environ.get("XTB_DIR")
    if not xtb_dir:
        return
    src = Path(xtb_dir) / "bin"
    if not src.is_dir():
        return
    dist_bin = Path(dist) / "bin"
    dist_bin.mkdir(parents=True, exist_ok=True)
    for name in ("xtb.exe", "libiomp5md.dll"):
        file = src / name
        if file.exists():
            shutil.copy(file, dist_bin / name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--with-xtb", action="store_true", help="include xTB runtime")
    args = parser.parse_args()

    dist = Path("scripts/installer/dist")
    if args.with_xtb:
        copy_xtb(dist)

    subprocess.check_call(["makensis", "scripts/installer/setup.nsi"])


if __name__ == "__main__":
    main()
