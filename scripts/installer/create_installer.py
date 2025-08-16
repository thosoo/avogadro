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
    ob_bindir = os.environ.get("OPENBABEL_BINDIR")
    if ob_bindir and ob_dir is None:
        ob_dir = Path(ob_bindir).parent
    ob_version = None
    if ob_dir:
        ob = ob_dir

        ob_version = os.environ.get("OPENBABEL_VERSION")
        if not ob_version:
            header = ob / "include" / "openbabel3" / "openbabel" / "babelconfig.h"
            if header.exists():
                m = re.search(r"BABEL_VERSION\s+\"([^\"]+)\"", header.read_text())
                if m:
                    ob_version = m.group(1)
        if not ob_version:
            share_dir = ob / "share" / "openbabel"
            if share_dir.exists():
                for sub in share_dir.iterdir():
                    if sub.is_dir() and sub.name[0].isdigit():
                        ob_version = sub.name
                        break
        if not ob_version:
            exe = None
            for cand in (ob / "bin" / "obabel.exe", ob / "bin" / "babel.exe", ob / "bin" / "obabel", ob / "bin" / "babel"):
                if cand.exists():
                    exe = cand
                    break
            if exe:
                try:
                    out = subprocess.check_output([str(exe), "--version"], text=True)
                    m = re.search(r"Open Babel\s+(\d+\.\d+\.\d+)", out)
                    if m:
                        ob_version = m.group(1)
                except Exception:
                    pass
        if not ob_version:
            ob_version = "3.1.1"

        for f in ob.glob("bin/*"):
            if f.suffix.lower() in (".dll", ".exe", ".obf"):
                copy(f, dist / "bin")

        plugins_src = None
        for base in (ob / "bin" / "plugins" / "openbabel", ob / "lib" / "openbabel"):
            if base.exists():
                subdirs = [d for d in base.iterdir() if d.is_dir()]
                if subdirs:
                    plugins_src = subdirs[0]
                    if not ob_version:
                        ob_version = plugins_src.name
                else:
                    plugins_src = base
                break
        if plugins_src:
            dest_plugins = dist / "bin" / "plugins" / "openbabel"
            if ob_version:
                dest_plugins /= ob_version
            log(f"Copying OpenBabel plugins from {plugins_src} to {dest_plugins}")
            shutil.copytree(plugins_src, dest_plugins, dirs_exist_ok=True)

        share = ob / "share" / "openbabel" / ob_version
        alt_share = ob / "bin" / "data"
        src_share = share if share.exists() else alt_share if alt_share.exists() else None
        if src_share:
            # Copy to the versioned share directory
            dest = dist / "share" / "openbabel" / ob_version
            log(f"Copying OpenBabel data from {src_share} to {dest}")
            shutil.copytree(src_share, dest, dirs_exist_ok=True)

            # Also place data in bin/data for Windows builds so default paths work
            dest_data = dist / "bin" / "data"
            log(f"Copying OpenBabel data from {src_share} to {dest_data}")
            shutil.copytree(src_share, dest_data, dirs_exist_ok=True)

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
    if version:
        args.append(f"/DVERSION={version}")
    if vi_version:
        args.append(f"/DVI_VERSION={vi_version}")
    if ob_version:
        args.append(f"/DOB_VERSION={ob_version}")
    args.append(str(root / "setup.nsi"))
    log("Running makensis")
    subprocess.check_call(args)


if __name__ == "__main__":
    main()
