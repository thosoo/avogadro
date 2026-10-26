#!/usr/bin/env python3
import os
import re
import shutil
import subprocess
from pathlib import Path


# Windows system DLLs that are always present on the target OS. Bundled
# binaries may import these without bundling them themselves.
SYSTEM_DLL_ALLOWLIST = frozenset({
    "kernel32.dll", "kernelbase.dll", "user32.dll", "gdi32.dll", "advapi32.dll",
    "shell32.dll", "shellapi.dll", "shlwapi.dll", "comctl32.dll", "comdlg32.dll",
    "ole32.dll", "oleaut32.dll", "uuid.dll", "ws2_32.dll", "mswsock.dll",
    "wininet.dll", "winmm.dll", "imm32.dll", "netapi32.dll", "rasapi32.dll",
    "dnsapi.dll", "iphlpapi.dll", "mpr.dll", "crypt32.dll", "setupapi.dll",
    "cfgmgr32.dll", "version.dll", "dwmapi.dll", "oleacc.dll", "devenum.dll",
    "strmi32.dll", "winusb.dll", "usbhub3.dll", "ntdll.dll", "uuid.dll",
    # GPU / OpenGL / D3D surface plus the OS components Qt, glew, opengl32sw,
    # d3dcompiler_47, libxml2 and vc_redist import directly. All ship with
    # Windows (named by the closure gate at run 40298973463; present on the
    # reporting machine's ModLoad lines); the gate fails on any that are absent
    # from the allowlist, so these are documented here rather than bundled.
    "authz.dll", "userenv.dll", "dwrite.dll", "d3d11.dll", "d3d12.dll",
    "dxgi.dll", "secur32.dll", "uxtheme.dll", "winhttp.dll", "opengl32.dll",
    "glu32.dll", "bcrypt.dll", "bcryptprimitives.dll", "rpcrt4.dll",
    "wsock32.dll", "imagehlp.dll", "cabinet.dll", "wintrust.dll", "msi.dll",
})

# Debug C/C++ runtime names that do NOT exist on end-user machines. Kept for the
# Boost DLL filter (task 2.3); the closure allowlist is release-only so a debug
# import still surfaces as a hard failure if the filter misses one.
DEBUG_RUNTIME_NAMES = frozenset({
    "ucrtbased.dll", "msvcp140d.dll", "msvcp140d_ucrt.dll", "msvcp140d_mda.dll",
    "vcruntime140d.dll", "vcruntime140d_u.dll", "vcruntime140d_da.dll",
    "vcomp140d.dll",
})


def is_allowed_system_dll(name):
    """True if a dependency on this DLL is acceptable without bundling it.

    Covers the UWP API shims (api-ms-*), the UCRT (ucrtbase.dll) and the VC++
    redistributable RELEASE set (msvcp140*, vcruntime140*, vcomp140), plus the
    known Windows system DLL set. Debug runtimes are intentionally excluded: a
    binary importing them should fail the closure gate (or be filtered by the
    Boost debug-DLL filter), never be silently accepted.
    """
    lower = name.lower()
    # Debug runtimes never exist on a clean machine; reject them outright so a
    # debug import fails the gate (or is dropped by the Boost filter) rather than
    # being accepted by the release-redist regexes below.
    if lower in DEBUG_RUNTIME_NAMES:
        return False
    if lower.startswith("api-ms-") or lower.startswith("api-"):
        return True
    if lower in {"ucrtbase.dll", "msvcrt.dll"}:
        return True
    # Release VC++ redistributable set only. The optional underscore suffix may
    # itself contain underscores (e.g. msvcp140_codecvt_ids.dll, msvcp140_atomic_wait.dll);
    # the required leading underscore still excludes bare debug variants
    # (msvcp140d, vcruntime140d), and the DEBUG_RUNTIME_NAMES early-return above
    # excludes the *-d runtime DLLs outright. The previous class [a-z0-9] could
    # not match the second underscore segment of _codecvt_ids, so that release
    # redist DLL slipped through as "missing" (run 40298973463).
    if re.fullmatch(r"msvcp14[0-9]+(_[a-z0-9_]+)?\.dll", lower):
        return True
    if re.fullmatch(r"vcruntime14[0-9]+(_[a-z0-9_]+)?\.dll", lower):
        return True
    if lower == "vcomp140.dll":
        return True
    return lower in SYSTEM_DLL_ALLOWLIST


