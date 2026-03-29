#!/usr/bin/env python3
"""Probe the packaged OpenBabel runtime in a Windows installer tree.

Usage:
  python scripts/installer/probe_openbabel_runtime.py scripts\\installer\\dist
"""

from __future__ import annotations

import ctypes
import glob
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Iterable


def _print_header(title: str) -> None:
    print(f"\n=== {title} ===")


def _list_paths(paths: Iterable[Path], label: str) -> None:
    print(f"{label}:")
    for p in paths:
        print(f"  - {p}")


def _find_version_dir(openbabel_root: Path) -> Path:
    candidates = sorted([p for p in openbabel_root.iterdir() if p.is_dir()])
    if not candidates:
        raise FileNotFoundError(f"No versioned plugin directories under: {openbabel_root}")
    # Prefer semver-ish directory names.
    semver = [p for p in candidates if re.match(r"^\d+\.\d+(\.\d+)?", p.name)]
    if semver:
        return semver[0]
    return candidates[0]


def _find_glob(base: Path, pattern: str) -> list[Path]:
    return sorted([Path(p) for p in glob.glob(str(base / pattern))])


def _try_load(path: Path) -> tuple[bool, str]:
    try:
        ctypes.WinDLL(str(path))
        return True, "ok"
    except OSError as exc:
        return False, str(exc)


def _run(cmd: list[str], env: dict[str, str]) -> tuple[int, str, str]:
    proc = subprocess.run(
        cmd,
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )
    return proc.returncode, proc.stdout, proc.stderr


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: probe_openbabel_runtime.py <dist-dir>", file=sys.stderr)
        return 2

    dist = Path(sys.argv[1]).resolve()
    bin_dir = dist / "bin"
    lib_root = dist / "lib" / "openbabel"
    share_root = dist / "share" / "openbabel"
    fragments_root = dist / "share" / "avogadro" / "fragments"

    _print_header("Packaged tree roots")
    _list_paths([dist, bin_dir, lib_root, share_root, fragments_root], "Resolved paths")
    for p in [dist, bin_dir, lib_root, share_root]:
        print(f"exists({p}) = {p.exists()}")

    if not bin_dir.exists() or not lib_root.exists() or not share_root.exists():
        print("ERROR: packaged OpenBabel directories are incomplete.", file=sys.stderr)
        return 1

    version_dir = _find_version_dir(lib_root)
    plugin_dir = version_dir
    data_dir = share_root / version_dir.name

    _print_header("Versioned OpenBabel paths")
    print(f"plugin_dir={plugin_dir}")
    print(f"data_dir={data_dir} exists={data_dir.exists()}")

    openbabel_dll = bin_dir / "openbabel-3.dll"
    formats_xml = plugin_dir / "formats_xml.obf"
    plugin_descriptors = plugin_dir / "plugin_descriptors.obf"
    formats_common = plugin_dir / "formats_common.obf"
    obabel_exe = bin_dir / "obabel.exe"

    libxml_dlls = _find_glob(bin_dir, "libxml2*.dll")
    maeparser_dlls = _find_glob(bin_dir, "maeparser*.dll")
    coordgen_dlls = _find_glob(bin_dir, "coordgen*.dll")
    boost_dlls = _find_glob(bin_dir, "boost_*.dll")

    _print_header("Required and related runtime artifacts")
    required = [openbabel_dll, formats_xml, plugin_descriptors]
    for p in required:
        print(f"{p.name}: exists={p.exists()} path={p}")
    print(f"formats_common.obf: exists={formats_common.exists()} path={formats_common}")
    _list_paths(libxml_dlls, "libxml2 DLLs")
    _list_paths(maeparser_dlls, "maeparser DLLs")
    _list_paths(coordgen_dlls, "coordgen DLLs")
    _list_paths(boost_dlls[:20], "boost DLLs (first 20)")

    env = os.environ.copy()
    env["BABEL_LIBDIR"] = str(plugin_dir)
    env["BABEL_DATADIR"] = str(data_dir)
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")

    if hasattr(os, "add_dll_directory"):
        # Python 3.8+ on Windows
        os.add_dll_directory(str(bin_dir))
        os.add_dll_directory(str(plugin_dir))

    _print_header("ctypes direct-load probes")
    load_targets: list[Path] = [openbabel_dll]
    load_targets.extend(libxml_dlls[:1])
    load_targets.extend(maeparser_dlls[:1])
    load_targets.extend(coordgen_dlls[:1])
    if formats_common.exists():
        load_targets.append(formats_common)
    load_targets.extend([formats_xml, plugin_descriptors])

    load_results: dict[str, tuple[bool, str]] = {}
    for target in load_targets:
        ok, msg = _try_load(target)
        load_results[str(target)] = (ok, msg)
        print(f"load {target.name}: {'OK' if ok else 'FAIL'} ({msg})")

    _print_header("obabel command probes")
    commands = [
        ["obabel", "-V"],
        ["obabel", "-L", "formats"],
        ["obabel", "-H", "cml"],
    ]
    cmd_results: dict[str, tuple[int, str, str]] = {}
    for cmd in commands:
        code, out, err = _run(cmd, env)
        key = " ".join(cmd)
        cmd_results[key] = (code, out, err)
        print(f"\n$ {key}")
        print(f"exit_code={code}")
        print("--- stdout ---")
        print(out.rstrip() or "<empty>")
        print("--- stderr ---")
        print(err.rstrip() or "<empty>")

    _print_header("Sample fragment CML conversion probe")
    cml_candidates = sorted(fragments_root.rglob("*.cml")) if fragments_root.exists() else []
    sample_cml = cml_candidates[0] if cml_candidates else None
    conversion_ok = False
    conversion_cmd = None
    if sample_cml is None:
        print(f"No packaged fragment .cml file found under {fragments_root}")
    else:
        conversion_cmd = ["obabel", "-icml", str(sample_cml), "-ocan"]
        code, out, err = _run(conversion_cmd, env)
        print(f"$ {' '.join(conversion_cmd)}")
        print(f"exit_code={code}")
        print("--- stdout ---")
        print(out.rstrip() or "<empty>")
        print("--- stderr ---")
        print(err.rstrip() or "<empty>")
        conversion_ok = code == 0 and bool(out.strip())

    formats_list_out = cmd_results.get("obabel -L formats", (1, "", ""))[1]
    cml_in_list = bool(re.search(r"(?im)^\s*cml(\s|$)", formats_list_out))
    formats_xml_load_ok = load_results.get(str(formats_xml), (False, "not attempted"))[0]

    _print_header("Heuristic summary")
    print(f"cml_listed_by_obabel={cml_in_list}")
    print(f"formats_xml_obf_loadable={formats_xml_load_ok}")
    print(f"sample_cml_conversion_ok={conversion_ok}")

    likely = "another runtime issue"
    if not formats_xml.exists():
        likely = "plugin missing"
    elif not formats_xml_load_ok:
        likely = "plugin dependency load failure"
    elif not cml_in_list:
        likely = "plugin discovery/path failure or plugin loaded but cml not registered"
    elif not conversion_ok:
        likely = "plugin loaded but runtime conversion still failing"
    print(f"likely_failure_class={likely}")

    fail = False
    if not formats_xml_load_ok:
        print("FAIL: formats_xml.obf is not loadable via ctypes.", file=sys.stderr)
        fail = True
    if not cml_in_list:
        print("FAIL: obabel -L formats does not list cml.", file=sys.stderr)
        fail = True
    if not conversion_ok:
        print("FAIL: sample CML conversion probe failed.", file=sys.stderr)
        fail = True

    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
