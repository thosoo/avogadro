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
    ob_dir = Path(ob_dir_env) if ob_dir_env else None
    ob_bindir = os.environ.get("OPENBABEL_BINDIR")
    if ob_bindir and ob_dir is None:
        ob_dir = Path(ob_bindir).parent
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
            ob_version = "2"

        for f in ob.glob("bin/*"):
            if f.suffix.lower() in (".dll", ".exe", ".obf"):
                copy(f, dist / "bin")

        dest_plugins = dist / "lib" / "openbabel" / ob_version
        dest_plugins.mkdir(parents=True, exist_ok=True)

        for plugins in [ob / "lib" / "openbabel", ob / "bin" / "openbabel"]:
            if plugins.exists():
                log(f"Copying plugins from {plugins} to {dest_plugins}")
                shutil.copytree(plugins, dest_plugins, dirs_exist_ok=True)

        for dll in ob.glob("bin/plugin_*.dll"):
            copy(dll, dest_plugins)
        for obf in ob.glob("bin/plugin_*.obf"):
            copy(obf, dest_plugins)

        share = ob / "share" / "openbabel" / ob_version
        alt_share = ob / "bin" / "data"
        if not share.exists() and alt_share.exists():
            share = alt_share
        if share.exists():
            dest = dist / "share" / "openbabel" / ob_version
            log(f"Copying OpenBabel data from {share} to {dest}")
            shutil.copytree(share, dest, dirs_exist_ok=True)
            patterns = ["*.txt", "*.par", "*.prm", "*.ff", "*.dat"]
            for pat in patterns:
                for f in share.glob(pat):
                    if f.is_file():
                        copy(f, dist / "bin")
        if alt_share.exists():
            for pat in ["*.txt", "*.par", "*.prm", "*.ff", "*.dat"]:
                for f in alt_share.glob(pat):
                    if f.is_file():
                        copy(f, dist / "bin")

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

    mklroot = os.environ.get("MKLROOT")
    if mklroot:
        mkl_dir = Path(mklroot) / 'redist' / 'intel64'
        for dll in mkl_dir.rglob('mkl_rt*.dll'):
            copy(dll, dist / 'bin')
            break

    oneapi_root = os.environ.get("ONEAPI_ROOT")
    if oneapi_root:
        redist = Path(oneapi_root) / 'compiler' / 'latest' / 'windows' / 'redist'
        for sub in ('intel64_win/compiler', 'intel64/compiler'):
            omp_dir = redist / sub
            if not omp_dir.exists():
                continue
            for dll in omp_dir.glob('*.dll'):
                if 'debug' not in dll.name.lower():
                    copy(dll, dist / 'bin')

        # Ensure Fortran runtime libraries are bundled
        fortran_libs = [
            'libifcoremd.dll',
            'libifportmd.dll',
            'libimf.dll',
            'libirc.dll',
            'svml_dispmd.dll',
            'libdecimal.dll',
            'libintlc.dll',
        ]
        search_bases = [redist, Path(oneapi_root) / 'compiler' / 'latest' / 'redist']
        subdirs = ['intel64_win/compiler', 'intel64/compiler', 'intel64_win', 'intel64']
        for base in search_bases:
            for sub in subdirs:
                lib_dir = base / sub
                if not lib_dir.exists():
                    continue
                for flib in fortran_libs:
                    dll = lib_dir / flib
                    if dll.exists():
                        copy(dll, dist / 'bin')

    xtb_dir = os.environ.get("XTB_DIR")
    if xtb_dir:
        xtb = Path(xtb_dir)
        for f in xtb.glob('bin/*'):
            if f.suffix.lower() in ('.exe', '.dll'):
                copy(f, dist / 'bin')
        share = xtb / 'share' / 'xtb'
        if share.exists():
            dest = dist / 'share' / 'xtb'
            log(f"Copying xTB data from {share} to {dest}")
            shutil.copytree(share, dest, dirs_exist_ok=True)

        bat = dist / 'bin' / 'Avogadro-xTB.bat'
        with open(bat, 'w', encoding='utf8') as f:
            f.write('@echo off\n')
            f.write('set "XTBPATH=%~dp0..\\share\\xtb"\n')
            f.write('"%~dp0Avogadro.exe" %*\n')

        lib_dest = dist / 'lib'
        lib_dest.mkdir(parents=True, exist_ok=True)
        for f in xtb.glob('lib/*.dll'):
            copy(f, lib_dest)
        for f in xtb.glob('lib/*.lib'):
            copy(f, lib_dest)

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
    args.append(str(root / "setup.nsi"))
    log("Running makensis")
    subprocess.check_call(args)


if __name__ == "__main__":
    main()
