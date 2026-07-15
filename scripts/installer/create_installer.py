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

    def env_truthy(name):
        return os.environ.get(name, "").strip().upper() in {"1", "ON", "TRUE", "YES"}

    def is_zlib_runtime_name(name):
        lower = name.lower()
        return lower == "z.dll" or (lower.startswith("zlib") and lower.endswith(".dll"))

    def check_no_missing_zlib_runtime_dependencies():
        bin_dir = dist / "bin"
        dumpbin = shutil.which("dumpbin")
        if not dumpbin:
            raise RuntimeError("dumpbin was not found; cannot verify zlib runtime DLL dependencies")

        bundled_zlib = {p.name.lower() for p in bin_dir.glob("*.dll") if is_zlib_runtime_name(p.name)}
        missing = []
        binaries = sorted(
            p for p in bin_dir.iterdir()
            if p.is_file() and p.suffix.lower() in {".dll", ".exe"}
        )
        for binary in binaries:
            output = subprocess.check_output(
                [dumpbin, "/dependents", str(binary)],
                text=True,
                errors="replace",
            )
            for line in output.splitlines():
                dep = line.strip()
                if is_zlib_runtime_name(dep) and dep.lower() not in bundled_zlib:
                    missing.append((binary, dep))

        if missing:
            details = "\n".join(f"{binary} depends on missing {dep}" for binary, dep in missing)
            bundled = ", ".join(sorted(bundled_zlib)) or "<none>"
            raise RuntimeError(
                "Packaged binaries depend on a zlib runtime DLL that is not bundled in dist/bin. "
                "This usually means a dependency was linked dynamically against zlib.\n"
                f"Bundled zlib runtime DLLs: {bundled}\n{details}"
            )

        print("Verified packaged binaries do not depend on an unbundled zlib runtime DLL.")

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

        # OpenBabel installs runtime data to bin/data for MSVC builds. The
        # installed Avogadro executable sets BABEL_DATADIR to its application
        # directory's data subdirectory, so package the complete OpenBabel data
        # directory there too. Keep the share/openbabel copy above for tools or
        # layouts that still look for the Unix-style data location.
        bin_data = dist / "bin" / "data"
        log(f"Copying OpenBabel data from {share} to {bin_data}")
        shutil.copytree(share, bin_data, dirs_exist_ok=True)

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

        space_groups = bin_data / "space-groups.txt"
        if not space_groups.exists():
            raise FileNotFoundError(
                f"Packaged OpenBabel data is missing required file: {space_groups}"
            )

    libxml = os.environ.get("LIBXML2_LIBRARY")
    if libxml:
        libxml_lib = Path(libxml)
        libxml_candidates = [libxml_lib.with_suffix('.dll')]
        libxml_candidates.extend((libxml_lib.parent.parent / "bin").glob("libxml2*.dll"))
        copied = False
        for dll in libxml_candidates:
            if dll.exists():
                copy(dll, dist / "bin")
                copied = True
        if not copied:
            raise FileNotFoundError(
                f"Could not locate libxml2 runtime DLL from LIBXML2_LIBRARY={libxml_lib}"
            )

    zlib_lib = os.environ.get("ZLIB_LIBRARY")
    zlib_dir = os.environ.get("ZLIB_LIBRARY_DIR")
    if env_truthy("ZLIB_IS_STATIC"):
        print("ZLIB_IS_STATIC is set; not bundling a separate zlib runtime DLL.")
    else:
        candidates = []
        if zlib_dir:
            zdir = Path(zlib_dir)
            candidates.extend([
                zdir.parent / "bin" / "z.dll",
                zdir.parent / "bin" / "zlib.dll",
                zdir.parent / "bin" / "zlib1.dll",
            ])
        if zlib_lib:
            zlib_path = Path(zlib_lib)
            candidates.extend([
                zlib_path.parent.parent / "bin" / "z.dll",
                zlib_path.parent.parent / "bin" / "zlib.dll",
                zlib_path.parent.parent / "bin" / "zlib1.dll",
                zlib_path.with_suffix(".dll"),
            ])
        if zlib_dir:
            zdir = Path(zlib_dir)
            candidates.extend([
                zdir / "zlib1.dll",
                zdir / "zlib.dll",
            ])

        # preserve order while deduplicating
        seen = set()
        zlib_candidates = []
        for c in candidates:
            key = str(c)
            if key not in seen:
                seen.add(key)
                zlib_candidates.append(c)

        zlib_runtime = None
        for dll in zlib_candidates:
            if dll.exists():
                zlib_runtime = dll
                break

        if zlib_runtime:
            print(f"Using zlib runtime DLL: {zlib_runtime}")
            copy(zlib_runtime, dist / "bin")
        elif zlib_lib or zlib_dir:
            attempted = "\n".join(str(p) for p in zlib_candidates) or "<none>"
            raise FileNotFoundError(
                "Could not locate zlib runtime DLL from provided ZLIB variables. "
                f"Tried:\n{attempted}"
            )

    glew_bin = os.environ.get("GLEW_BIN_DIR")
    if glew_bin:
        dll = Path(glew_bin) / 'glew32.dll'
        if dll.exists():
            copy(dll, dist / 'bin')

    check_no_missing_zlib_runtime_dependencies()

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
