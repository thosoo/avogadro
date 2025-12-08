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
    if ob_dir:
        ob = ob_dir

        ob_version = os.environ.get("OPENBABEL_VERSION")
        if not ob_version:
            header = ob / "include" / "openbabel3" / "openbabel" / "babelconfig.h"
            if not header.exists():
                raise FileNotFoundError(
                    f"OpenBabel headers not found at {header}; set OPENBABEL_INSTALL_DIR"
                )

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
            raise RuntimeError("Could not determine OpenBabel version to bundle")

        plugin_roots = []
        for plugins in [
            ob / "lib" / "openbabel" / ob_version,
            ob / "lib" / "openbabel",
            ob / "bin" / "openbabel" / ob_version,
            ob / "bin" / "openbabel",
            ob / "plugins",
            ob / "bin" / "plugins",
        ]:
            if plugins.exists():
                plugin_roots.append(plugins)

        if not plugin_roots:
            # Look for any directory that already contains OpenBabel plugins
            # (plugin_*.dll) or compiled format bundles (*.obf) and copy from
            # there. This covers layouts produced by different Windows installs
            # as well as CMake package builds where plugins end up in a
            # top-level "plugins" directory.
            plugin_matches = set()
            for pattern in ("plugin_*.dll", "*.obf"):
                for plugin_file in ob.rglob(pattern):
                    plugin_matches.add(plugin_file.parent)
            plugin_roots.extend(sorted(plugin_matches))

        if not plugin_roots:
            all_dirs = "\n".join(str(p) for p in sorted(ob.iterdir())) if ob.exists() else "<missing>"
            raise FileNotFoundError(
                f"OpenBabel plugin directory not found in {ob}; set OPENBABEL_INSTALL_DIR."
                f" Contents inspected:\n{all_dirs}"
            )

        for f in ob.glob("bin/*"):
            if f.suffix.lower() in (".dll", ".exe", ".obf"):
                copy(f, dist / "bin")

        dest_plugins = dist / "lib" / "openbabel" / ob_version
        dest_plugins.mkdir(parents=True, exist_ok=True)

        for plugins in plugin_roots:
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
        if not share.exists():
            raise FileNotFoundError(
                f"OpenBabel data directory not found in {ob}; set OPENBABEL_INSTALL_DIR"
            )

        dest = dist / "share" / "openbabel" / ob_version
        log(f"Copying OpenBabel data from {share} to {dest}")
        shutil.copytree(share, dest, dirs_exist_ok=True)
        patterns = ["*.txt", "*.par", "*.prm", "*.ff", "*.dat"]
        for pat in patterns:
            for f in share.glob(pat):
                if f.is_file():
                    copy(f, dist / "bin")
        if alt_share.exists() and alt_share != share:
            for pat in ["*.txt", "*.par", "*.prm", "*.ff", "*.dat"]:
                for f in alt_share.glob(pat):
                    if f.is_file():
                        copy(f, dist / "bin")

    libxml = os.environ.get("LIBXML2_LIBRARY")
    if libxml:
        libxml_path = Path(libxml)

        def add_candidate(path, candidates, seen):
            if path not in seen:
                candidates.append(path)
                seen.add(path)

        candidates = []
        seen_candidates = set()

        add_candidate(libxml_path.with_suffix(".dll"), candidates, seen_candidates)
        add_candidate(libxml_path.parent / "libxml2.dll", candidates, seen_candidates)
        add_candidate(libxml_path.parent / "libxml2-2.dll", candidates, seen_candidates)
        add_candidate(libxml_path.parent.parent / "bin" / "libxml2.dll", candidates, seen_candidates)
        add_candidate(libxml_path.parent.parent / "bin" / "libxml2-2.dll", candidates, seen_candidates)

        search_roots = {
            libxml_path.parent,
            libxml_path.parent.parent,
            libxml_path.parent.parent / "bin",
            libxml_path.parent.parent / "lib",
        }

        for root in search_roots:
            if not root.exists():
                continue
            for pattern in ("libxml2*.dll", "xml2*.dll"):
                for dll in root.rglob(pattern):
                    if dll.is_file():
                        add_candidate(dll, candidates, seen_candidates)

        for dll in candidates:
            if dll.exists():
                copy(dll, dist / "bin")
                break
        else:
            print(
                "Warning: LIBXML2_LIBRARY set but libxml2 DLL not found near",
                libxml_path,
            )

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

    # Copy the GPLv2 license expected by NSIS and ensure it exists for packaging
    license_dest = dist / "gpl.txt"

    def add_license_candidate(path, candidates, seen):
        if path not in seen:
            candidates.append(path)
            seen.add(path)

    license_candidates = []
    seen_license_candidates = set()

    for base in [
        root.parent.parent,
        root.parent,
        root,
        Path.cwd(),
        build_dir,
        build_dir.parent,
    ]:
        add_license_candidate(base / "COPYING", license_candidates, seen_license_candidates)
        add_license_candidate(base / "COPYING.txt", license_candidates, seen_license_candidates)
        add_license_candidate(base / "LICENSE", license_candidates, seen_license_candidates)
        add_license_candidate(base / "LICENSE.txt", license_candidates, seen_license_candidates)

    for ancestor in Path(__file__).resolve().parents:
        add_license_candidate(ancestor / "COPYING", license_candidates, seen_license_candidates)
        add_license_candidate(ancestor / "LICENSE", license_candidates, seen_license_candidates)

    for ancestor in Path.cwd().parents:
        add_license_candidate(ancestor / "COPYING", license_candidates, seen_license_candidates)
        add_license_candidate(ancestor / "LICENSE", license_candidates, seen_license_candidates)

    license_src = None
    for candidate in license_candidates:
        if candidate.exists():
            license_src = candidate
            break

    if not license_src:
        searched = "\n".join(str(p) for p in license_candidates)
        raise FileNotFoundError(
            "License file not found. Looked for a COPYING or LICENSE file at:\n" f"{searched}"
        )

    copy(license_src, license_dest)
    if not license_dest.exists():
        raise FileNotFoundError(f"Failed to place license file at {license_dest}")

    version = os.environ.get("AVOGADRO_VERSION")
    vi_version = None
    if not version:
        cmake_candidates = []

        def add_cmake_candidate(path):
            if path not in cmake_candidates:
                cmake_candidates.append(path)

        add_cmake_candidate(root.parent.parent / "CMakeLists.txt")
        add_cmake_candidate(Path.cwd() / "CMakeLists.txt")
        add_cmake_candidate(build_dir.parent / "CMakeLists.txt")

        cmake_lists = None
        for path in cmake_candidates:
            if path.exists():
                cmake_lists = path
                break

        if not cmake_lists:
            searched = "\n".join(str(p) for p in cmake_candidates)
            raise FileNotFoundError(
                "Could not locate CMakeLists.txt to determine version. Searched:\n"
                f"{searched}"
            )

        text = cmake_lists.read_text()
        maj = re.search(r"Avogadro_VERSION_MAJOR\s+(\d+)", text)
        min_ = re.search(r"Avogadro_VERSION_MINOR\s+(\d+)", text)
        patch = re.search(r"Avogadro_VERSION_PATCH\s+(\d+)", text)
        if maj and min_ and patch:
            version = f"{maj.group(1)}.{min_.group(1)}.{patch.group(1)}"
            vi_version = version + ".0"
    setup_candidates = []

    def add_setup_candidate(path):
        if path not in setup_candidates:
            setup_candidates.append(path)

    add_setup_candidate(root / "setup.nsi")
    add_setup_candidate(Path.cwd() / "setup.nsi")
    add_setup_candidate(Path.cwd() / "scripts" / "installer" / "setup.nsi")
    add_setup_candidate(root.parent / "setup.nsi")
    add_setup_candidate(root.parent / "installer" / "setup.nsi")
    add_setup_candidate(root.parent.parent / "scripts" / "installer" / "setup.nsi")

    setup_script = None
    for cand in setup_candidates:
        if cand.exists():
            setup_script = cand
            break

    if not setup_script:
        searched = "\n".join(str(p) for p in setup_candidates)
        raise FileNotFoundError(
            "Could not locate setup.nsi for NSIS packaging. Searched:\n" f"{searched}"
        )

    args = ["makensis", f"/DPRODUCT_LICENSE_FILE={license_dest}"]
    if version:
        args.append(f"/DVERSION={version}")
    if vi_version:
        args.append(f"/DVI_VERSION={vi_version}")
    args.append(str(setup_script))
    log(f"Running makensis with script {setup_script}")
    subprocess.check_call(args, cwd=root)


if __name__ == "__main__":
    main()
