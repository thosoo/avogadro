#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent
    dist = root / "dist"
    build_dir = Path(os.environ.get("BUILD_DIR", root.parent.parent / "build"))

    if dist.exists():
        shutil.rmtree(dist)
    dist.mkdir(parents=True)

    def log(msg):
        if os.environ.get("VERBOSE"):
            print(msg)

    def copy(src, dst):
        log(f"Copying {src} -> {dst}")
        shutil.copy(src, dst)

    log(f"Installing build from {build_dir} into {dist}")
    subprocess.check_call(["cmake", "--install", str(build_dir), "--prefix", str(dist)])

    exe = dist / "bin" / "Avogadro.exe"
    if not exe.exists():
        exe = dist / "bin" / "avogadro.exe"
    log("Running windeployqt")
    subprocess.check_call(["windeployqt", "--release", "--dir", str(dist / "bin"), str(exe)])

    ob_dir_env = os.environ.get("OPENBABEL_INSTALL_DIR")
    ob_dir = Path(ob_dir_env) if ob_dir_env else build_dir / "openbabel-install"
    ob_build = build_dir / "openbabel_ext-prefix" / "src" / "openbabel_ext-build"
    ob_bindir = os.environ.get("OPENBABEL_BINDIR")
    if ob_bindir and ob_dir is None:
        ob_dir = Path(ob_bindir).parent
    if ob_dir:
        ob = ob_dir

        ob_version = os.environ.get("OPENBABEL_VERSION")
        if not ob_version:
            header = ob / "include" / "openbabel3" / "openbabel" / "babelconfig.h"
            if header.exists():
                txt = header.read_text()
                m = re.search(r"BABEL_VERSION\s+\"([^\"]+)\"", txt)
                if not m:
                    m = re.search(r"OB_VERSION\s+\"([^\"]+)\"", txt)
                if m:
                    ob_version = m.group(1)
                else:
                    raise RuntimeError("Failed to parse Open Babel version from babelconfig.h")
            else:
                raise RuntimeError(f"Missing {header}")
        if not ob_version:
            raise RuntimeError("Unable to determine Open Babel version")

        dest_plugins = dist / "lib" / "openbabel" / ob_version
        if (ob / "lib" / "openbabel" / ob_version).exists():
            shutil.copytree(ob / "lib" / "openbabel" / ob_version, dest_plugins, dirs_exist_ok=True)

        for f in ob.glob("bin/*"):
            if f.suffix.lower() in (".dll", ".exe", ".obf"):
                if f.name.lower().startswith("openbabel-3") and f.suffix.lower() == ".dll":
                    continue
                copy(f, dist / "bin")

        for plugins in [ob / "bin" / "openbabel"]:
            if plugins.exists():
                shutil.copytree(plugins, dest_plugins, dirs_exist_ok=True)

        stray = dist / "bin" / "openbabel-3.dll"
        if stray.exists():
            stray.unlink()

        env_bat = dist / "bin" / "Avogadro-ob.bat"
        env_bat.write_text(
            "@echo off\n"
            f"set \"BABEL_LIBDIR=%~dp0..\\lib\\openbabel\\{ob_version}\"\n"
            f"set \"BABEL_DATADIR=%~dp0..\\share\\openbabel\\{ob_version}\"\n"
            "start \"\" \"%~dp0Avogadro.exe\" %*\n"
        )

        share = ob / "share" / "openbabel" / ob_version
        build_share = ob_build / "share" / "openbabel" / ob_version
        if not share.exists() and build_share.exists():
            share = build_share
        if share.exists():
            dest = dist / "share" / "openbabel" / ob_version
            shutil.copytree(share, dest, dirs_exist_ok=True)


    libxml = os.environ.get("LIBXML2_LIBRARY")
    if libxml:
        dll = Path(libxml).with_suffix('.dll')
        if dll.exists():
            copy(dll, dist / "bin")

    zlib_lib = os.environ.get("ZLIB_LIBRARY")
    zlib_dir = os.environ.get("ZLIB_LIBRARY_DIR")
    candidates = []
    if zlib_lib:
        candidates.append(Path(zlib_lib).with_suffix('.dll'))
    if zlib_dir:
        candidates.append(Path(zlib_dir) / 'zlib1.dll')
        candidates.append(Path(zlib_dir) / 'zlib.dll')
    for dll in candidates:
        if dll.exists():
            copy(dll, dist / 'bin')
            break

    glew_bin = os.environ.get("GLEW_BIN_DIR")
    if glew_bin:
        dll = Path(glew_bin) / 'glew32.dll'
        if dll.exists():
            copy(dll, dist / 'bin')

    # Copy the GPLv2 license expected by NSIS
    license_src = root.parent.parent / 'COPYING'
    license_dest = dist / 'gpl.txt'
    if license_src.exists():
        copy(license_src, license_dest)

    version = os.environ.get("AVOGADRO_VERSION")
    vi_version = None
    if not version:
        cmake_lists = root.parent.parent / "CMakeLists.txt"
        text = cmake_lists.read_text()
        maj = re.search(r"Avogadro_VERSION_MAJOR\s+(\d+)", text)
        min_ = re.search(r"Avogadro_VERSION_MINOR\s+(\d+)", text)
        patch = re.search(r"Avogadro_VERSION_PATCH\s+(\d+)", text)
        if maj and min_ and patch:
            version = f"{maj.group(1)}.{min_.group(1)}.{patch.group(1)}"
            vi_version = version + ".0"
    args = ["makensis"]
    if ob_version:
        args.append(f"/DOB_VERSION={ob_version}")
    if version:
        args.append(f"/DVERSION={version}")
    if vi_version:
        args.append(f"/DVI_VERSION={vi_version}")
    args.append(str(root / "setup.nsi"))
    log("Running makensis")
    subprocess.check_call(args)


if __name__ == "__main__":
    main()
