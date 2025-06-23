#!/usr/bin/env python3
import os
import shutil
import subprocess
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent
    dist = root / "dist"
    build_dir = Path(os.environ.get("BUILD_DIR", root.parent.parent / "build"))

    license_src = root.parent.parent / "COPYING"
    if dist.exists():
        shutil.rmtree(dist)
    dist.mkdir(parents=True)
    if license_src.exists():
        shutil.copy(license_src, dist / "gpl.txt")

    subprocess.check_call(["cmake", "--install", str(build_dir), "--prefix", str(dist)])

    exe = dist / "bin" / "Avogadro.exe"
    if not exe.exists():
        exe = dist / "bin" / "avogadro.exe"
    subprocess.check_call(["windeployqt", "--release", "--dir", str(dist / "bin"), str(exe)])

    ob_dir = os.environ.get("OPENBABEL_INSTALL_DIR")
    if ob_dir:
        ob = Path(ob_dir)
        for dll in ob.glob("bin/*.dll"):
            shutil.copy(dll, dist / "bin")
        plugins = ob / "lib" / "openbabel"
        if plugins.exists():
            for lib in plugins.glob("*"):
                shutil.copy(lib, dist / "bin")
        share = ob / "share" / "openbabel"
        if share.exists():
            dest = dist / "share" / "openbabel"
            shutil.copytree(share, dest, dirs_exist_ok=True)

    libxml = os.environ.get("LIBXML2_LIBRARY")
    if libxml:
        dll = Path(libxml).with_suffix('.dll')
        if dll.exists():
            shutil.copy(dll, dist / "bin")

    subprocess.check_call(["makensis", str(root / "setup.nsi")])


if __name__ == "__main__":
    main()