def is_debug_runtime_dll(dumpbin, name, dll_path=None):
    """True for debug C/C++ runtime DLLs that cannot load on a clean machine.

    Matches the Boost debug naming (``*_gd`` / ``-gd-``) and explicit debug
    runtime names, and falls back to inspecting the import table for a debug
    runtime dependency (ucrtbased / MSVCP140D / VCRUNTIME140D) when the name
    alone is inconclusive.
    """
    lower = name.lower()
    if "_gd" in lower or "-gd-" in lower:
        return True
    if lower in DEBUG_RUNTIME_NAMES:
        return True
    if dll_path is not None and dumpbin:
        try:
            output = subprocess.check_output(
                [dumpbin, "/dependents", str(dll_path)],
                text=True, errors="replace",
            )
        except subprocess.CalledProcessError:
            return False
        for line in output.splitlines():
            token = os.path.basename(line.strip()).lower()
            if (token.startswith("ucrtbased")
                    or "msvcp140d" in token
                    or "vcruntime140d" in token):
                return True
    return False


def pe_files(dist):
    """Every PE (dll/exe) under dist\\bin and the plugin tree dist\\lib\\avogadro\\**.

    The plugin tree is inspected as importers too, so a DLL dropped into a
    colors/engines/extensions/tools subdirectory is closure-checked.
    """
    files = [p for p in (dist / "bin").iterdir()
             if p.is_file() and p.suffix.lower() in {".dll", ".exe"}]
    plugin_tree = dist / "lib" / "avogadro"
    if plugin_tree.exists():
        files.extend(p for p in plugin_tree.rglob("*")
                     if p.is_file() and p.suffix.lower() in {".dll", ".exe"})
    return files


def imported_dlls(dumpbin, binary):
    """Return the set of DLL/EXE names in binary's import table."""
    output = subprocess.check_output(
        [dumpbin, "/dependents", str(binary)],
        text=True, errors="replace",
    )
    imports = set()
    for line in output.splitlines():
        token = os.path.basename(line.strip())
        low = token.lower()
        if low.endswith(".dll") or low.endswith(".exe"):
            imports.add(token)
    return imports


def scan_closure(dumpbin, dist):
    """Scan every bundled PE's import table.

    Returns a list of (importer_name, missing_import) pairs for imports that are
    neither present in the package nor on the OS/redistributable allowlist.
    """
    present = {p.name.lower() for p in pe_files(dist)}
    missing = []
    for binary in sorted(pe_files(dist)):
        try:
            imports = imported_dlls(dumpbin, binary)
        except subprocess.CalledProcessError:
            continue
        for dep in imports:
            if dep.lower() in present:
                continue
            if is_allowed_system_dll(dep):
                continue
            missing.append((binary.name, dep))
    return missing


def copy_missing_qt_modules(dumpbin, dist, qt6_dir):
    """Copy Qt6 module DLLs that bundled binaries import but windeployqt did not.

    windeployqt scans only the main executable's static imports, so Qt modules
    imported exclusively by plugins or bundled libraries (today Qt6Concurrent.dll)
    are invisible to it. This is import-table-driven and iterates to a fixpoint: a
    freshly copied Qt module may itself import another missing Qt module. Returns
    the list of copied DLL names. Nothing is copied if qt6_dir is falsy.
    """
    if not qt6_dir:
        return []
    qt6_bin = Path(qt6_dir) / "bin"
    if not qt6_bin.is_dir():
        print(f"Qt6_DIR set to {qt6_dir} but {qt6_bin} is missing; skipping top-up")
        return []
    copied = []
    while True:
        missing = scan_closure(dumpbin, dist)
        qt_missing = {dep for _, dep in missing if dep.lower().startswith("qt6")}
        to_copy = sorted(p for p in qt_missing if (qt6_bin / p).exists())
        if not to_copy:
            break
        for name in to_copy:
            src = qt6_bin / name
            print(f"Qt-module top-up: copying {src} -> {dist / 'bin' / name}")
            shutil.copy(src, dist / "bin" / name)
            copied.append(name)
    if copied:
        print(f"Copied {len(copied)} missing Qt6 module(s) from Qt installation: "
              + ", ".join(copied))
    else:
        print("Qt-module top-up: no missing Qt6 modules found.")
    return copied


def check_dependency_closure(dumpbin, dist):
    """Hard gate: every packaged PE must resolve its non-system imports.

    Prints a binary -> missing-DLL report and raises on any unresolved import so
    the installer build fails instead of shipping an un-loadable package.
    """
    missing = scan_closure(dumpbin, dist)
    if not missing:
        print("Verified packaged binaries resolve all non-system dependencies.")
        return
    by_binary = {}
    for binary, dep in missing:
        by_binary.setdefault(binary, []).append(dep)
    lines = []
    for binary in sorted(by_binary):
        deps = ", ".join(sorted(set(by_binary[binary])))
        lines.append(f"{binary} imports missing {deps}")
    raise RuntimeError(
        "Packaged binaries depend on DLLs that are neither bundled nor on the "
        "OS/redistributable allowlist. Installation will fail with "
        "'module could not be found'.\n" + "\n".join(lines)
    )


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

        # Debug-runtime DLL filter + import-driven Boost selection (task 2.3 /
        # windows-installer-runtime): drop debug C/C++ runtime payloads
        # (Boost *-gd-* variants and anything importing ucrtbased /
        # MSVCP140D / VCRUNTIME140D) that cannot load on a clean machine, and drop
        # every boost_* variant from the bulk copy (selected below, import-driven).
        # Release non-Boost DLLs importable on a clean machine are kept
        # (e.g. maeparser, openbabel-3). .obf bundles are copied unconditionally:
        # OpenBabel plugin discovery LoadLibrarys them.
        #
        # The choco Boost prefix carries variants nothing here loads
        # (boost_filesystem/log/python312/stacktrace_windbg*); shipping them only
        # forces allowlisting the runtimes they reference (python312.dll,
        # dbgeng.dll) that are absent from clean machines. So re-add only the
        # boost_* variants that some packaged PE actually imports, iterating to a
        # fixpoint (a freshly copied variant may itself import another). This
        # keeps the change's import-driven closure (design D2 / windows-installer
        # spec): boost_iostreams / bzip2 / zlib (via maeparser) survive; dead
        # variants vanish. The closure gate in check_dependency_closure is the
        # same-run verifier -- if any packaged PE imports a dropped variant, the
        # next gate call fails loudly naming the importer.
        dumpbin = shutil.which("dumpbin")
        for f in ob.glob("bin/*"):
            if f.suffix.lower() not in (".dll", ".exe", ".obf"):
                continue
            if f.name.lower().startswith("boost_"):
                continue  # selected below, import-driven
            if f.suffix.lower() == ".obf":
                copy(f, dist / "bin")
                continue
            if is_debug_runtime_dll(dumpbin, f.name, f):
                log(f"Skipping debug-runtime DLL (not user-loadable): {f.name}")
                continue
            copy(f, dist / "bin")

        boost_prefix_bin = ob / "bin"
        if boost_prefix_bin.is_dir() and dumpbin:
            while True:
                needed = {
                    dep
                    for binary in pe_files(dist)
                    for dep in imported_dlls(dumpbin, binary)
                    if dep.lower().startswith("boost_")
                }
                to_copy = sorted(
                    n for n in needed if not (dist / "bin" / n).exists()
                )
                if not to_copy:
                    break
                resolved = []
                for name in to_copy:
                    matches = [
                        p for p in boost_prefix_bin.iterdir()
                        if p.name.lower() == name.lower()
                    ]
                    if not matches:
                        log(
                            f"Boost variant needed by a packaged binary but not "
                            f"found in the OpenBabel prefix: {name}"
                        )
                        break
                    resolved.append(matches[0])
                if len(resolved) != len(to_copy):
                    break
                for src in resolved:
                    log(f"Import-kept Boost variant: {src.name}")
                    copy(src, dist / "bin" / src.name)

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

    dumpbin = shutil.which("dumpbin")
    if not dumpbin:
        raise RuntimeError("dumpbin was not found; cannot verify dependency closure")

    # Qt-module top-up (task 2.2 / windows-installer-runtime): windeployqt scans
    # only the main executable's static imports, so Qt modules imported only by
    # plugins or bundled libraries (today Qt6Concurrent.dll) are invisible to it.
    # Copy any missing Qt6 module from the Qt installation before the gate runs.
    # Qt6_DIR is pinned by the workflow's Configure step.
    qt6_dir = (os.environ.get("Qt6_DIR")
               or os.environ.get("QT_ROOT")
               or os.environ.get("QT_DIR"))
    copy_missing_qt_modules(dumpbin, dist, qt6_dir)

    # Subsumed by the closure check below (kept for its specific message).
    check_no_missing_zlib_runtime_dependencies()

    # Hard gate (task 2.1 / windows-installer-runtime): every packaged PE must
    # resolve its non-system imports, or the build fails and no installer is
    # produced.
    check_dependency_closure(dumpbin, dist)

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
